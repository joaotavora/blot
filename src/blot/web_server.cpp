#include "web_server.hpp"

#include <fmt/core.h>

#include <atomic>
#include <boost/asio/ip/tcp.hpp>
#include <filesystem>
#include <iostream>
#include <thread>
#include <vector>

#include "../libblot/logger.hpp"

namespace net = boost::asio;
using tcp = net::ip::tcp;

namespace xpto::blot {

namespace fs = std::filesystem;

void run_web_server(const fs::path& ccj_path, int port) {
  fs::path project_root = fs::absolute(ccj_path).parent_path();

  fmt::println("blot --web: listening on http://localhost:{}", port);
  fmt::println("  project root : {}", project_root.string());
  fmt::println("  ccj          : {}", ccj_path.string());
  fmt::println("  press Ctrl-C to stop");
  std::cout.flush();

  net::io_context ioc;
  tcp::acceptor acceptor{
    ioc, tcp::endpoint{tcp::v4(), static_cast<unsigned short>(port)}};
  acceptor.set_option(net::socket_base::reuse_address{true});

  // Cap the live thread count to avoid unbounded growth.
  constexpr int kMaxThreads{4};
  std::atomic<int> active{0};
  std::vector<std::thread> threads;
  threads.reserve(64);

  for (;;) {
    tcp::socket socket{ioc};
    boost::system::error_code ec;
    acceptor.accept(socket, ec);
    if (ec) break;  // acceptor was closed (e.g. signal)

    // Simple back-pressure: spin until a slot opens.
    while (active.load() >= kMaxThreads) {
      std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }

    boost::system::error_code ec2;
    auto remote = socket.remote_endpoint(ec2);
    LOG_INFO(
        "connection from {}:{}", ec2 ? "?" : remote.address().to_string(),
        ec2 ? 0 : remote.port());
    ++active;
    // Transfer socket ownership into the thread via native handle.
    int fd = socket.release();
    threads.emplace_back([fd, &ccj_path, &project_root, &active]() {
      handle_connection(fd, ccj_path, project_root);
      --active;
    });
  }

  for (auto& t : threads) {
    if (t.joinable()) t.join();
  }
}

}  // namespace xpto::blot
