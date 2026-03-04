#include <doctest/doctest.h>

#include <boost/json.hpp>
#include <deque>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "fixture.hpp"
#include "session.hpp"

namespace json = boost::json;
namespace fs = std::filesystem;

namespace xpto::blot::tests {

struct jsonrpc_error : std::runtime_error {
  int code{};
  std::string message;

  explicit jsonrpc_error(const json::object& err)
      : std::runtime_error{
            "JSONRPC error " +
            std::to_string(err.at("code").as_int64()) + ": " +
            std::string{err.at("message").as_string()}},
        code{static_cast<int>(err.at("code").as_int64())},
        message{err.at("message").as_string()} {}
};

struct mock_session : session {
  mock_session(const fs::path& ccj, const fs::path& root)
      : session{ccj, root} {}

  void send(const json::object& msg) override { outbox.push_back(msg); }

  // Serialize and dispatch a JSONRPC request; return the result object.
  // Throws jsonrpc_error if the response contains an "error" field.
  json::object call(std::string_view method, json::object params = {}) {
    json::object req{};
    req["jsonrpc"] = "2.0";
    req["id"] = next_id_++;
    req["method"] = method;
    req["params"] = std::move(params);
    handle_frame(json::serialize(req));

    while (!outbox.empty()) {
      auto msg = outbox.front();
      outbox.pop_front();
      if (msg.contains("method")) {
        notifications_.push_back(std::move(msg));
        continue;
      }
      if (msg.contains("error"))
        throw jsonrpc_error{msg.at("error").as_object()};
      return msg.at("result").as_object();
    }
    throw std::runtime_error{"call(): no response in outbox"};
  }

  std::vector<json::object> pop_notifications() {
    return std::move(notifications_);
  }

 private:
  int next_id_{1};
  std::vector<json::object> notifications_;
  std::deque<json::object> outbox;
};

static std::tuple<int64_t, int64_t, int64_t> run_pipeline(mock_session& sess) {
  sess.call("initialize");
  json::object ip{};
  ip["file"] = "source.cpp";
  auto infer_res = sess.call("blot/infer", ip);

  json::object ap{};
  ap["token"] = infer_res.at("token").as_int64();
  auto asm_res = sess.call("blot/grab_asm", ap);

  json::object annp{};
  annp["token"] = asm_res.at("token").as_int64();
  json::object opts{};
  opts["demangle"] = false;
  annp["options"] = std::move(opts);
  auto ann_res = sess.call("blot/annotate", annp);

  return {
    infer_res.at("token").as_int64(), asm_res.at("token").as_int64(),
    ann_res.at("token").as_int64()};
}

// NOLINTNEXTLINE(*-macro-usage)
#define CHECK_RPC_ERROR(sess, method, params, expected_code) \
  /* NOLINTNEXTLINE */                                       \
  do {                                                       \
    bool _caught = false;                                    \
    try {                                                    \
      (sess).call(method, params);                           \
    } catch (const jsonrpc_error& _e) {                      \
      CHECK(_e.code == (expected_code));                     \
      _caught = true;                                        \
    }                                                        \
    CHECK(_caught);                                          \
  } while (false)

struct gcc_minimal_fixture {
  fs::path root{fixture_dir("gcc-minimal")};
  fs::path ccj{root / "compile_commands.json"};
  mock_session sess{ccj, root};

