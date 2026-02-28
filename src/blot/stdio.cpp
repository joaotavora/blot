#include "stdio.hpp"

#define BOOST_ASIO_NO_DEPRECATED
#include <unistd.h>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/json.hpp>
#include <filesystem>
#include <iostream>
#include <string>

#include "logger.hpp"
#include "session.hpp"

namespace json = boost::json;
namespace net = boost::asio;

namespace xpto::blot {

namespace fs = std::filesystem;

struct stdio_session : session<stdio_session> {
  stdio_session(const fs::path& ccj_path, const fs::path& project_root)
      : session{ccj_path, project_root} {}

  void send(const json::object& msg) {
    auto text = json::serialize(msg);
    std::cout << "Content-Length: " << text.size() << "\r\n\r\n" << text;
    std::cout.flush();
  }
};

static net::awaitable<void> stdio_loop(
    net::posix::stream_descriptor* input, fs::path ccj_path,
    fs::path project_root) {
  stdio_session sess{ccj_path, project_root};
  net::streambuf buf;
  try {
    for (;;) {
      size_t content_length{0};

      // Read headers until blank line.
      for (;;) {
        std::size_t n = co_await net::async_read_until(
            *input, buf, "\r\n", net::use_awaitable);
        std::string line{
          net::buffers_begin(buf.data()),
          net::buffers_begin(buf.data()) + static_cast<std::ptrdiff_t>(n)};
        buf.consume(n);
        if (line.size() >= 2) line.resize(line.size() - 2);  // strip \r\n
        if (line.empty()) break;
        if (line.starts_with("Content-Length: "))
          content_length = std::stoul(line.substr(16));
      }
      if (content_length == 0) break;

      // Read body (may already be partially in buf from previous read).
      if (buf.size() < content_length)
        co_await net::async_read(
            *input, buf, net::transfer_exactly(content_length - buf.size()),
            net::use_awaitable);
      std::string body{
        net::buffers_begin(buf.data()),
        net::buffers_begin(buf.data()) +
            static_cast<std::ptrdiff_t>(content_length)};
      buf.consume(content_length);

      if (!sess.handle_frame(body)) break;
    }
  } catch (const boost::system::system_error&) {
  }
  LOG_INFO("stdio session ended");
}

void run_stdio_server(net::io_context& ioc, const fs::path& ccj_path) {
  fs::path project_root = fs::absolute(ccj_path).parent_path();

  LOG_INFO("blot --stdio: project root: {}", project_root.string());
  LOG_INFO("blot --stdio: ccj          : {}", ccj_path.string());

  net::posix::stream_descriptor input{ioc, ::dup(STDIN_FILENO)};
  net::co_spawn(ioc, stdio_loop(&input, ccj_path, project_root), net::detached);
}

}  // namespace xpto::blot
