#define BOOST_ASIO_NO_DEPRECATED
#include "web.hpp"

#include <fmt/core.h>

#include <atomic>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/json.hpp>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#include "logger.hpp"
#include "session.hpp"
#include "web_config.hpp"

namespace beast = boost::beast;
namespace http = beast::http;
namespace json = boost::json;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = net::ip::tcp;

namespace xpto::blot {

namespace fs = std::filesystem;

struct ws_session : session {
  using stream_t = beast::websocket::stream<beast::tcp_stream>;
  stream_t ws;
  std::mutex write_mutex;
  std::atomic<bool> shutdown_requested{false};

  ws_session(
      stream_t ws, const fs::path& ccj_path, const fs::path& project_root)
      : ws{std::move(ws)}, session{ccj_path, project_root} {}

  void send(const json::object& msg) override {
    auto text = json::serialize(msg);
    std::lock_guard lk{write_mutex};
    beast::error_code ec{};
    ws.write(boost::asio::buffer(text), ec);
    if (ec) LOG_INFO("ws_send error: {}", ec.message());
  }
};

/// Helpers

using response_t = http::response<http::string_body>;
using request_t = http::request<http::string_body>;

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

// Try to list source files via `git ls-files` (respects .gitignore).
// Returns nullopt if git is unavailable or exits non-zero.
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

// List .c/.cpp/.h/.hpp files under root.
// Uses git ls-files when a .git directory is present, otherwise walks the tree.
std::vector<std::string> list_source_files(const fs::path& root) {
  if (fs::exists(root / ".git")) {
    if (auto files = try_git_ls_files(root)) {
      std::sort(files->begin(), files->end());
      return std::move(*files);
    }
  }
  // Fallback: recursive directory walk
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

/// Request dispatch

response_t dispatch(
    const request_t& req, const fs::path& ccj_path,
    const fs::path& project_root) {
  const auto version = req.version();
  const bool keep_alive = req.keep_alive();
  const auto target = std::string(req.target());

  // files (anything not under /api/)
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

  // GET /api/status
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

  // GET /api/files
  if (req.method() == http::verb::get && target == "/api/files") {
    auto srcs = list_source_files(project_root);
    json::object obj;
    json::array arr(srcs.begin(), srcs.end());
    obj["files"] = arr;
    return make_json_response(http::status::ok, obj, version, keep_alive);
  }

  // GET /api/source?file=...
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

net::awaitable<void> process_frame(
    std::shared_ptr<ws_session> sess, std::string text) {
  if (!sess->handle_frame(text))
    sess->shutdown_requested.store(true, std::memory_order_relaxed);
  co_return;
}

net::awaitable<void> run_ws_session(
    websocket::stream<beast::tcp_stream> ws, fs::path ccj_path,
    fs::path project_root) {
  ws.text(true);
  auto sess =
      std::make_shared<ws_session>(std::move(ws), ccj_path, project_root);
  auto ex = co_await net::this_coro::executor;
  for (;;) {
    if (sess->shutdown_requested.load(std::memory_order_relaxed)) break;
    beast::flat_buffer buf{};
    co_await sess->ws.async_read(buf, net::use_awaitable);
    net::co_spawn(
        ex, process_frame(sess, beast::buffers_to_string(buf.data())),
        net::detached);
  }
  LOG_INFO("ws session ended");
}

/// Connection handler

net::awaitable<void> handle_connection(
    tcp::socket socket, fs::path ccj_path, fs::path project_root) {
  beast::tcp_stream stream{std::move(socket)};
  beast::flat_buffer buffer;
  for (;;) {
    request_t req;
    co_await http::async_read(stream, buffer, req, net::use_awaitable);
    LOG_INFO("{} {}", req.method_string(), req.target());

    if (websocket::is_upgrade(req) && req.target() == "/ws") {
      websocket::stream<beast::tcp_stream> ws{std::move(stream)};
      co_await ws.async_accept(req, net::use_awaitable);
      LOG_INFO("ws session started");
      co_await run_ws_session(
          std::move(ws), std::move(ccj_path), std::move(project_root));
      co_return;
    }

    auto res = dispatch(req, ccj_path, project_root);
    LOG_INFO("→ {}", static_cast<unsigned>(res.result_int()));
    co_await http::async_write(stream, res, net::use_awaitable);
    if (!req.keep_alive()) break;
  }
  beast::error_code ec;
  stream.socket().shutdown(tcp::socket::shutdown_send, ec);
}

/// Server loop

net::awaitable<void> accept_loop(
    tcp::acceptor acceptor, fs::path ccj_path, fs::path project_root) {
  for (;;) {
    tcp::socket socket = co_await acceptor.async_accept(net::use_awaitable);
    boost::system::error_code ec;
    auto remote = socket.remote_endpoint(ec);
    LOG_INFO(
        "connection from {}:{}", ec ? "?" : remote.address().to_string(),
        ec ? 0 : remote.port());
    net::co_spawn(
        acceptor.get_executor(),
        handle_connection(std::move(socket), ccj_path, project_root),
        net::detached);
  }
}

int run_web_server(
    const net::any_io_executor& ex, const fs::path& ccj_path, int port) {
  fs::path project_root = fs::absolute(ccj_path).parent_path();

  tcp::acceptor acceptor{
    ex, tcp::endpoint{tcp::v4(), static_cast<unsigned short>(port)}};
  acceptor.set_option(net::socket_base::reuse_address{true});

  int bound_port = static_cast<int>(acceptor.local_endpoint().port());

  net::co_spawn(
      ex, accept_loop(std::move(acceptor), ccj_path, project_root),
      net::detached);
  return bound_port;
}

}  // namespace xpto::blot
