#define BOOST_ASIO_NO_DEPRECATED
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/json.hpp>
#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include "http-tests.hpp"

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace json = boost::json;
using tcp = net::ip::tcp;

namespace xpto::blot::tests {

/// beast_http_client — Beast implementation of http_client.

struct beast_http_client : http_client {
  int port_;

  explicit beast_http_client(int port) : port_{port} {}

  net::awaitable<http_response> get(std::string_view path) override {
    auto ex = co_await net::this_coro::executor;
    // FIXME: DNS resolve + new TCP connection opened on every get() call.
    // For the shared http_srv() tests, reuse a persistent connection instead.
    tcp::resolver resolver{ex};
    beast::tcp_stream stream{ex};

    auto results = co_await resolver.async_resolve(
        "127.0.0.1", std::to_string(port_), net::use_awaitable);
    co_await stream.async_connect(results, net::use_awaitable);

    http::request<http::string_body> req{
      http::verb::get, std::string{path}, 11};
    req.set(http::field::host, "127.0.0.1");
    req.set(http::field::connection, "close");
    co_await http::async_write(stream, req, net::use_awaitable);

    beast::flat_buffer buf;
    http::response<http::string_body> res;
    co_await http::async_read(stream, buf, res, net::use_awaitable);

    beast::error_code ec;
    stream.socket().shutdown(tcp::socket::shutdown_both, ec);
    co_return http_response{
      static_cast<int>(res.result_int()), std::string{res.body()}};
  }
};

std::unique_ptr<http_client> connect_http(int port) {
  return std::make_unique<beast_http_client>(port);
}

/// beast_ws_client — Beast implementation of ws_client.

struct beast_ws_client : ws_client {
  websocket::stream<beast::tcp_stream> ws_;

  explicit beast_ws_client(websocket::stream<beast::tcp_stream> ws)
      : ws_{std::move(ws)} {}

  // NOLINTNEXTLINE(*-coroutine-parameters), because this is always co-awaited
  net::awaitable<void> send(const json::object& req) override {
    co_await ws_.async_write(
        net::buffer(json::serialize(req)), net::use_awaitable);
  }

  net::awaitable<json::object> recv_response() override {
    for (;;) {
      beast::flat_buffer buf{};
      co_await ws_.async_read(buf, net::use_awaitable);
      auto msg = json::parse(beast::buffers_to_string(buf.data())).as_object();
      if (!msg.contains("method")) co_return msg;
    }
  }

  net::awaitable<std::map<int64_t, json::object>> recv_n_responses(
      int n) override {
    std::map<int64_t, json::object> out;
    while (static_cast<int>(out.size()) < n) {
      beast::flat_buffer buf{};
      co_await ws_.async_read(buf, net::use_awaitable);
      auto msg = json::parse(beast::buffers_to_string(buf.data())).as_object();
      if (msg.contains("method")) continue;
      out[msg.at("id").as_int64()] = msg;
    }
    co_return out;
  }
};

net::awaitable<std::unique_ptr<ws_client>> connect_ws(int port) {
  auto ex = co_await net::this_coro::executor;
  tcp::resolver resolver{ex};
  beast::tcp_stream tcp_s{ex};

  auto results = co_await resolver.async_resolve(
      "127.0.0.1", std::to_string(port), net::use_awaitable);
  co_await tcp_s.async_connect(results, net::use_awaitable);

  websocket::stream<beast::tcp_stream> ws{std::move(tcp_s)};
  co_await ws.async_handshake("127.0.0.1", "/ws", net::use_awaitable);
  ws.text(true);

  co_return std::make_unique<beast_ws_client>(std::move(ws));
}

}  // namespace xpto::blot::tests
