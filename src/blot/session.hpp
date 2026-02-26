#pragma once

#include <boost/json.hpp>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "../libblot/json_helpers.hpp"
#include "../libblot/linespan.hpp"
#include "blot/assembly.hpp"
#include "blot/blot.hpp"
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

/// Free helpers

inline json::object make_result(const json::value& id, json::object result) {
  json::object msg{};
  msg["jsonrpc"] = "2.0";
  msg["id"] = id;
  msg["result"] = std::move(result);
  return msg;
}

inline json::object make_jsonrpc_error(
    const json::value& id, int code, std::string_view message,
    std::optional<json::object> data = std::nullopt) {
  json::object err{};
  err["code"] = code;
  err["message"] = message;
  if (data) err["data"] = *data;
  json::object msg{};
  msg["jsonrpc"] = "2.0";
  msg["id"] = id;
  msg["error"] = std::move(err);
  return msg;
}

/// CRTP session base

template <typename Derived>
class session {
  using clock_t = std::chrono::steady_clock;

  static long long duration_ms(clock_t::time_point t0) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               clock_t::now() - t0)
        .count();
  }

  static token_t next_token() {
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
    static std::atomic<int> counter{0};
    return ++counter;
  }

  static annotation_options parse_aopts(const json::object* opts) {
    annotation_options aopts{};
    if (!opts) return aopts;
    auto get_bool = [&](std::string_view key, bool& dst) {
      if (opts->contains(key)) {
        if (auto* b = opts->at(key).if_bool()) dst = *b;
      }
    };
    get_bool("demangle", aopts.demangle);
    get_bool("preserve_directives", aopts.preserve_directives);
    get_bool("preserve_comments", aopts.preserve_comments);
    get_bool("preserve_library_functions", aopts.preserve_library_functions);
    get_bool("preserve_unused_labels", aopts.preserve_unused_labels);
    return aopts;
  }

  void reply(json::object msg) { static_cast<Derived*>(this)->send(msg); }

  void send_progress(
      const json::value& request_id, std::string_view phase,
      std::string_view status,
      std::optional<long long> elapsed_ms = std::nullopt) {
    json::object params{};
    params["request_id"] = request_id;
    params["phase"] = phase;
    params["status"] = status;
    if (elapsed_ms) params["elapsed_ms"] = *elapsed_ms;
    json::object msg{};
    msg["jsonrpc"] = "2.0";
    msg["method"] = "blot/progress";
    msg["params"] = std::move(params);
    reply(std::move(msg));
  }

  const fs::path* ccj_path;
  const fs::path* project_root;
  std::unordered_map<token_t, infer_entry> infer_cache_1;
  std::unordered_map<token_t, asm_entry> asm_cache_1;
  std::unordered_map<std::string, std::pair<int, asm_entry>> asm_cache_2;
  std::unordered_map<token_t, annotate_entry> annotate_cache_1;

 public:
  session(const session&) = delete;
  session(session&&) = delete;
  session& operator=(const session&) = delete;
  session& operator=(session&&) = delete;
  ~session() = default;

  session(const fs::path& ccj_path, const fs::path& project_root)
      : ccj_path{&ccj_path}, project_root{&project_root} {}

  /// Handlers

  void handle_initialize(
      const json::value& id, const json::object& /*params*/) {
    json::object result{};
    json::object server_info{};
    server_info["name"] = "blot";
    server_info["version"] = "0.1";
    result["serverInfo"] = std::move(server_info);
    result["ccj"] = ccj_path->string();
    result["project_root"] = project_root->string();
    reply(make_result(id, std::move(result)));
  }

  void handle_infer(const json::value& id, const json::object& params) {
    token_t tok{};
    if (params.contains("token")) {
      tok = params.at("token").as_int64();
      if (auto it = infer_cache_1.find(tok); it != infer_cache_1.end()) {
        send_progress(id, "infer", "cached", 0);
        json::object result{};
        result["token"] = tok;
        result["cached"] = "token";
        json::object inf{};
        inf["annotation_target"] = it->second.cmd.file.string();
        inf["compilation_command"] = it->second.cmd.command;
        inf["compilation_directory"] = it->second.cmd.directory.string();
        result["inference"] = std::move(inf);
        return reply(make_result(id, std::move(result)));
      }
      return reply(
          make_jsonrpc_error(id, -32602, "token not found in infer cache"));
    } else {
      tok = next_token();
    }

    if (!params.contains("file"))
      return reply(make_jsonrpc_error(id, -32602, "missing 'file' or 'token'"));

    std::string file_str{params.at("file").as_string()};

    std::error_code ec{};
    auto abs_file = fs::weakly_canonical(*project_root / file_str, ec);
    if (ec || abs_file.string().find(project_root->string()) != 0)
      return reply(make_jsonrpc_error(id, -32602, "path traversal denied"));

    send_progress(id, "infer", "running");
    auto t0 = clock_t::now();

    std::optional<compile_command> cmd{};
    try {
      cmd = infer(*ccj_path, abs_file);
    } catch (std::exception& e) {
      auto ms = duration_ms(t0);
      send_progress(id, "infer", "error", ms);
      json::object data{};
      data["dribble"] = e.what();
      return reply(
          make_jsonrpc_error(id, -32603, "infer() threw", std::move(data)));
    }

    auto ms = duration_ms(t0);

    if (!cmd) {
      send_progress(id, "infer", "error", ms);
      return reply(
          make_jsonrpc_error(id, -32602, "no CCJ entry found for file"));
    }

    send_progress(id, "infer", "done", ms);

    infer_cache_1[tok] = infer_entry{*cmd};

    json::object result{};
    result["token"] = tok;
    result["cached"] = false;
    json::object inf{};
    inf["annotation_target"] = cmd->file.string();
    inf["compilation_command"] = cmd->command;
    inf["compilation_directory"] = cmd->directory.string();
    result["inference"] = std::move(inf);
    reply(make_result(id, std::move(result)));
  }

  void handle_grabasm(const json::value& id, const json::object& params) {
    std::optional<compile_command> cmd_opt{};

    token_t tok{};
    if (params.contains("token")) {
      tok = params.at("token").as_int64();

      // Check asm_cache_1 first (exact token from a previous grab_asm result)
      if (auto it = asm_cache_1.find(tok); it != asm_cache_1.end()) {
        send_progress(id, "grabasm", "cached", 0);
        json::object result{};
        result["token"] = tok;
        result["cached"] = "token";
        json::object cc{};
        cc["compiler"] = it->second.result.invocation.compiler;
        cc["compiler_version"] = it->second.result.invocation.compiler_version;
        result["compilation_command"] = std::move(cc);
        return reply(make_result(id, std::move(result)));
      }

      // Fall back to the previous phase
      if (auto it = infer_cache_1.find(tok); it != infer_cache_1.end()) {
        cmd_opt = it->second.cmd;
      } else {
        return reply(
            make_jsonrpc_error(id, -32602, "token not found in infer cache"));
      }
    } else if (params.contains("inference")) {
      auto& inf = params.at("inference").as_object();
      compile_command cc{};
      cc.command = std::string{inf.at("compilation_command").as_string()};
      cc.directory =
          fs::path{std::string{inf.at("compilation_directory").as_string()}};
      if (inf.contains("annotation_target"))
        cc.file =
            fs::path{std::string{inf.at("annotation_target").as_string()}};
      cmd_opt = std::move(cc);
      tok = next_token();
    } else {
      return reply(
          make_jsonrpc_error(id, -32602, "missing 'inference' or 'token'"));
    }

    const compile_command& cmd = *cmd_opt;

    // Check asm_cache_2 (keyed by command+directory)
    std::string cache_key{cmd.command + "|" + cmd.directory.string()};
    if (auto it = asm_cache_2.find(cache_key); it != asm_cache_2.end()) {
      send_progress(id, "grabasm", "cached", 0);
      int cached_tok{it->second.first};
      const auto& cr = it->second.second.result;
      json::object result{};
      result["token"] = cached_tok;
      result["cached"] = "other";
      json::object cc{};
      cc["compiler"] = cr.invocation.compiler;
      cc["compiler_version"] = cr.invocation.compiler_version;
      result["compilation_command"] = std::move(cc);
      return reply(make_result(id, std::move(result)));
    }

    send_progress(id, "grabasm", "running");
    auto t0 = clock_t::now();

    compilation_result cr{};
    try {
      cr = get_asm(cmd);
    } catch (compilation_error& e) {
      auto ms = duration_ms(t0);
      send_progress(id, "grabasm", "error", ms);
      json::object data{};
      data["compiler_invocation"] = meta_to_json(e.invocation);
      xpto::linespan ls{e.dribble};
      data["dribble"] = json::array(ls.begin(), ls.end());
      return reply(make_jsonrpc_error(id, -32603, e.what(), std::move(data)));
    } catch (std::exception& e) {
      auto ms = duration_ms(t0);
      send_progress(id, "grabasm", "error", ms);
      json::object data{};
      data["dribble"] = e.what();
      return reply(make_jsonrpc_error(id, -32603, e.what(), std::move(data)));
    }

    auto ms = duration_ms(t0);
    send_progress(id, "grabasm", "done", ms);

    asm_entry entry{cr};
    asm_cache_1[tok] = entry;
    asm_cache_2[cache_key] = {tok, entry};

    json::object result{};
    result["token"] = tok;
    result["cached"] = false;
    json::object cc{};
    cc["compiler"] = cr.invocation.compiler;
    cc["compiler_version"] = cr.invocation.compiler_version;
    result["compilation_command"] = std::move(cc);
    reply(make_result(id, std::move(result)));
  }

  void handle_annotate(const json::value& id, const json::object& params) {
    const json::object* opts_ptr{nullptr};
    if (params.contains("options")) {
      opts_ptr = params.at("options").if_object();
    }
    auto aopts = parse_aopts(opts_ptr);

    if (!params.contains("token") && !params.contains("asm_blob"))
      return reply(
          make_jsonrpc_error(id, -32602, "missing 'token' or 'asm_blob'"));

    std::optional<std::string_view> asm_blob_view{};
    std::string asm_blob_owned{};
    std::optional<fs::path> src_path{};
    token_t tok{};

    if (params.contains("token")) {
      tok = params.at("token").as_int64();

      // Check annotate cache
      if (auto it = annotate_cache_1.find(tok); it != annotate_cache_1.end()) {
        send_progress(id, "annotate", "cached", 0);
        json::object result{it->second.annotated};
        result["token"] = tok;
        result["cached"] = "token";
        return reply(make_result(id, std::move(result)));
      }

      // Fallback to the previous phase
      if (auto it = asm_cache_1.find(tok); it != asm_cache_1.end()) {
        asm_blob_view = it->second.result.assembly;
        if (auto iit = infer_cache_1.find(tok); iit != infer_cache_1.end())
          src_path = iit->second.cmd.file;
      } else {
        return reply(
            make_jsonrpc_error(id, -32602, "token not found in asm cache"));
      }
    } else {
      asm_blob_owned = std::string{params.at("asm_blob").as_string()};
      asm_blob_view = asm_blob_owned;
      tok = next_token();
    }

    send_progress(id, "annotate", "running");
    auto t0 = clock_t::now();

    json::object annotated{};
    try {
      annotated = annotate_to_json(*asm_blob_view, aopts, src_path);
    } catch (std::exception& e) {
      auto ms = duration_ms(t0);
      send_progress(id, "annotate", "error", ms);
      json::object data{};
      data["dribble"] = e.what();
      return reply(make_jsonrpc_error(id, -32603, e.what(), std::move(data)));
    }

    auto ms = duration_ms(t0);
    send_progress(id, "annotate", "done", ms);

    annotate_cache_1[tok] = annotate_entry{annotated};

    json::object result{std::move(annotated)};
    result["token"] = tok;
    result["cached"] = false;
    reply(make_result(id, std::move(result)));
  }
};

}  // namespace xpto::blot
