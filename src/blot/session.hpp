#pragma once

#include <boost/json.hpp>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "blot/assembly.hpp"
#include "blot/ccj.hpp"

namespace json = boost::json;

namespace xpto::blot {

namespace fs = std::filesystem;

using token_t = int64_t;

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
  const fs::path* ccj_path;
  const fs::path* project_root;
  mutable std::mutex cache_mutex;
  std::unordered_map<token_t, infer_entry> infer_cache_1;
  std::unordered_map<token_t, asm_entry> asm_cache_1;
  std::unordered_map<std::string, std::pair<int, asm_entry>> asm_cache_2;
  std::unordered_map<token_t, annotate_entry> annotate_cache_1;

 public:
  session(const session&) = delete;
  session(session&&) = delete;
  session& operator=(const session&) = delete;
  session& operator=(session&&) = delete;
  virtual ~session() = default;

  session(const fs::path& ccj_path, const fs::path& project_root);

  virtual void send(const json::object& msg) = 0;

  json::object handle_initialize(
      const json::value& id, const json::object& params);
  json::object handle_infer(const json::value& id, const json::object& params);
  json::object handle_grabasm(
      const json::value& id, const json::object& params);
  json::object handle_annotate(
      const json::value& id, const json::object& params);
  bool handle_frame(std::string_view text);
};

}  // namespace xpto::blot
