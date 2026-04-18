#pragma once

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/io_context.hpp>
#include <filesystem>

namespace xpto::blot {

// Set up the HTTP server: bind to port, co_spawn the accept loop.
// Returns the actual bound port (useful when port=0 for auto-assignment).
int run_web_server(
    const boost::asio::any_io_executor& ex,
    const std::filesystem::path& ccj_path,
    const std::filesystem::path& project_root, int port);

}  // namespace xpto::blot
