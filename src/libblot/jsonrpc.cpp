// SPDX-License-Identifier: MIT
#include "blot/jsonrpc.hpp"

#include <fmt/format.h>
#include <re2/re2.h>

#include <boost/asio/buffer.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/write.hpp>
#include <boost/system/error_code.hpp>
#include <istream>
#include <stdexcept>

namespace xpto::blot {

namespace asio = boost::asio;
namespace sys = boost::system;

asio::awaitable<std::optional<std::string>> read_jsonrpc_message(
    asio::posix::stream_descriptor& stream) {
  try {
    // Read headers until \r\n\r\n
    asio::streambuf buf;
    co_await asio::async_read_until(
        stream, buf, "\r\n\r\n", asio::use_awaitable);

    // Parse Content-Length header
    std::istream is(&buf);
    std::string header_line;
    int content_length{-1};

    while (std::getline(is, header_line) && header_line != "\r") {
      static const RE2 content_length_re{R"(Content-Length:\s*(\d+))"};
      int length;
      if (RE2::PartialMatch(header_line, content_length_re, &length)) {
        content_length = length;
      }
    }

    if (content_length < 0) {
      throw std::runtime_error{"Missing Content-Length header"};
    }

    // Read exact content bytes
    std::string content;
    content.resize(content_length);

    // First, consume any already-buffered data
    std::size_t buffered{buf.size()};
    if (buffered > 0) {
      std::size_t to_copy{
        std::min(buffered, static_cast<std::size_t>(content_length))};
      std::istream(&buf).read(content.data(), to_copy);
      buffered = to_copy;
    }

    // Then read remaining bytes
    if (buffered < static_cast<std::size_t>(content_length)) {
      co_await asio::async_read(
          stream,
          asio::buffer(content.data() + buffered, content_length - buffered),
          asio::use_awaitable);
    }

    co_return content;

  } catch (const sys::system_error& e) {
    if (e.code() == asio::error::eof) {
      co_return std::nullopt;
    }
    throw;
  }
}

asio::awaitable<void> write_jsonrpc_message(
    asio::posix::stream_descriptor& stream, const boost::json::object& msg) {
  std::string json_str{boost::json::serialize(msg)};
  std::string full_msg{
    fmt::format("Content-Length: {}\r\n\r\n{}", json_str.size(), json_str)};

  co_await asio::async_write(
      stream, asio::buffer(full_msg), asio::use_awaitable);
}

}  // namespace xpto::blot
