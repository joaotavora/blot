// SPDX-License-Identifier: MIT
#pragma once

/**
 * @file jsonrpc.hpp
 * @brief JSONRPC 2.0 message framing over async byte streams.
 *
 * Messages are framed using the same Content-Length header convention as
 * the Language Server Protocol: each message is preceded by a header block
 * of the form @c "Content-Length: N\r\n\r\n" followed by exactly @c N bytes
 * of UTF-8 JSON text.  Both functions operate on Boost.ASIO
 * @c posix::stream_descriptor objects and are designed to be used with
 * @c co_await in a coroutine context.
 */

#include <boost/asio/awaitable.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/json.hpp>
#include <optional>
#include <string>

namespace xpto::blot {

/** @brief Read one framed JSONRPC message from @p stream.
 *
 * Reads and parses the @c Content-Length header, then reads exactly that
 * many bytes of message body.  Returns the raw JSON text as a string, or
 * an empty optional if the stream reaches EOF before a complete message
 * is received.
 */
boost::asio::awaitable<std::optional<std::string>> read_jsonrpc_message(
    boost::asio::posix::stream_descriptor& stream);

/** @brief Write one framed JSONRPC message to @p stream.
 *
 * Serialises @p msg to JSON, prepends the appropriate
 * @c Content-Length header, and writes the complete frame to @p stream
 * as a single async operation.
 */
boost::asio::awaitable<void> write_jsonrpc_message(
    boost::asio::posix::stream_descriptor& stream,
    const boost::json::object& msg);

}  // namespace xpto::blot
