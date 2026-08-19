#define BOOST_ASIO_NO_DEPRECATED
#include <fmt/core.h>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/json.hpp>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>

#include "auto.hpp"
#include "logger.hpp"
#include "session.hpp"
#include "web-dispatch.hpp"
#include "web.hpp"

namespace beast = boost::beast;
namespace http = beast::http;
namespace json = boost::json;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace fs = std::filesystem;
using tcp = net::ip::tcp;

namespace xpto::blot {

/// ws_session — WebSocket transport; hidden in this TU

struct ws_session : session {
  using stream_t = beast::websocket::stream<beast::tcp_stream>;
  stream_t ws;
  std::mutex write_mutex;
  std::atomic<bool> shutdown_requested{false};
  // Guards cancel_sig: boost::asio::cancellation_signal forbids concurrent
  // slot connection and emit(), which can otherwise happen from different
  // thread-pool threads here.
  std::mutex cancel_mutex;
  net::cancellation_signal cancel_sig;

  ws_session(stream_t ws, fs::path ccj_path, fs::path project_root)
      : session{std::move(ccj_path), std::move(project_root)},
        ws{std::move(ws)} {
    this->ws.text(true);
  }

  void send(const json::object& msg) override {
    auto text = json::serialize(msg);  // serialize before lock
    LOG_DEBUG("ws <- {}", text);
    std::lock_guard lk{write_mutex};
    beast::error_code ec{};
    ws.write(net::buffer(text), ec);
    if (ec) LOG_INFO("ws_send error: {}", ec.message());
  }

  net::awaitable<std::string> read_frame() {
    beast::flat_buffer buf{};
    co_await ws.async_read(buf, net::use_awaitable);
    co_return beast::buffers_to_string(buf.data());
  }
};

/// Session coroutines

net::awaitable<void> process_frame(
    std::shared_ptr<ws_session> sess, std::string text) {
  auto state = co_await net::this_coro::cancellation_state;
  if (state.cancelled() != net::cancellation_type::none) {
    // The owning run_session loop has already ended (e.g. the client
    // disconnected); don't bother compiling/annotating for a dead session.
    LOG_DEBUG("dropping frame on closed session: {}", text);
    co_return;
  }
  LOG_DEBUG("ws -> {}", text);
  if (!sess->handle_frame(text))
    sess->shutdown_requested.store(true, std::memory_order_relaxed);
}

net::awaitable<void> run_session(std::shared_ptr<ws_session> sess) {
  // Tell any process_frame tasks still queued/running when this loop ends
  // (break or exception, e.g. a read error from client disconnect) to skip
  // their work. Note this can't interrupt work already in progress inside
  // handle_frame, which has no cancellation checkpoints of its own — it
  // only prevents *not-yet-started* frames from doing wasted work.
  AUTO({
    std::lock_guard lk{sess->cancel_mutex};
    sess->cancel_sig.emit(net::cancellation_type::terminal);
  });
  auto ex = co_await net::this_coro::executor;
  for (;;) {
    // FIXME: shutdown_requested is never observed while suspended in
    // read_frame() — shutdown only takes effect after the next frame arrives.
    // Consider using async_close + catching the resulting error instead.
    if (sess->shutdown_requested.load(std::memory_order_relaxed)) break;
    auto text = co_await sess->read_frame();
    net::post(
        ex, [text = std::move(text), sess, ex] () mutable {
          net::cancellation_slot slot;
          {
            std::lock_guard lk{sess->cancel_mutex};
            slot = sess->cancel_sig.slot();
          }
          // sess is captured by value (shared_ptr) so the session outlives
          // this detached task even if run_session's loop has since ended.
          net::co_spawn(
              ex, process_frame(std::move(sess), std::move(text)),
              net::bind_cancellation_slot(slot, net::detached));
        });
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
      auto sess =
          std::make_shared<ws_session>(std::move(ws), ccj_path, project_root);
      co_await run_session(std::move(sess));
      co_return;
    }

    response_t res = dispatch(req, ccj_path, project_root);
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
    const net::any_io_executor& ex, const fs::path& ccj_path,
    const fs::path& project_root, int port) {
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
