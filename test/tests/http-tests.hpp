#pragma once

#include <boost/asio/any_io_executor.hpp>
#include <thread>
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
  int port;
  fs::path ccj;

  explicit test_server(
      const net::any_io_executor& ex, fs::path ccj, fs::path project_root)
  : port{xpto::blot::run_web_server(ex, ccj, project_root, 0)},
        ccj{std::move(ccj)} {}
};

/// Run a test coroutine on ioc. Calls restart() first to allow ioc reuse.
template <typename Coro>
static void run_ioc_test(net::io_context& ioc, Coro coro, size_t nthreads = 1) {
  ioc.restart();
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

}  // namespace xpto::blot::tests
