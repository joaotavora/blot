#include "blot/assembly.hpp"

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/readable_pipe.hpp>
#include <boost/process/v2/process.hpp>
#include <boost/process/v2/stdio.hpp>
#include <boost/system/detail/error_code.hpp>
#include <filesystem>
#include <string>
#include <vector>

#include "blot/logger.hpp"

namespace xpto::blot {

namespace fs = std::filesystem;
namespace p2 = boost::process::v2;
namespace asio = boost::asio;

// Run the compiler with modified command to generate assembly
std::string get_asm(
    const fs::path& directory, const std::string& command,
    const fs::path& file) {
  // Modify the command to generate assembly with debugging info
  // Parse the original command to extract the compiler and its arguments
  std::istringstream iss(command);
  std::string compiler;
  iss >> compiler;

  std::vector<std::string> args;
  bool had_dash_c = false;

  for (std::string arg{}; iss >> arg;) {
    // Skip output specifiers (and the next argument, too)
    if (arg.substr(0, 2) == "-o") {
      iss >> arg;
      continue;
    } else if (arg.substr(0,2) == "-c") {
      arg = "-S";
      had_dash_c = true;
    }
    args.push_back(std::move(arg));
  }

  // Add -g1
  args.push_back("-g1");
  // Add -S
  if (!had_dash_c) {
    args.push_back("-S");
    args.push_back(file);
  }

  // Output to stdout
  args.push_back("-o");
  args.push_back("-");

  LOG_INFO("Running compiler {}:\n{}", compiler,
    [&](){
        std::string res{compiler};
        res.reserve(command.length());
        for (const auto& a : args) {res+= " "; res += a;}
        return res;
    }());

  // process(asio::any_io_executor, filesystem::path, range<string> args,
  // AdditionalInitializers...)
  asio::io_context ctx;
  asio::readable_pipe rp{ctx};
  std::string output{};
  p2::process proc{
    ctx, compiler, args,
    p2::process_stdio{.in = nullptr, .out = rp, .err = nullptr}};
  boost::system::error_code ec;
  asio::read(rp, asio::dynamic_buffer(output), ec);
  assert(!ec || (ec == asio::error::eof));
  proc.wait();
  return output;
}


}  // namespace xpto::blot
