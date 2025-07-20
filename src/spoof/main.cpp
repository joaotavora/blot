#include <fmt/format.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define BOOST_PROCESS_USE_STD_FS 1

#include <boost/asio/io_context.hpp>
#include <boost/process/v2/process.hpp>
#include <boost/process/v2/start_dir.hpp>
#include <boost/process/v2/stdio.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace p2 = boost::process::v2;

int main(int argc, char* argv[]) {
  if (argc < 3) {
    fmt::print(stderr, "Usage: {} <filename> <command> [args...]\n", argv[0]);
    fmt::print(stderr, "Content to spoof is read from stdin\n");
    return 1;
  }

  std::string target_filename = argv[1];

  // Read content from stdin
  std::string spoof_content;
  for (std::string line; std::getline(std::cin, line);) {
    spoof_content += line + "\n";
  }

  // Create temporary directory structure for overlayfs
  fs::path temp_dir = fs::temp_directory_path() / "spoof_overlay";
  fs::create_directories(temp_dir);

  // Create the spoofed file in the overlay directory
  // Use the target filename as-is (relative to working directory)
  fs::path overlay_target = temp_dir / target_filename;
  fs::create_directories(overlay_target.parent_path());

  {
    std::ofstream ofs{overlay_target};
    ofs << spoof_content;
  }

  // Create mount point for overlay
  fs::path merged = fs::temp_directory_path() / "spoof_merged";
  fs::create_directories(merged);

  // Create user+mount namespace
  if (unshare(CLONE_NEWUSER | CLONE_NEWNS) != 0) {
    perror("unshare");
    return 1;
  }

  std::ofstream{"/proc/self/uid_map"} << fmt::format("0 {} 1\n", getuid());
  std::ofstream{"/proc/self/setgroups"} << "deny\n";
  std::ofstream{"/proc/self/gid_map"} << fmt::format("0 {} 1\n", getgid());

  // Mount overlayfs: overlay directory over current working directory
  std::string overlay_options = fmt::format(
      "lowerdir={}:{}", temp_dir.string(), fs::current_path().string());

  if (mount("overlay", merged.c_str(), "overlay", 0, overlay_options.c_str()) !=
      0) {
    perror("mount overlay");
    return 1;
  }
  std::vector<std::string> cmd_args;
  for (int i = 2; i < argc; ++i) {
    cmd_args.push_back(argv[i]);
  }

  try {
    boost::asio::io_context ctx;
    p2::process proc{
      ctx, cmd_args[0],
      std::vector<std::string>{cmd_args.begin() + 1, cmd_args.end()},
      p2::process_start_dir{merged}};

    int exit_code = proc.wait();

    // Cleanup
    umount(merged.c_str());
    fs::remove_all(temp_dir);
    fs::remove_all(merged);

    return exit_code;
  } catch (const std::exception& e) {
    fmt::print(stderr, "Error: {}\n", e.what());
    umount(merged.c_str());
    fs::remove_all(temp_dir);
    fs::remove_all(merged);
    return 1;
  }
}
