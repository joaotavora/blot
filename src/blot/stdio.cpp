#include "stdio.hpp"

#include <boost/json.hpp>
#include <filesystem>
#include <iostream>
#include <string>

#include "logger.hpp"
#include "session.hpp"

namespace json = boost::json;

namespace xpto::blot {

namespace fs = std::filesystem;

/// Session

struct stdio_session : session<stdio_session> {
  stdio_session(const fs::path& ccj_path, const fs::path& project_root)
      : session{ccj_path, project_root} {}

  void send(const json::object& msg) {
    auto text = json::serialize(msg);
    std::cout << "Content-Length: " << text.size() << "\r\n\r\n" << text;
    std::cout.flush();
  }
};

/// Server loop

void run_stdio_server(const fs::path& ccj_path) {
  fs::path project_root = fs::absolute(ccj_path).parent_path();

  LOG_INFO("blot --stdio: project root: {}", project_root.string());
  LOG_INFO("blot --stdio: ccj          : {}", ccj_path.string());

  stdio_session sess{ccj_path, project_root};

  for (;;) {
    // Read Content-Length header block (same framing as LSP)
    size_t content_length{0};
    std::string line;
    while (std::getline(std::cin, line)) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (line.empty()) break;  // blank line ends headers
      if (line.starts_with("Content-Length: "))
        content_length = std::stoul(line.substr(16));
    }
    if (!std::cin || content_length == 0) break;

    std::string body(content_length, '\0');
    if (!std::cin.read(
            body.data(), static_cast<std::streamsize>(content_length)))
      break;

    if (!sess.handle_frame(body)) break;
  }

  LOG_INFO("stdio session ended");
}

}  // namespace xpto::blot
