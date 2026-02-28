#pragma once

#include <boost/asio/io_context.hpp>
#include <filesystem>

namespace xpto::blot {

// Set up the HTTP server: bind to port, co_spawn the accept loop.
// Returns the actual bound port (useful when port=0 for auto-assignment).
// Does NOT call ioc.run() — the caller is responsible for that.
int run_web_server(
    boost::asio::io_context& ioc, const std::filesystem::path& ccj_path,
    int port);

}  // namespace xpto::blot
