#pragma once

#define BOOST_ASIO_NO_DEPRECATED
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/json.hpp>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>

#include "web.hpp"

namespace json = boost::json;
namespace net = boost::asio;
namespace fs = std::filesystem;

namespace xpto::blot::tests {

/// Plain HTTP response bag — no Beast types in sight.
struct http_response {
  int status{};
  std::string body;
  json::object json_body() const { return json::parse(body).as_object(); }
};

/// Owns an io_context and starts the web server on it.
struct test_server {
  net::io_context ioc;
  int port;

  explicit test_server(const fs::path& ccj)
      : port{xpto::blot::run_web_server(ioc.get_executor(), ccj, 0)} {}
};

/// Run an HTTP test coroutine on ioc alongside the server.
template <typename Coro>
static void run_http_test(net::io_context& ioc, Coro coro) {
  std::exception_ptr ep;
  net::co_spawn(ioc, std::move(coro), [&ep, &ioc](std::exception_ptr e) {
    ep = e;  // NOLINT(*-value-param)
    ioc.stop();
  });
  ioc.run();
  if (ep) std::rethrow_exception(ep);
}

/// Owns a thread pool and starts the web server on it (for concurrency tests).
struct ws_test_server {
  net::thread_pool pool{4};
  int port;

  explicit ws_test_server(const fs::path& ccj)
      : port{xpto::blot::run_web_server(pool.get_executor(), ccj, 0)} {}
};

/// Run a WS test coroutine on its own io_context.
template <typename Coro>
static void run_ws_test(ws_test_server& /*srv*/, Coro coro) {
  net::io_context ioc;
  std::exception_ptr ep;
  net::co_spawn(ioc, std::move(coro), [&ep, &ioc](std::exception_ptr e) {
    ep = e;  // NOLINT(*-value-param)
    ioc.stop();
  });
  ioc.run();
  if (ep) std::rethrow_exception(ep);
}

/// Abstract WebSocket client — no Beast types exposed.
/// Implementations live in http-tests-beast.cpp.
struct ws_client {
  virtual ~ws_client() = default;
  ws_client() = default;
  ws_client(ws_client&&) = delete;
  ws_client& operator=(ws_client&&) = delete;
  ws_client(const ws_client&) = delete;
  ws_client& operator=(const ws_client&) = delete;

  virtual net::awaitable<void> send(const json::object& req) = 0;
  virtual net::awaitable<json::object> recv_response() = 0;
  virtual net::awaitable<std::map<int64_t, json::object>> recv_n_responses(
      int n) = 0;
};

/// Connect to the WebSocket endpoint at /ws on the given port.
/// Defined in http-tests-beast.cpp.
net::awaitable<std::unique_ptr<ws_client>> connect_ws(int port);

/// Abstract HTTP client — no Beast types exposed.
/// Implementations live in http-tests-beast.cpp.
struct http_client {
  virtual ~http_client() = default;
  http_client() = default;
  http_client(http_client&&) = delete;
  http_client& operator=(http_client&&) = delete;
  http_client(const http_client&) = delete;
  http_client& operator=(const http_client&) = delete;

  virtual net::awaitable<http_response> get(std::string_view path) = 0;
};

/// Synchronous factory (no connection handshake needed for HTTP).
/// Defined in http-tests-beast.cpp.
std::unique_ptr<http_client> connect_http(int port);

inline fs::path fixture_dir(std::string_view name) {
  return fs::path{TEST_FIXTURE_DIR} / name;
}

inline fs::path fixture_ccj(std::string_view name) {
  return fixture_dir(name) / "compile_commands.json";
}

}  // namespace xpto::blot::tests
