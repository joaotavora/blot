#include <sys/socket.h>
#include <unistd.h>

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/json.hpp>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>

#include "../libblot/json_helpers.hpp"
#include "../libblot/linespan.hpp"
#include "../libblot/logger.hpp"
#include "blot/assembly.hpp"
#include "blot/blot.hpp"
#include "blot/ccj.hpp"
#include "web_config.hpp"
#include "web_server.hpp"

namespace beast = boost::beast;
namespace http = beast::http;
namespace json = boost::json;

namespace xpto::blot {

namespace fs = std::filesystem;

// Serialize all annotate calls (libclang is not thread-safe).
static std::mutex
    g_annotate_mutex;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

// ── Helpers ───────────────────────────────────────────────────────────────

static http::response<http::string_body> make_json_response(
    http::status status_code, const json::value& body,
    unsigned int http_version, bool keep_alive) {
  http::response<http::string_body> res{status_code, http_version};
  res.set(http::field::content_type, "application/json");
  res.set(http::field::access_control_allow_origin, "*");
  res.keep_alive(keep_alive);
  res.body() = json::serialize(body);
  res.prepare_payload();
  return res;
}

static http::response<http::string_body> make_error(
    http::status status_code, std::string_view message,
    unsigned int http_version, bool keep_alive) {
  json::object obj;
  obj["error"] = message;
  return make_json_response(status_code, obj, http_version, keep_alive);
}

// List .c/.cpp/.h/.hpp files under project_root (relative paths).
static std::vector<std::string> list_source_files(const fs::path& root) {
  std::vector<std::string> result;
  std::error_code ec;
  for (auto& entry : fs::recursive_directory_iterator(root, ec)) {
    if (ec) break;
    if (!entry.is_regular_file()) continue;
    auto ext = entry.path().extension();
    if (ext != ".c" && ext != ".cpp" && ext != ".h" && ext != ".hpp") continue;
    auto rel = fs::relative(entry.path(), root, ec);
    if (!ec) result.push_back(rel.string());
  }
  std::sort(result.begin(), result.end());
  return result;
}

// ── Request dispatch ──────────────────────────────────────────────────────

template <typename Body, typename Allocator>
static http::response<http::string_body> dispatch(
    const http::request<Body, http::basic_fields<Allocator>>& req,
    const fs::path& ccj_path, const fs::path& project_root) {
  const auto version = req.version();
  const bool keep_alive = req.keep_alive();
  const auto target = std::string(req.target());

  // ── Static files (anything not under /api/) ──────────────
  if (req.method() == http::verb::get && !target.starts_with("/api/")) {
    // Map "/" or "/index.html" → "index.html", strip leading "/"
    std::string rel = (target == "/" || target == "/index.html")
                          ? "index.html"
                          : target.substr(1);
    std::error_code ec;
    fs::path file_path = fs::weakly_canonical(fs::path{kWebRoot} / rel, ec);
    if (!ec && file_path.string().starts_with(kWebRoot) &&
        fs::is_regular_file(file_path, ec) && !ec) {
      std::ifstream f{file_path};
      if (f) {
        auto ext = file_path.extension().string();
        std::string_view ct = "application/octet-stream";
        if (ext == ".html")
          ct = "text/html; charset=utf-8";
        else if (ext == ".css")
          ct = "text/css";
        else if (ext == ".js")
          ct = "application/javascript";

        http::response<http::string_body> res{http::status::ok, version};
        res.set(http::field::content_type, ct);
        res.keep_alive(keep_alive);
        res.body() = {std::istreambuf_iterator<char>{f}, {}};
        res.prepare_payload();
        return res;
      }
    }
    return make_error(
        http::status::not_found, rel + ": not found in web root", version,
        keep_alive);
  }

  // ── GET /api/status ──────────────────────────────────────
  if (req.method() == http::verb::get && target == "/api/status") {
    std::error_code ec;
    auto entries = json::parse(
        [&]() -> std::string {
          std::ifstream f{ccj_path.string()};
          return {std::istreambuf_iterator<char>{f}, {}};
        }(),
        ec);
    json::object obj;
    obj["ccj"] = ccj_path.string();
    obj["project_root"] = project_root.string();
    obj["tu_count"] = ec ? 0 : static_cast<int>(entries.as_array().size());
    return make_json_response(http::status::ok, obj, version, keep_alive);
  }

  // ── GET /api/files ───────────────────────────────────────
  if (req.method() == http::verb::get && target == "/api/files") {
    auto srcs = list_source_files(project_root);
    json::object obj;
    json::array arr(srcs.begin(), srcs.end());
    obj["files"] = arr;
    return make_json_response(http::status::ok, obj, version, keep_alive);
  }

  // ── GET /api/source?file=... ─────────────────────────────
  if (req.method() == http::verb::get && target.starts_with("/api/source")) {
    auto qpos = target.find('?');
    if (qpos == std::string::npos)
      return make_error(
          http::status::bad_request, "missing ?file=", version, keep_alive);
    std::string query = target.substr(qpos + 1);
    // Simple URL-decode of the file= param (handles %20 and +)
    auto decode_uri = [](std::string s) -> std::string {
      std::string out;
      out.reserve(s.size());
      for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '+') {
          out += ' ';
          continue;
        }
        if (s[i] == '%' && i + 2 < s.size()) {
          auto h = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
          };
          int hi = h(s[i + 1]), lo = h(s[i + 2]);
          if (hi >= 0 && lo >= 0) {
            out += static_cast<char>(hi * 16 + lo);
            i += 2;
            continue;
          }
        }
        out += s[i];
      }
      return out;
    };
    std::string file_param;
    // Parse "file=..." out of query string
    std::istringstream qs{query};
    std::string kv;
    while (std::getline(qs, kv, '&')) {
      if (kv.starts_with("file=")) {
        file_param = decode_uri(kv.substr(5));
        break;
      }
    }
    if (file_param.empty())
      return make_error(
          http::status::bad_request, "missing file param", version, keep_alive);

    // Path traversal check
    std::error_code ec;
    fs::path requested = fs::weakly_canonical(project_root / file_param, ec);
    if (ec || requested.string().find(project_root.string()) != 0)
      return make_error(
          http::status::forbidden, "path traversal denied", version,
          keep_alive);

    std::ifstream f{requested.string()};
    if (!f)
      return make_error(
          http::status::not_found, "file not found", version, keep_alive);

    std::string content{std::istreambuf_iterator<char>{f}, {}};
    json::object obj;
    obj["file"] = file_param;
    obj["content"] = content;
    return make_json_response(http::status::ok, obj, version, keep_alive);
  }

  // ── POST /api/annotate ───────────────────────────────────
  if (req.method() == http::verb::post && target == "/api/annotate") {
    std::error_code ec;
    auto body_val = json::parse(req.body(), ec);
    if (ec)
      return make_error(
          http::status::bad_request, "invalid JSON body", version, keep_alive);

    auto& body_obj = body_val.as_object();
    if (!body_obj.contains("file"))
      return make_error(
          http::status::bad_request, "missing 'file'", version, keep_alive);

    std::string file_str{body_obj.at("file").as_string()};

    annotation_options aopts{};
    if (body_obj.contains("options")) {
      auto& opts = body_obj.at("options").as_object();
      auto get_bool = [&](std::string_view key, bool& dst) {
        if (opts.contains(key)) {
          if (auto* b = opts.at(key).if_bool()) dst = *b;
        }
      };
      get_bool("demangle", aopts.demangle);
      get_bool("preserve_directives", aopts.preserve_directives);
      get_bool("preserve_comments", aopts.preserve_comments);
      get_bool("preserve_library_functions", aopts.preserve_library_functions);
      get_bool("preserve_unused_labels", aopts.preserve_unused_labels);
    }

    // Path traversal check
    fs::path src_path = fs::weakly_canonical(project_root / file_str, ec);
    if (ec || src_path.string().find(project_root.string()) != 0)
      return make_error(
          http::status::forbidden, "path traversal denied", version,
          keep_alive);

    json::object result;
    result["file"] = file_str;
    result["annotation_options"] = aopts_to_json(aopts);

    LOG_INFO("annotate: {}", src_path.string());
    std::lock_guard<std::mutex> lock{g_annotate_mutex};
    try {
      auto cmd = infer(ccj_path, src_path);
      if (!cmd) {
        LOG_INFO("annotate: no CCJ entry for {}", src_path.string());
        json::object err;
        err["name"] = "not_found";
        err["details"] = "No compile_commands.json entry found for this file";
        result["error"] = err;
        return make_json_response(
            http::status::ok, result, version, keep_alive);
      }
      LOG_INFO("annotate: compiling with {}", cmd->command);
      auto c_result = get_asm(*cmd);
      LOG_INFO(
          "annotate: annotating {} bytes of assembly",
          c_result.assembly.size());
      result["compiler_invocation"] = meta_to_json(c_result.invocation);
      auto annotation = annotate_to_json(c_result.assembly, aopts, src_path);
      result.insert(annotation.begin(), annotation.end());
      LOG_INFO("annotate: done");
    } catch (compilation_error& e) {
      result["compiler_invocation"] = meta_to_json(e.invocation);
      result["error"] = error_to_json(e);
      auto& desc = result["error"].as_object();
      xpto::linespan ls{e.dribble};
      desc["dribble"] = json::array(ls.begin(), ls.end());
    } catch (std::exception& e) {
      result["error"] = error_to_json(e);
    }
    return make_json_response(http::status::ok, result, version, keep_alive);
  }

  return make_error(http::status::not_found, "not found", version, keep_alive);
}

// ── Connection handler ────────────────────────────────────────────────────

void handle_connection(
    int socket_fd, const fs::path& ccj_path, const fs::path& project_root) {
  // Each worker thread owns its own io_context for purely synchronous use.
  boost::asio::io_context ioc;
  boost::asio::ip::tcp::socket raw_sock{ioc};
  boost::system::error_code ec;
  raw_sock.assign(boost::asio::ip::tcp::v4(), socket_fd, ec);
  if (ec) {
    ::close(socket_fd);
    return;
  }

  beast::tcp_stream stream{std::move(raw_sock)};
  beast::flat_buffer buffer;

  for (;;) {
    http::request<http::string_body> req;
    http::read(stream, buffer, req, ec);
    if (ec == http::error::end_of_stream || ec) break;

    LOG_INFO("{} {}", req.method_string(), req.target());
    auto res = dispatch(req, ccj_path, project_root);
    LOG_INFO("→ {}", static_cast<unsigned>(res.result_int()));
    http::write(stream, res, ec);
    if (ec || !req.keep_alive()) break;
  }

  stream.socket().shutdown(boost::asio::ip::tcp::socket::shutdown_send, ec);
}

}  // namespace xpto::blot
