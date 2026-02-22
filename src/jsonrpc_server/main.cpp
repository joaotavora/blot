// SPDX-License-Identifier: MIT
#include "blot/assembly.hpp"
#include "blot/blot.hpp"
#include "blot/jsonrpc.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/json.hpp>
#include <cstdio>
#include <exception>
#include <iostream>
#include <span>
#include <string>
#include <unistd.h>

namespace asio = boost::asio;
namespace json = boost::json;

namespace {

// Convert blot::annotation_options from JSON
xpto::blot::annotation_options
parse_annotation_options(const json::object& params) {
  xpto::blot::annotation_options opts;

  if (params.contains("preserve_library_functions")) {
    opts.preserve_library_functions =
        params.at("preserve_library_functions").as_bool();
  }
  if (params.contains("preserve_directives")) {
    opts.preserve_directives = params.at("preserve_directives").as_bool();
  }
  if (params.contains("preserve_comments")) {
    opts.preserve_comments = params.at("preserve_comments").as_bool();
  }
  if (params.contains("preserve_unused_labels")) {
    opts.preserve_unused_labels =
        params.at("preserve_unused_labels").as_bool();
  }
  if (params.contains("demangle")) {
    opts.demangle = params.at("demangle").as_bool();
  }

  return opts;
}

// Convert annotation_result to JSON
json::object serialize_result(const xpto::blot::annotation_result& result) {
  json::array assembly;
  for (const auto& line : result.output) {
    assembly.push_back(json::string{line});
  }

  json::array mappings;
  for (const auto& [src_line, asm_start, asm_end] : result.linemap) {
    json::object mapping;
    mapping["source_line"] = src_line;
    mapping["assembly_start"] = asm_start;
    mapping["assembly_end"] = asm_end;
    mappings.push_back(std::move(mapping));
  }

  json::object obj;
  obj["assembly"] = std::move(assembly);
  obj["line_mappings"] = std::move(mappings);
  return obj;
}

// JSONRPC error codes
constexpr int PARSE_ERROR{-32700};
constexpr int INVALID_REQUEST{-32600};
constexpr int METHOD_NOT_FOUND{-32601};
constexpr int INVALID_PARAMS{-32602};
constexpr int INTERNAL_ERROR{-32603};

json::object make_error_response(const json::value& id, int code,
                                  std::string message,
                                  std::string data = {}) {
  json::object error;
  error["code"] = code;
  error["message"] = std::move(message);
  if (!data.empty()) {
    error["data"] = std::move(data);
  }

  json::object response;
  response["jsonrpc"] = "2.0";
  response["id"] = id;
  response["error"] = std::move(error);
  return response;
}

json::object make_result_response(const json::value& id, json::value result) {
  json::object response;
  response["jsonrpc"] = "2.0";
  response["id"] = id;
  response["result"] = std::move(result);
  return response;
}

// Handle initialize method
json::object handle_initialize(const json::value& id,
                                [[maybe_unused]] const json::object& params) {
  json::object capabilities;
  // Add capability flags here as needed

  json::object server_info;
  server_info["name"] = "blot-jsonrpc";
  server_info["version"] = "0.1.0";

  json::object result;
  result["capabilities"] = std::move(capabilities);
  result["serverInfo"] = std::move(server_info);

  return make_result_response(id, std::move(result));
}

// Handle shutdown method
json::object handle_shutdown(const json::value& id,
                              [[maybe_unused]] const json::object& params,
                              bool& should_shutdown) {
  should_shutdown = true;
  return make_result_response(id, nullptr);
}

// Handle blot/annotate method
json::object handle_annotate(const json::value& id,
                              const json::object& params) {
  try {
    // Extract assembly input
    if (!params.contains("assembly")) {
      return make_error_response(id, INVALID_PARAMS,
                                 "Missing 'assembly' parameter");
    }

    std::string assembly_input;
    const auto& assembly_value{params.at("assembly")};

    if (assembly_value.is_string()) {
      assembly_input = assembly_value.as_string().c_str();
    } else if (assembly_value.is_array()) {
      const auto& array{assembly_value.as_array()};
      for (const auto& line : array) {
        if (!line.is_string()) {
          return make_error_response(
              id, INVALID_PARAMS,
              "Assembly array must contain only strings");
        }
        assembly_input += line.as_string().c_str();
        assembly_input += '\n';
      }
    } else {
      return make_error_response(
          id, INVALID_PARAMS,
          "Assembly parameter must be string or array of strings");
    }

    // Parse options
    xpto::blot::annotation_options opts;
    if (params.contains("options")) {
      if (!params.at("options").is_object()) {
        return make_error_response(id, INVALID_PARAMS,
                                   "Options must be an object");
      }
      opts = parse_annotation_options(params.at("options").as_object());
    }

    // Call blot::annotate (keeping assembly_input alive!)
    auto result{xpto::blot::annotate(
        std::span<const char>{assembly_input.data(), assembly_input.size()},
        opts)};

    // Serialize result (copies string_views to JSON before assembly_input
    // is destroyed)
    json::object response_result{serialize_result(result)};

    return make_result_response(id, std::move(response_result));

  } catch (const std::exception& e) {
    return make_error_response(id, INTERNAL_ERROR, "Internal error",
                                e.what());
  }
}

// Dispatch request to appropriate handler
json::object dispatch_request(const json::object& request,
                               bool& should_shutdown) {
  // Extract ID (can be null for notifications)
  json::value id{nullptr};
  if (request.contains("id")) {
    id = request.at("id");
  }

  // Check for required fields
  if (!request.contains("method") || !request.at("method").is_string()) {
    return make_error_response(id, INVALID_REQUEST, "Missing method field");
  }

  std::string method{request.at("method").as_string().c_str()};

  // Extract params (optional)
  json::object params;
  if (request.contains("params")) {
    if (!request.at("params").is_object()) {
      return make_error_response(id, INVALID_PARAMS,
                                 "Params must be an object");
    }
    params = request.at("params").as_object();
  }

  // Route to handler
  if (method == "initialize") {
    return handle_initialize(id, params);
  } else if (method == "shutdown") {
    return handle_shutdown(id, params, should_shutdown);
  } else if (method == "exit") {
    // Exit is a notification - no response
    should_shutdown = true;
    return {};  // Empty response
  } else if (method == "blot/annotate") {
    return handle_annotate(id, params);
  } else {
    return make_error_response(id, METHOD_NOT_FOUND, "Method not found",
                                method);
  }
}

asio::awaitable<void> server_loop() {
  auto executor{co_await asio::this_coro::executor};

  // Duplicate stdin/stdout to allow async operations
  asio::posix::stream_descriptor stdin_stream{executor, ::dup(STDIN_FILENO)};
  asio::posix::stream_descriptor stdout_stream{executor,
                                                ::dup(STDOUT_FILENO)};

  bool should_shutdown{false};

  while (!should_shutdown) {
    auto msg{co_await xpto::blot::read_jsonrpc_message(stdin_stream)};
    if (!msg) {
      break;  // EOF
    }

    json::object response;
    bool has_response{true};

    try {
      json::value parsed{json::parse(*msg)};
      if (!parsed.is_object()) {
        response = make_error_response(nullptr, INVALID_REQUEST,
                                        "Request must be an object");
      } else {
        json::object request{parsed.as_object()};
        response = dispatch_request(request, should_shutdown);
        // Check if we should send a response (empty for notifications)
        has_response = !response.empty();
      }
    } catch (const std::exception& e) {
      response = make_error_response(nullptr, PARSE_ERROR, "Parse error",
                                      e.what());
      has_response = true;
    }

    if (has_response) {
      co_await xpto::blot::write_jsonrpc_message(stdout_stream, response);
    }
  }
}

}  // namespace

int main() {
  try {
    // Redirect stderr for logging (stdout is for JSONRPC)
    std::freopen("/dev/null", "w", stderr);

    asio::io_context ctx;
    asio::co_spawn(ctx, server_loop(), asio::detached);
    ctx.run();

    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Fatal error: " << e.what() << '\n';
    return 1;
  }
}
