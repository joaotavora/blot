#include "session.hpp"

#include <atomic>
#include <boost/json.hpp>
#include <chrono>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>

#include "blot/blot.hpp"
#include "blot/ccj.hpp"
#include "json_helpers.hpp"
#include "linespan.hpp"
#include "logger.hpp"

namespace xpto::blot {

namespace fs = std::filesystem;

/// File-scope helpers

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

/// session members

session::session(fs::path ccj_path, fs::path project_root)
: ccj_path{std::move(ccj_path)}, project_root{std::move(project_root)} {}

void session::reply_(const json::value& id, const jsonrpc_response_t& res) {
  json::object msg = std::visit(
      [&](auto&& x) {
        using t = std::decay_t<decltype(x)>;
        if constexpr (std::is_same_v<t, json::object>) {
          json::object m{};
          m["jsonrpc"] = "2.0";
          m["id"] = id;
          m["result"] = std::forward<decltype(x)>(x);
          return m;
        } else if constexpr (std::is_same_v<t, error>) {
          json::object err{};
          err["code"] = x.code;
          err["message"] = x.message;
          if (x.data) err["data"] = *x.data;
          json::object m{};
          m["jsonrpc"] = "2.0";
          m["id"] = id;
          m["error"] = std::move(err);
          return m;
        } else {
          static_assert(!sizeof(t), "unhandled jsonrpc_response_t alternative");
        }
      },
      res);
  send(msg);
}

void session::send_progress_(
    const json::value& request_id, std::string_view phase,
    std::string_view status, std::optional<long long> elapsed_ms) {
  json::object params{};
  params["request_id"] = request_id;
  params["phase"] = phase;
  params["status"] = status;
  if (elapsed_ms) params["elapsed_ms"] = *elapsed_ms;
  json::object msg{};
  msg["jsonrpc"] = "2.0";
  msg["method"] = "blot/progress";
  msg["params"] = std::move(params);
  send(msg);
}

/// Handlers

jsonrpc_response_t session::handle_initialize(
    const json::object& /*params*/,
    std::invocable<
        std::string_view, std::string_view> auto&& /*send_progress*/) {
  json::object result{};
  json::object server_info{};
  server_info["name"] = "blot";
  server_info["version"] = "0.1";
  result["serverInfo"] = std::move(server_info);
  result["ccj"] = ccj_path.string();
  result["project_root"] = project_root.string();
  return result;
}

jsonrpc_response_t session::handle_infer(
    const json::object& params,
    std::invocable<std::string_view, std::string_view> auto&& send_progress) {
  token_t tok{};
  if (params.contains("token")) {
    tok = params.at("token").as_int64();
    std::optional<json::object> cached;
    {
      std::lock_guard lk{cache_mutex};
      if (auto it = infer_cache_1.find(tok); it != infer_cache_1.end()) {
        json::object result{};
        result["token"] = tok;
        result["cached"] = "token";
        json::object inf{};
        inf["annotation_target"] = it->second.cmd.file.string();
        inf["compilation_command"] = it->second.cmd.command;
        inf["compilation_directory"] = it->second.cmd.directory.string();
        result["inference"] = std::move(inf);
        cached = std::move(result);
      }
    }
    if (cached) {
      LOG_DEBUG("infer cache hit: token={}", tok);
      send_progress("infer", "running");
      send_progress("infer", "cached", 0);
      return *cached;
    }
    LOG_DEBUG("infer cache miss: token={} not found", tok);
    return error{-32602, "token not found in infer cache"};
  }

  tok = next_token();

  if (!params.contains("file"))
    return error{-32602, "missing 'file' or 'token'"};

  std::string file_str{params.at("file").as_string()};

  std::error_code ec{};
  auto abs_file = fs::weakly_canonical(project_root / file_str, ec);
  if (ec || !abs_file.string().starts_with(project_root.string()))
    return error{-32602, "path traversal denied"};

  LOG_DEBUG("infer: token={}, file={}", tok, file_str);
  send_progress("infer", "running");
  auto t0 = clock_t::now();

  std::optional<compile_command> cmd{};
  try {
    cmd = infer(ccj_path, abs_file);
  } catch (std::exception& e) {
    auto ms = duration_ms(t0);
    send_progress("infer", "error", ms);
    json::object data{};
    data["dribble"] = e.what();
    return error{-32603, "infer() threw", std::move(data)};
  }

  auto ms = duration_ms(t0);

  if (!cmd) {
    send_progress("infer", "error", ms);
    return error{-32602, "no CCJ entry found for file"};
  }

  send_progress("infer", "done", ms);

  {
    std::lock_guard lk{cache_mutex};
    infer_cache_1[tok] = infer_entry{*cmd};
    LOG_DEBUG("infer cache store: token={}, file={}", tok, cmd->file.string());
  }

  json::object result{};
  result["token"] = tok;
  result["cached"] = false;
  json::object inf{};
  inf["annotation_target"] = cmd->file.string();
  inf["compilation_command"] = cmd->command;
  inf["compilation_directory"] = cmd->directory.string();
  result["inference"] = std::move(inf);
  return result;
}

jsonrpc_response_t session::handle_grabasm(
    const json::object& params,
    std::invocable<std::string_view, std::string_view> auto&& send_progress) {
  // Phase 1: locked cache check
  std::optional<json::object> cached;
  compile_command cmd;
  std::string cache_key;
  token_t tok{};
  {
    std::lock_guard lk{cache_mutex};
    if (params.contains("token")) {
      tok = params.at("token").as_int64();
      if (auto it = asm_cache_1.find(tok); it != asm_cache_1.end()) {
        LOG_DEBUG("grabasm cache hit (asm_cache_1): token={}", tok);
        json::object result{};
        result["token"] = tok;
        result["cached"] = "token";
        json::object cc{};
        cc["compiler"] = it->second.result.invocation.compiler;
        cc["compiler_version"] = it->second.result.invocation.compiler_version;
        result["compilation_command"] = std::move(cc);
        cached = std::move(result);
      } else if (auto it2 = infer_cache_1.find(tok);
                 it2 != infer_cache_1.end()) {
        cmd = it2->second.cmd;
      } else {
        return error{-32602, "token not found in infer cache"};
      }
    } else if (params.contains("inference")) {
      auto& inf = params.at("inference").as_object();
      cmd.command = std::string{inf.at("compilation_command").as_string()};
      cmd.directory =
          fs::path{std::string{inf.at("compilation_directory").as_string()}};
      if (inf.contains("annotation_target"))
        cmd.file =
            fs::path{std::string{inf.at("annotation_target").as_string()}};
      tok = next_token();
    } else {
      return error{-32602, "missing 'inference' or 'token'"};
    }

    if (!cached) {
      cache_key = cmd.command + '\0' + cmd.directory.string();
      if (auto it = asm_cache_2.find(cache_key); it != asm_cache_2.end()) {
        int cached_tok{it->second.first};
        LOG_DEBUG("grabasm cache hit (asm_cache_2): tok={} -> cached_tok={}", tok, cached_tok);
        const auto& cr = it->second.second.result;
        json::object result{};
        result["token"] = cached_tok;
        result["cached"] = "other";
        json::object cc{};
        cc["compiler"] = cr.invocation.compiler;
        cc["compiler_version"] = cr.invocation.compiler_version;
        result["compilation_command"] = std::move(cc);
        cached = std::move(result);
      }
    }
  }

  if (cached) {
    send_progress("grabasm", "running");
    send_progress("grabasm", "cached", 0);
    return *cached;
  }

  // Phase 2: compile outside lock
  send_progress("grabasm", "running");
  auto t0 = clock_t::now();

  compilation_result cr{};
  try {
    cr = get_asm(cmd);
  } catch (compilation_error& e) {
    auto ms = duration_ms(t0);
    send_progress("grabasm", "error", ms);
    json::object data{};
    data["compiler_invocation"] = meta_to_json(e.invocation);
    xpto::linespan ls{e.dribble};
    data["dribble"] = json::array(ls.begin(), ls.end());
    return error{-32603, e.what(), std::move(data)};
  } catch (std::exception& e) {
    auto ms = duration_ms(t0);
    send_progress("grabasm", "error", ms);
    json::object data{};
    data["dribble"] = e.what();
    return error{-32603, e.what(), std::move(data)};
  }

  auto ms = duration_ms(t0);
  send_progress("grabasm", "done", ms);

  // Phase 3: locked insert
  asm_entry entry{cr};
  {
    std::lock_guard lk{cache_mutex};
    asm_cache_1[tok] = entry;
    asm_cache_2[cache_key] = {tok, entry};
    LOG_DEBUG("grabasm cache store: token={}, dir={}", tok, cmd.directory.string());
  }

  json::object result{};
  result["token"] = tok;
  result["cached"] = false;
  json::object cc{};
  cc["compiler"] = cr.invocation.compiler;
  cc["compiler_version"] = cr.invocation.compiler_version;
  result["compilation_command"] = std::move(cc);
  return result;
}

jsonrpc_response_t session::handle_annotate(
    const json::object& params,
    std::invocable<std::string_view, std::string_view> auto&& send_progress) {
  const json::object* opts_ptr{nullptr};
  if (params.contains("options")) {
    opts_ptr = params.at("options").if_object();
  }
  auto aopts = parse_aopts(opts_ptr);

  if (!params.contains("token") && !params.contains("asm_blob"))
    return error{-32602, "missing 'token' or 'asm_blob'"};

  std::string asm_blob;
  std::optional<fs::path> src_path{};
  token_t tok{};

  if (params.contains("token")) {
    tok = params.at("token").as_int64();
    std::optional<json::object> cached;
    {
      std::lock_guard lk{cache_mutex};
      if (auto it = annotate_cache_1.find(tok); it != annotate_cache_1.end()) {
        json::object result{it->second.annotated};
        result["token"] = tok;
        result["cached"] = "token";
        cached = std::move(result);
      } else if (auto it2 = asm_cache_1.find(tok); it2 != asm_cache_1.end()) {
        asm_blob = it2->second.result.assembly;
        if (auto iit = infer_cache_1.find(tok); iit != infer_cache_1.end())
          src_path = iit->second.cmd.file;
      } else {
        return error{-32602, "token not found in asm cache"};
      }
    }
    if (cached) {
      LOG_DEBUG("annotate cache hit: token={}", tok);
      send_progress("annotate", "running");
      send_progress("annotate", "cached", 0);
      return *cached;
    }
  } else {
    asm_blob = std::string{params.at("asm_blob").as_string()};
    tok = next_token();
  }

  // Phase 2: annotate outside lock
  send_progress("annotate", "running");
  auto t0 = clock_t::now();

  json::object annotated{};
  try {
    annotated = annotate_to_json(asm_blob, aopts, src_path);
  } catch (std::exception& e) {
    auto ms = duration_ms(t0);
    send_progress("annotate", "error", ms);
    json::object data{};
    data["dribble"] = e.what();
    return error{-32603, e.what(), std::move(data)};
  }

  auto ms = duration_ms(t0);
  send_progress("annotate", "done", ms);

  // Phase 3: locked insert
  {
    std::lock_guard lk{cache_mutex};
    annotate_cache_1[tok] = annotate_entry{annotated};
    LOG_DEBUG("annotate cache store: token={}", tok);
  }

  json::object result{std::move(annotated)};
  result["token"] = tok;
  result["cached"] = false;
  return result;
}

bool session::handle_frame(std::string_view text) {
  json::value msg_val{};
  {
    std::error_code jec{};
    msg_val = json::parse(text, jec);
    if (jec) {
      LOG_WARN("Ignoring odd JSONRPC frame: {}", text);
      return true;
    }
  }

  auto* msg = msg_val.if_object();
  if (!msg) {
    LOG_WARN("Ignoring non object JSON value in JSONRPC frame: {}", text);
    return true;
  }

  json::value id{nullptr};
  if (msg->contains("id")) id = msg->at("id");

  if (!msg->contains("method")) {
    reply_(id, error{-32600, "missing method"});
    return true;
  }

  std::string method{msg->at("method").as_string()};
  const json::object* params_ptr{nullptr};
  if (msg->contains("params")) {
    params_ptr = msg->at("params").if_object();
  }
  json::object empty_params{};
  const json::object& params = params_ptr ? *params_ptr : empty_params;

  LOG_INFO("ws rpc: {}", method);

  auto sp = [this, &id](
                std::string_view phase, std::string_view status,
                std::optional<long long> ms = std::nullopt) {
    send_progress_(id, phase, status, ms);
  };

  if (method == "initialize") {
    reply_(id, handle_initialize(params, sp));
  } else if (method == "blot/infer") {
    reply_(id, handle_infer(params, sp));
  } else if (method == "blot/grab_asm") {
    reply_(id, handle_grabasm(params, sp));
  } else if (method == "blot/annotate") {
    reply_(id, handle_annotate(params, sp));
  } else if (method == "shutdown") {
    reply_(id, json::object{});
    return false;
  } else {
    reply_(id, error{-32601, "Method not found"});
  }
  return true;
}

}  // namespace xpto::blot
