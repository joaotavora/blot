#pragma once

#include <boost/asio/any_io_executor.hpp>
#include <filesystem>

namespace xpto::blot {

// Set up the HTTP server: bind to port, co_spawn the accept loop.
// Returns the actual bound port (useful when port=0 for auto-assignment).
// Does NOT run the executor — the caller is responsible for that.
int run_web_server(
    const boost::asio::any_io_executor& ex,
    const std::filesystem::path& ccj_path, int port);

}  // namespace xpto::blot
