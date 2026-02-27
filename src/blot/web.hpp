#pragma once

#include <filesystem>

namespace xpto::blot {

// Start the HTTP server. Blocks until the process receives SIGINT/SIGTERM.
// ccj_path must point to a valid compile_commands.json file.
void run_web_server(const std::filesystem::path& ccj_path, int port);

}  // namespace xpto::blot
