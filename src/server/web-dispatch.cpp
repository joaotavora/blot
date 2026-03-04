#include <boost/beast/http.hpp>
#include <boost/json.hpp>
#include <filesystem>
#include <fstream>
#include <web-config.hpp>

namespace beast = boost::beast;
namespace http = beast::http;
namespace json = boost::json;
namespace fs = std::filesystem;

using response_t = http::response<http::string_body>;
using request_t = http::request<http::string_body>;

namespace xpto::blot {

response_t make_json_response(
    http::status status_code, const json::value& body,
    unsigned int http_version, bool keep_alive) {
  response_t res{status_code, http_version};
  res.set(http::field::content_type, "application/json");
  res.set(http::field::access_control_allow_origin, "*");
  res.keep_alive(keep_alive);
  res.body() = json::serialize(body);
  res.prepare_payload();
  return res;
}

response_t make_error(
    http::status status_code, std::string_view message,
    unsigned int http_version, bool keep_alive) {
  json::object obj;
  obj["error"] = message;
  return make_json_response(status_code, obj, http_version, keep_alive);
}

static const std::array kSrcExts{
  std::string_view{".c"}, std::string_view{".cpp"}, std::string_view{".h"},
  std::string_view{".hpp"}};

static bool is_source_file(const fs::path& p) {
  auto ext = p.extension();
  return std::any_of(
      kSrcExts.begin(), kSrcExts.end(), [&](auto e) { return ext == e; });
}

static std::optional<std::vector<std::string>> try_git_ls_files(
    const fs::path& root) {
  std::string cmd = "git -C \"" + root.string() + "\" ls-files 2>/dev/null";
  FILE* pipe = popen(cmd.c_str(), "r");  // NOLINT(cert-env33-c)
  if (!pipe) return std::nullopt;
  std::string output;
  std::array<char, 4096> buf{};
  while (fgets(buf.data(), buf.size(), pipe)) output += buf.data();
  if (pclose(pipe) != 0) return std::nullopt;
  std::vector<std::string> result;
  std::istringstream ss{output};
  std::string line;
  while (std::getline(ss, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (!line.empty() && is_source_file(line))
      result.push_back(std::move(line));
  }
  return result;
}

static std::string decode_uri(std::string s) {
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
}

std::vector<std::string> list_source_files(const fs::path& root) {
  if (fs::exists(root / ".git")) {
    if (auto files = try_git_ls_files(root)) {
      std::sort(files->begin(), files->end());
      return std::move(*files);
    }
  }
  std::vector<std::string> result;
  std::error_code ec;
  for (auto& entry : fs::recursive_directory_iterator(root, ec)) {
    if (ec) break;
    if (!entry.is_regular_file()) continue;
    if (!is_source_file(entry.path())) continue;
    auto rel = fs::relative(entry.path(), root, ec);
    if (!ec) result.push_back(rel.string());
  }
  std::sort(result.begin(), result.end());
  return result;
}

response_t dispatch(
    const request_t& req, const fs::path& ccj_path,
    const fs::path& project_root) {
  const auto version = req.version();
  const bool keep_alive = req.keep_alive();
  const auto target = std::string(req.target());

  if (req.method() == http::verb::get && !target.starts_with("/api/")) {
    std::string rel = (target == "/" || target == "/index.html")
                          ? "index.html"
                          : target.substr(1);
    std::error_code ec;
    fs::path file_path = fs::weakly_canonical(fs::path{k_web_root} / rel, ec);
    if (!ec && file_path.string().starts_with(k_web_root) &&
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

        response_t res{http::status::ok, version};
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

  if (req.method() == http::verb::get && target == "/api/status") {
    // FIXME: reads and fully parses the entire compile_commands.json on every
    // poll just to get the array length.  Cache the count (or the parsed
    // value) and invalidate on file modification time change.
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

  if (req.method() == http::verb::get && target == "/api/files") {
    // FIXME: list_source_files() either forks git or walks the directory tree
    // on every request with no caching.  Cache the result and invalidate on
    // a directory-level mtime/inotify change.
    auto srcs = list_source_files(project_root);
    json::object obj;
    json::array arr(srcs.begin(), srcs.end());
    obj["files"] = arr;
    return make_json_response(http::status::ok, obj, version, keep_alive);
  }

  if (req.method() == http::verb::get && target.starts_with("/api/source")) {
    auto qpos = target.find('?');
    if (qpos == std::string::npos)
      return make_error(
          http::status::bad_request, "missing ?file=", version, keep_alive);
    std::string query = target.substr(qpos + 1);
    std::string file_param;
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

    std::error_code ec;
    fs::path requested = fs::weakly_canonical(project_root / file_param, ec);
    if (ec || !requested.string().starts_with(project_root.string()))
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

  return make_error(http::status::not_found, "not found", version, keep_alive);
}

}  // namespace xpto::blot
