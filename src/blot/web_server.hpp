#pragma once

#include <filesystem>

namespace xpto::blot {

// Handle a single Beast HTTP connection. Declared here so web_handlers.cpp
// can provide the implementation that the server loop calls.
void handle_connection(
    int socket_fd, const std::filesystem::path& ccj_path,
    const std::filesystem::path& project_root);

// Start the HTTP server. Blocks until the process receives SIGINT/SIGTERM.
// ccj_path must point to a valid compile_commands.json file.
void run_web_server(const std::filesystem::path& ccj_path, int port);

}  // namespace xpto::blot
