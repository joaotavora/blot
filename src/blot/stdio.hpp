#pragma once

#include <filesystem>

namespace xpto::blot {

// Start the JSONRPC stdio server.  Reads Content-Length-framed messages from
// stdin and writes responses to stdout.  Blocks until the client sends
// "shutdown" or stdin reaches EOF.  ccj_path must point to a valid
// compile_commands.json file.
void run_stdio_server(const std::filesystem::path& ccj_path);

}  // namespace xpto::blot
