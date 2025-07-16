#include "blot/assembly.hpp"

#include <fmt/std.h>
#include <re2/re2.h>

#define BOOST_PROCESS_USE_STD_FS 1

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/readable_pipe.hpp>
#include <boost/process/v2/process.hpp>
#include <boost/process/v2/start_dir.hpp>
#include <boost/process/v2/stdio.hpp>
#include <boost/system/detail/error_code.hpp>
#include <filesystem>
#include <string>
#include <vector>

#include "logger.hpp"

namespace xpto::blot {

namespace fs = std::filesystem;
namespace p2 = boost::process::v2;
namespace asio = boost::asio;

auto args_to_string(std::string res, std::vector<std::string>& args) {
  for (const auto& a : args) {
    res += " ";
    res += a;
  }
  return res;
}

std::string get_compiler_version(const std::string& compiler) {
  asio::io_context ctx;
  asio::readable_pipe rp_out{ctx};
  std::string output;

  p2::process proc{
    ctx,
    compiler,
    {"--version"},
    p2::process_stdio{.in = nullptr, .out = rp_out, .err = nullptr}};

  boost::system::error_code ec;
  asio::read(rp_out, asio::dynamic_buffer(output), ec);
  proc.wait();

  // Parse version from output using RE2
  static const RE2 gcc_re(R"((?:gcc|GCC)\)?\s*(\d+\.\d+\.\d+))");
  static const RE2 clang_re(R"(clang.*?(\d+\.\d+\.\d+))");

  std::string version;
  if (RE2::PartialMatch(output, gcc_re, &version) ||
      RE2::PartialMatch(output, clang_re, &version)) {
    return version;
  }

  return "<unknown>";
}

// Run the compiler with modified command to generate assembly
compilation_result get_asm(
    const fs::path& directory, const std::string& command,
    const fs::path& file) {
  // Modify the command to generate assembly with debugging info
  // Parse the original command to extract the compiler and its arguments
  std::istringstream iss(command);
  std::string compiler;
  iss >> compiler;

  std::vector<std::string> original_args;
  for (std::string arg{}; iss >> arg;) original_args.push_back(arg);

  std::string compiler_version = get_compiler_version(compiler);

  std::vector<std::string> args;
  bool had_dash_c = false;

  for (size_t i = 0; i < original_args.size(); ++i) {
    auto arg = original_args[i];
    // Skip output specifiers (and the next argument, too)
    if (arg.substr(0, 2) == "-o") {
      ++i;  // skip next argument too
      continue;
    } else if (arg.substr(0, 2) == "-c") {
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
  // Add -o - to output to stdout
  args.push_back("-o");
  args.push_back("-");

  LOG_INFO(
      "Running compiler {}:\n{}", compiler, args_to_string(compiler, args));
  LOG_DEBUG("Workdir {}:", directory);

  // process(asio::any_io_executor, filesystem::path, range<string> args,
  // AdditionalInitializers...)
  asio::io_context ctx;
  asio::readable_pipe rp_out{ctx};
  asio::readable_pipe rp_err{ctx};
  std::string output{};
  std::string error_output{};

  p2::process proc{
    ctx, compiler, args,
    p2::process_stdio{.in = nullptr, .out = rp_out, .err = rp_err},
    p2::process_start_dir{directory}};

  boost::system::error_code ec_out, ec_err;
  asio::read(rp_out, asio::dynamic_buffer(output), ec_out);
  asio::read(rp_err, asio::dynamic_buffer(error_output), ec_err);
  assert(!ec_out || (ec_out == asio::error::eof));
  assert(!ec_err || (ec_err == asio::error::eof));

  auto exit_code = proc.wait();
  if (exit_code != 0) {
    fmt::print(stderr, "{}", error_output);
    throw compilation_error{
      fmt::format("Compiler failed with exit code {}", exit_code),
      {compiler, args, directory, compiler_version},
      std::move(error_output)};
  }

  return {
    .assembly = output,
    .invocation = {compiler, args, directory, compiler_version}};
}

}  // namespace xpto::blot
