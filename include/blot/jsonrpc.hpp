// SPDX-License-Identifier: MIT
#pragma once

#include <boost/asio/awaitable.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/json.hpp>
#include <optional>
#include <string>

namespace xpto::blot {

// Read a JSONRPC 2.0 message from a stream using Content-Length framing
// Returns nullopt on EOF/error
boost::asio::awaitable<std::optional<std::string>>
read_jsonrpc_message(boost::asio::posix::stream_descriptor& stream);

// Write a JSONRPC 2.0 message to a stream using Content-Length framing
boost::asio::awaitable<void>
write_jsonrpc_message(boost::asio::posix::stream_descriptor& stream,
                      const boost::json::object& msg);

}  // namespace xpto::blot
