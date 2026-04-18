#pragma once

#include <boost/json.hpp>
#include <concepts>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>

#include "blot/assembly.hpp"

namespace json = boost::json;

namespace xpto::blot {

namespace fs = std::filesystem;

using token_t = int64_t;

struct error {
  int code;
  std::string message;
  std::optional<json::object> data{};
};
using jsonrpc_response_t = std::variant<json::object, error>;

struct infer_entry {
  compile_command cmd;
};

struct asm_entry {
  compilation_result result;
};

struct annotate_entry {
  json::object annotated;
};

class session {
  fs::path ccj_path;
  fs::path project_root;
  mutable std::mutex cache_mutex;
  // FIXME: all four caches are unbounded — no eviction or capacity cap.
  // Long-running sessions or projects with many TUs will grow without limit.
  std::unordered_map<token_t, infer_entry> infer_cache_1;
  std::unordered_map<token_t, asm_entry> asm_cache_1;
  std::unordered_map<std::string, std::pair<int, asm_entry>> asm_cache_2;
  std::unordered_map<token_t, annotate_entry> annotate_cache_1;

  void reply_(const json::value& id, const jsonrpc_response_t& res);
  void send_progress_(
      const json::value& id, std::string_view phase, std::string_view status,
      std::optional<long long> elapsed_ms = std::nullopt);

  jsonrpc_response_t handle_initialize(
      const json::object& params,
      std::invocable<std::string_view, std::string_view> auto&& send_progress);
  jsonrpc_response_t handle_infer(
      const json::object& params,
      std::invocable<std::string_view, std::string_view> auto&& send_progress);
  jsonrpc_response_t handle_grabasm(
      const json::object& params,
      std::invocable<std::string_view, std::string_view> auto&& send_progress);
  jsonrpc_response_t handle_annotate(
      const json::object& params,
      std::invocable<std::string_view, std::string_view> auto&& send_progress);

 public:
  session(const session&) = delete;
  session(session&&) = delete;
  session& operator=(const session&) = delete;
  session& operator=(session&&) = delete;
  virtual ~session() = default;

  session(fs::path ccj_path, fs::path project_root);

  virtual void send(const json::object& msg) = 0;

  bool handle_frame(std::string_view text);
};

namespace testing {
/// Peak number of handle_grabasm invocations that were simultaneously
/// in flight since the last reset.  Used by concurrency tests.
int grabasm_max_concurrent();
void reset_grabasm_max_concurrent();
}  // namespace testing

}  // namespace xpto::blot
