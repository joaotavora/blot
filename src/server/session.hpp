#pragma once

#include <boost/json.hpp>
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
  std::string_view message;
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

class session;

/// Callable passed to handlers for emitting progress notifications.
/// Handlers call it as: send_progress("phase", "status") or
/// send_progress("phase", "status", elapsed_ms).  No JSONRPC knowledge needed.
struct progress_fn {
  void operator()(
      std::string_view phase, std::string_view status,
      std::optional<long long> ms = std::nullopt) const;

 private:
  friend class session;
  progress_fn(session* s, const json::value& id) : sess_{s}, id_{&id} {}
  session* sess_;
  const json::value* id_;
};

class session {
  friend struct progress_fn;

  const fs::path* ccj_path;
  const fs::path* project_root;
  mutable std::mutex cache_mutex;
  std::unordered_map<token_t, infer_entry> infer_cache_1;
  std::unordered_map<token_t, asm_entry> asm_cache_1;
  std::unordered_map<std::string, std::pair<int, asm_entry>> asm_cache_2;
  std::unordered_map<token_t, annotate_entry> annotate_cache_1;

  void reply_(const json::value& id, const jsonrpc_response_t& res);
  void send_progress_(
      const json::value& id, std::string_view phase, std::string_view status,
      std::optional<long long> elapsed_ms = std::nullopt);

  jsonrpc_response_t handle_initialize(
      const json::object& params, const progress_fn& send_progress);
  jsonrpc_response_t handle_infer(
      const json::object& params, const progress_fn& send_progress);
  jsonrpc_response_t handle_grabasm(
      const json::object& params, const progress_fn& send_progress);
  jsonrpc_response_t handle_annotate(
      const json::object& params, const progress_fn& send_progress);

 public:
  session(const session&) = delete;
  session(session&&) = delete;
  session& operator=(const session&) = delete;
  session& operator=(session&&) = delete;
  virtual ~session() = default;

  session(const fs::path& ccj_path, const fs::path& project_root);

  virtual void send(const json::object& msg) = 0;

  bool handle_frame(std::string_view text);
};

}  // namespace xpto::blot