  gcc_minimal_fixture() { fs::current_path(root); }
};

TEST_CASE_FIXTURE(gcc_minimal_fixture, "server_initialize") {
  auto result = sess.call("initialize");
  CHECK(result.contains("serverInfo"));
  CHECK(result.at("serverInfo").as_object().at("name").as_string() == "blot");
  CHECK(result.contains("ccj"));
  CHECK(result.contains("project_root"));
  auto ccj_str = std::string{result.at("ccj").as_string()};
  CHECK(ccj_str.find("gcc-minimal") != std::string::npos);
}

TEST_CASE_FIXTURE(gcc_minimal_fixture, "server_shutdown") {
  sess.call("initialize");
  auto result = sess.call("shutdown");
  CHECK(result.empty());
}

TEST_CASE_FIXTURE(gcc_minimal_fixture, "server_unknown_method") {
  CHECK_RPC_ERROR(sess, "no_such_method", json::object{}, -32601);
}

TEST_CASE_FIXTURE(gcc_minimal_fixture, "server_full_pipeline") {
  // CWD must be the fixture dir so "directory":"." in compile_commands.json
  // resolves correctly (same approach as annotation-tests.cpp).
  sess.call("initialize");

  // Phase 1: infer
  json::object infer_params{};
  infer_params["file"] = "source.cpp";
  auto infer_res = sess.call("blot/infer", infer_params);
  CHECK(infer_res.contains("token"));
  CHECK(infer_res.contains("inference"));
  auto& inf = infer_res.at("inference").as_object();
  CHECK(inf.contains("annotation_target"));
  CHECK(inf.contains("compilation_command"));
  CHECK(inf.contains("compilation_directory"));
  CHECK(infer_res.at("cached").as_bool() == false);

  // Phase 2: grab_asm
  json::object asm_params{};
  asm_params["token"] = infer_res.at("token").as_int64();
  auto asm_res = sess.call("blot/grab_asm", asm_params);
  CHECK(asm_res.contains("token"));
  CHECK(asm_res.contains("compilation_command"));
  CHECK(asm_res.at("cached").as_bool() == false);

  // Phase 3: annotate
  json::object ann_params{};
  ann_params["token"] = asm_res.at("token").as_int64();
  json::object opts{};
  opts["demangle"] = true;
  ann_params["options"] = std::move(opts);
  auto ann_res = sess.call("blot/annotate", ann_params);
  CHECK(ann_res.contains("assembly"));
  CHECK(ann_res.contains("line_mappings"));
  CHECK(ann_res.at("assembly").as_array().size() > 0);
  CHECK(ann_res.at("cached").as_bool() == false);
}

TEST_CASE_FIXTURE(gcc_minimal_fixture, "server_progress_notifications") {
  sess.call("initialize");

  json::object infer_params{};
  infer_params["file"] = "source.cpp";
  sess.call("blot/infer", infer_params);
  auto notifs = sess.pop_notifications();

  REQUIRE(notifs.size() == 2);
  auto& p0 = notifs[0].at("params").as_object();
  auto& p1 = notifs[1].at("params").as_object();
  CHECK(std::string{p0.at("phase").as_string()} == "infer");
  CHECK(std::string{p1.at("phase").as_string()} == "infer");
  CHECK(std::string{p0.at("status").as_string()} == "running");
  auto status1 = std::string{p1.at("status").as_string()};
  CHECK((status1 == "done" || status1 == "cached" || status1 == "error"));
  CHECK(!p0.contains("elapsed_ms"));
  CHECK(p1.contains("elapsed_ms"));
}

TEST_CASE_FIXTURE(gcc_minimal_fixture, "server_infer_unknown_file") {
  sess.call("initialize");

  json::object params{};
  params["file"] = "no_such_file.cpp";
  bool caught = false;
  try {
    sess.call("blot/infer", params);
  } catch (const jsonrpc_error& e) {
    CHECK((e.code == -32602 || e.code == -32603));
    caught = true;
  }
  CHECK(caught);
}

TEST_CASE_FIXTURE(gcc_minimal_fixture, "server_annotate_options") {
  sess.call("initialize");

  json::object ip{};
  ip["file"] = "source.cpp";
  auto infer_res = sess.call("blot/infer", ip);

  json::object ap{};
  ap["token"] = infer_res.at("token").as_int64();
  auto asm_res = sess.call("blot/grab_asm", ap);

  json::object ann_params{};
  ann_params["token"] = asm_res.at("token").as_int64();
  json::object opts{};
  opts["demangle"] = true;
  opts["preserve_directives"] = true;
  opts["preserve_comments"] = false;
  ann_params["options"] = std::move(opts);
  auto ann_res = sess.call("blot/annotate", ann_params);
  CHECK(ann_res.contains("assembly"));
  CHECK(ann_res.at("assembly").as_array().size() > 0);
}

TEST_CASE_FIXTURE(gcc_minimal_fixture, "server_errors_unknown_method") {
  CHECK_RPC_ERROR(sess, "blot/no_such_method", json::object{}, -32601);
}

TEST_CASE_FIXTURE(gcc_minimal_fixture, "server_errors_infer_missing_params") {
  CHECK_RPC_ERROR(sess, "blot/infer", json::object{}, -32602);
}

TEST_CASE_FIXTURE(gcc_minimal_fixture, "server_errors_infer_path_traversal") {
  json::object params{};
  params["file"] = "../../etc/passwd";
  CHECK_RPC_ERROR(sess, "blot/infer", params, -32602);
}

TEST_CASE_FIXTURE(gcc_minimal_fixture, "server_errors_infer_stale_token") {
  json::object params{};
  params["token"] = int64_t{999999};
  CHECK_RPC_ERROR(sess, "blot/infer", params, -32602);
}

TEST_CASE_FIXTURE(gcc_minimal_fixture, "server_errors_grabasm_missing_params") {
  CHECK_RPC_ERROR(sess, "blot/grab_asm", json::object{}, -32602);
}

TEST_CASE_FIXTURE(gcc_minimal_fixture, "server_errors_grabasm_stale_token") {
  json::object params{};
  params["token"] = int64_t{999999};
  CHECK_RPC_ERROR(sess, "blot/grab_asm", params, -32602);
}

TEST_CASE_FIXTURE(
    gcc_minimal_fixture, "server_errors_annotate_missing_params") {
  CHECK_RPC_ERROR(sess, "blot/annotate", json::object{}, -32602);
}

TEST_CASE_FIXTURE(gcc_minimal_fixture, "server_errors_annotate_stale_token") {
  json::object params{};
  params["token"] = int64_t{999999};
  CHECK_RPC_ERROR(sess, "blot/annotate", params, -32602);
}

TEST_CASE_FIXTURE(gcc_minimal_fixture, "server_errors_do_not_break_session") {
  for (int i = 0; i < 3; ++i) {
    try {
      sess.call("blot/infer", json::object{});
      // NOLINTNEXTLINE(*-empty-catch)
    } catch (const jsonrpc_error&) {
    }
  }

  auto result = sess.call("initialize");
  CHECK(result.contains("serverInfo"));
}

TEST_CASE_FIXTURE(gcc_minimal_fixture, "server_cache_grabasm_token") {
  auto [infer_tok, asm_tok, ann_tok] = run_pipeline(sess);

  json::object p{};
  p["token"] = asm_tok;
  auto res = sess.call("blot/grab_asm", p);
  CHECK(std::string{res.at("cached").as_string()} == "token");
}

TEST_CASE_FIXTURE(gcc_minimal_fixture, "server_cache_annotate_token") {
  auto [infer_tok, asm_tok, ann_tok] = run_pipeline(sess);

  json::object p{};
  p["token"] = ann_tok;
  json::object opts{};
  opts["demangle"] = false;
  p["options"] = std::move(opts);
  auto res = sess.call("blot/annotate", p);
  CHECK(std::string{res.at("cached").as_string()} == "token");
}

TEST_CASE_FIXTURE(gcc_minimal_fixture, "server_cache_infer_token") {
  auto [infer_tok, asm_tok, ann_tok] = run_pipeline(sess);

  json::object p{};
  p["token"] = infer_tok;
  auto res = sess.call("blot/infer", p);
  CHECK(std::string{res.at("cached").as_string()} == "token");
  CHECK(res.at("token").as_int64() == infer_tok);
}

TEST_CASE_FIXTURE(gcc_minimal_fixture, "server_cache_is_session_scoped") {
  int64_t infer_tok{};
  {
    mock_session sess1{ccj, root};
    auto [it, at, ant] = run_pipeline(sess1);
    infer_tok = it;
  }

  // New session — tokens from the old session are unknown
  mock_session sess2{ccj, root};
  sess2.call("initialize");
  json::object p{};
  p["token"] = infer_tok;
  CHECK_RPC_ERROR(sess2, "blot/infer", p, -32602);
}

TEST_CASE_FIXTURE(gcc_minimal_fixture, "server_cache_other_inference") {
  sess.call("initialize");

  json::object ip{};
  ip["file"] = "source.cpp";
  auto infer_res = sess.call("blot/infer", ip);
  auto tok = infer_res.at("token").as_int64();

  json::object ap{};
  ap["token"] = tok;
  auto asm_res = sess.call("blot/grab_asm", ap);
  CHECK(asm_res.at("cached").as_bool() == false);

  // Pass explicit inference object — same command+directory → hits asm_cache_2
  auto& inf = infer_res.at("inference").as_object();
  json::object inf2{};
  inf2["compilation_command"] = inf.at("compilation_command");
  inf2["compilation_directory"] = inf.at("compilation_directory");
  inf2["annotation_target"] = inf.at("annotation_target");
  json::object ap2{};
  ap2["inference"] = std::move(inf2);
  auto asm2 = sess.call("blot/grab_asm", ap2);
  CHECK(std::string{asm2.at("cached").as_string()} == "other");
  CHECK(asm2.at("token").as_int64() == tok);
}

TEST_CASE_FIXTURE(gcc_minimal_fixture, "server_cache_other_pipelines") {
  sess.call("initialize");

  // Pipeline A: populate asm_cache_2
  json::object ip{};
  ip["file"] = "source.cpp";
  auto infer_a = sess.call("blot/infer", ip);
  auto tok_a = infer_a.at("token").as_int64();

  json::object ap_a{};
  ap_a["token"] = tok_a;
  auto asm_a = sess.call("blot/grab_asm", ap_a);
  CHECK(asm_a.at("cached").as_bool() == false);

  // Pipeline B: fresh infer → new token
  auto infer_b = sess.call("blot/infer", ip);
  auto tok_b = infer_b.at("token").as_int64();
  CHECK(tok_b != tok_a);

  // grab_asm(tok_b): misses asm_cache_1, falls through, hits asm_cache_2
  json::object ap_b{};
  ap_b["token"] = tok_b;
  auto asm_b = sess.call("blot/grab_asm", ap_b);
  CHECK(std::string{asm_b.at("cached").as_string()} == "other");
  CHECK(asm_b.at("token").as_int64() == tok_a);
}

}  // namespace xpto::blot::tests
