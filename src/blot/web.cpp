#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#define BOOST_ASIO_NO_DEPRECATED
#include <fmt/core.h>

#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "logger.hpp"
#include "session.hpp"
#include "web.hpp"
#include "web_config.hpp"

namespace beast = boost::beast;
namespace http = beast::http;
namespace json = boost::json;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = net::ip::tcp;

namespace xpto::blot {

namespace fs = std::filesystem;

struct ws_session : session<ws_session> {
  using stream_t = beast::websocket::stream<beast::tcp_stream>;
  ws_session(
      stream_t& ws, const fs::path& ccj_path, const fs::path& project_root)
      : ws{&ws}, session{ccj_path, project_root} {}
  void send(const json::object& msg) {
    beast::error_code ec{};
    auto text = json::serialize(msg);
    ws->write(boost::asio::buffer(text), ec);
    if (ec) LOG_INFO("ws_send error: {}", ec.message());
  }
  stream_t* ws;  // NOLINT
};

/// Helpers

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

/// Request dispatch

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

  return make_error(http::status::not_found, "not found", version, keep_alive);
}

static void run_ws_session(
    websocket::stream<beast::tcp_stream> ws, const fs::path& ccj_path,
    const fs::path& project_root) {
  beast::error_code ec{};
  ws.text(true);
  ws_session sess{ws, ccj_path, project_root};
  for (;;) {
    beast::flat_buffer buf{};
    ws.read(buf, ec);
    if (ec == websocket::error::closed || ec) break;
    if (!sess.handle_frame(beast::buffers_to_string(buf.data()))) break;
  }
  LOG_INFO("ws session ended");
}

/// Connection handler

static void handle_connection(
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

    // ── WebSocket upgrade ─────────────────────────────────
    if (websocket::is_upgrade(req) && req.target() == "/ws") {
      websocket::stream<beast::tcp_stream> ws{std::move(stream)};
      beast::error_code wec;
      ws.accept(req, wec);
      if (!wec) {
        LOG_INFO("ws session started");
        run_ws_session(std::move(ws), ccj_path, project_root);
      }
      return;  // skip shutdown below
    }

    auto res = dispatch(req, ccj_path, project_root);
    LOG_INFO("→ {}", static_cast<unsigned>(res.result_int()));
    http::write(stream, res, ec);
    if (ec || !req.keep_alive()) break;
  }

  stream.socket().shutdown(boost::asio::ip::tcp::socket::shutdown_send, ec);
}

/// Server loop

void run_web_server(const fs::path& ccj_path, int port) {
  fs::path project_root = fs::absolute(ccj_path).parent_path();

  fmt::println("blot --web: listening on http://localhost:{}", port);
  fmt::println("  project root : {}", project_root.string());
  fmt::println("  ccj          : {}", ccj_path.string());
  fmt::println("  press Ctrl-C to stop");
  std::cout.flush();

  net::io_context ioc;
  tcp::acceptor acceptor{
    ioc, tcp::endpoint{tcp::v4(), static_cast<unsigned short>(port)}};
  acceptor.set_option(net::socket_base::reuse_address{true});

  // Cap the live thread count to avoid unbounded growth.
  constexpr int kMaxThreads{4};
  std::atomic<int> active{0};
  std::vector<std::thread> threads;
  threads.reserve(64);

  for (;;) {
    tcp::socket socket{ioc};
    boost::system::error_code ec;
    acceptor.accept(socket, ec);
    if (ec) break;  // acceptor was closed (e.g. signal)

    // Simple back-pressure: spin until a slot opens.
    while (active.load() >= kMaxThreads) {
      std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }

    boost::system::error_code ec2;
    auto remote = socket.remote_endpoint(ec2);
    LOG_INFO(
        "connection from {}:{}", ec2 ? "?" : remote.address().to_string(),
        ec2 ? 0 : remote.port());
    ++active;
    // Transfer socket ownership into the thread via native handle.
    int fd = socket.release();
    threads.emplace_back([fd, &ccj_path, &project_root, &active]() {
      handle_connection(fd, ccj_path, project_root);
      --active;
    });
  }

  for (auto& t : threads) {
    if (t.joinable()) t.join();
  }
}

}  // namespace xpto::blot
