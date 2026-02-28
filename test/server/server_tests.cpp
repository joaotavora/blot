#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#define BOOST_ASIO_NO_DEPRECATED
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/json.hpp>
#include <deque>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "session.hpp"
#include "web.hpp"

namespace json = boost::json;
namespace fs = std::filesystem;
namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

namespace xpto::blot {

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

struct test_session : session<test_session> {
  std::deque<json::object> outbox;

  test_session(const fs::path& ccj, const fs::path& root)
      : session<test_session>{ccj, root} {}

  void send(const json::object& msg) { outbox.push_back(msg); }

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
    auto n = std::move(notifications_);
    notifications_.clear();
    return n;
  }

 private:
  int next_id_{1};
  std::vector<json::object> notifications_;
};

struct http_response {
  int status{};
  std::string body;
  json::object json_body() const {
    return boost::json::parse(body).as_object();
  }
};

static net::awaitable<http_response> async_http_get(
    int port, std::string_view path) {
  auto ex = co_await net::this_coro::executor;
  tcp::resolver resolver{ex};
  beast::tcp_stream stream{ex};

  auto results = co_await resolver.async_resolve(
      "127.0.0.1", std::to_string(port), net::use_awaitable);
  co_await stream.async_connect(results, net::use_awaitable);

  http::request<http::string_body> req{http::verb::get, std::string{path}, 11};
  req.set(http::field::host, "127.0.0.1");
  req.set(http::field::connection, "close");
  co_await http::async_write(stream, req, net::use_awaitable);

  beast::flat_buffer buf;
  http::response<http::string_body> res;
  co_await http::async_read(stream, buf, res, net::use_awaitable);

  beast::error_code ec;
  stream.socket().shutdown(tcp::socket::shutdown_both, ec);
  co_return http_response{
    static_cast<int>(res.result_int()), std::string{res.body()}};
}

// Return the fixture directory path for a named fixture.
// Tests that invoke infer/grab_asm/annotate must call
// fs::current_path(fixture_dir(name)) so that relative paths in
// compile_commands.json ("directory": ".") resolve correctly — exactly as
// annotation-tests.cpp does with its fs::current_path(fixture_subdir) call.
static fs::path fixture_dir(std::string_view name) {
  return fs::path{TEST_FIXTURE_DIR} / name;
}

static fs::path fixture_ccj(std::string_view name) {
  return fixture_dir(name) / "compile_commands.json";
}

static std::tuple<int64_t, int64_t, int64_t> run_pipeline(test_session& sess) {
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

}  // namespace xpto::blot

// Owns the io_context and server setup. Caller drives it via ioc.run().
struct test_server {
  net::io_context ioc;
  int port;

  explicit test_server(const fs::path& ccj)
      : port{xpto::blot::run_web_server(ioc.get_executor(), ccj, 0)} {}

  test_server(const test_server&) = delete;
  test_server& operator=(const test_server&) = delete;
};

// Run a test coroutine on ioc alongside the server. Stops ioc on completion
// and rethrows any exception (so doctest REQUIRE failures propagate).
template <typename Coro>
static void run_test(net::io_context& ioc, Coro coro) {
  std::exception_ptr ep;
  net::co_spawn(ioc, std::move(coro), [&ep, &ioc](std::exception_ptr e) {
    ep = e;
    ioc.stop();
  });
  ioc.run();
  if (ep) std::rethrow_exception(ep);
}

// NOLINTNEXTLINE(*-macro-usage)
#define CHECK_RPC_ERROR(sess, method, params, expected_code) \
  /* NOLINTNEXTLINE */                                       \
  do {                                                       \
    bool _caught = false;                                    \
    try {                                                    \
      (sess).call(method, params);                           \
    } catch (const xpto::blot::jsonrpc_error& _e) {          \
      CHECK(_e.code == (expected_code));                     \
      _caught = true;                                        \
    }                                                        \
    CHECK(_caught);                                          \
  } while (false)

TEST_CASE("server_initialize") {
  auto ccj = xpto::blot::fixture_ccj("gcc-minimal");
  auto root = ccj.parent_path();
  xpto::blot::test_session sess{ccj, root};

  auto result = sess.call("initialize");
  CHECK(result.contains("serverInfo"));
  CHECK(result.at("serverInfo").as_object().at("name").as_string() == "blot");
  CHECK(result.contains("ccj"));
  CHECK(result.contains("project_root"));
  auto ccj_str = std::string{result.at("ccj").as_string()};
  CHECK(ccj_str.find("gcc-minimal") != std::string::npos);
}

TEST_CASE("server_shutdown") {
  auto ccj = xpto::blot::fixture_ccj("gcc-minimal");
  auto root = ccj.parent_path();
  xpto::blot::test_session sess{ccj, root};

  sess.call("initialize");
  auto result = sess.call("shutdown");
  CHECK(result.empty());
}

TEST_CASE("server_unknown_method") {
  auto ccj = xpto::blot::fixture_ccj("gcc-minimal");
  auto root = ccj.parent_path();
  xpto::blot::test_session sess{ccj, root};

  CHECK_RPC_ERROR(sess, "no_such_method", json::object{}, -32601);
}

TEST_CASE("server_full_pipeline") {
  // CWD must be the fixture dir so "directory":"." in compile_commands.json
  // resolves correctly (same approach as annotation-tests.cpp).
  auto dir = xpto::blot::fixture_dir("gcc-minimal");
  fs::current_path(dir);
  auto ccj = dir / "compile_commands.json";
  xpto::blot::test_session sess{ccj, dir};
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

TEST_CASE("server_progress_notifications") {
  auto dir = xpto::blot::fixture_dir("gcc-minimal");
  fs::current_path(dir);
  auto ccj = dir / "compile_commands.json";
  xpto::blot::test_session sess{ccj, dir};
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

TEST_CASE("server_infer_unknown_file") {
  auto ccj = xpto::blot::fixture_ccj("gcc-minimal");
  auto root = ccj.parent_path();
  xpto::blot::test_session sess{ccj, root};
  sess.call("initialize");

  json::object params{};
  params["file"] = "no_such_file.cpp";
  bool caught = false;
  try {
    sess.call("blot/infer", params);
  } catch (const xpto::blot::jsonrpc_error& e) {
    CHECK((e.code == -32602 || e.code == -32603));
    caught = true;
  }
  CHECK(caught);
}

TEST_CASE("server_annotate_options") {
  auto dir = xpto::blot::fixture_dir("gcc-minimal");
  fs::current_path(dir);
  auto ccj = dir / "compile_commands.json";
  xpto::blot::test_session sess{ccj, dir};
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

TEST_CASE("server_errors_unknown_method") {
  auto ccj = xpto::blot::fixture_ccj("gcc-minimal");
  auto root = ccj.parent_path();
  xpto::blot::test_session sess{ccj, root};

  CHECK_RPC_ERROR(sess, "blot/no_such_method", json::object{}, -32601);
}

TEST_CASE("server_errors_infer_missing_params") {
  auto ccj = xpto::blot::fixture_ccj("gcc-minimal");
  auto root = ccj.parent_path();
  xpto::blot::test_session sess{ccj, root};

  CHECK_RPC_ERROR(sess, "blot/infer", json::object{}, -32602);
}

TEST_CASE("server_errors_infer_path_traversal") {
  auto ccj = xpto::blot::fixture_ccj("gcc-minimal");
  auto root = ccj.parent_path();
  xpto::blot::test_session sess{ccj, root};

  json::object params{};
  params["file"] = "../../etc/passwd";
  CHECK_RPC_ERROR(sess, "blot/infer", params, -32602);
}

TEST_CASE("server_errors_infer_stale_token") {
  auto ccj = xpto::blot::fixture_ccj("gcc-minimal");
  auto root = ccj.parent_path();
  xpto::blot::test_session sess{ccj, root};

  json::object params{};
  params["token"] = int64_t{999999};
  CHECK_RPC_ERROR(sess, "blot/infer", params, -32602);
}

TEST_CASE("server_errors_grabasm_missing_params") {
  auto ccj = xpto::blot::fixture_ccj("gcc-minimal");
  auto root = ccj.parent_path();
  xpto::blot::test_session sess{ccj, root};

  CHECK_RPC_ERROR(sess, "blot/grab_asm", json::object{}, -32602);
}

TEST_CASE("server_errors_grabasm_stale_token") {
  auto ccj = xpto::blot::fixture_ccj("gcc-minimal");
  auto root = ccj.parent_path();
  xpto::blot::test_session sess{ccj, root};

  json::object params{};
  params["token"] = int64_t{999999};
  CHECK_RPC_ERROR(sess, "blot/grab_asm", params, -32602);
}

TEST_CASE("server_errors_annotate_missing_params") {
  auto ccj = xpto::blot::fixture_ccj("gcc-minimal");
  auto root = ccj.parent_path();
  xpto::blot::test_session sess{ccj, root};

  CHECK_RPC_ERROR(sess, "blot/annotate", json::object{}, -32602);
}

TEST_CASE("server_errors_annotate_stale_token") {
  auto ccj = xpto::blot::fixture_ccj("gcc-minimal");
  auto root = ccj.parent_path();
  xpto::blot::test_session sess{ccj, root};

  json::object params{};
  params["token"] = int64_t{999999};
  CHECK_RPC_ERROR(sess, "blot/annotate", params, -32602);
}

TEST_CASE("server_errors_do_not_break_session") {
  auto ccj = xpto::blot::fixture_ccj("gcc-minimal");
  auto root = ccj.parent_path();
  xpto::blot::test_session sess{ccj, root};

  for (int i = 0; i < 3; ++i) {
    try {
      sess.call("blot/infer", json::object{});
    } catch (const xpto::blot::jsonrpc_error&) {
    }  // NOLINT(*-empty-catch)
  }

  auto result = sess.call("initialize");
  CHECK(result.contains("serverInfo"));
}

TEST_CASE("server_cache_grabasm_token") {
  auto dir = xpto::blot::fixture_dir("gcc-minimal");
  fs::current_path(dir);
  auto ccj = dir / "compile_commands.json";
  xpto::blot::test_session sess{ccj, dir};
  auto [infer_tok, asm_tok, ann_tok] = run_pipeline(sess);

  json::object p{};
  p["token"] = asm_tok;
  auto res = sess.call("blot/grab_asm", p);
  CHECK(std::string{res.at("cached").as_string()} == "token");
}

TEST_CASE("server_cache_annotate_token") {
  auto dir = xpto::blot::fixture_dir("gcc-minimal");
  fs::current_path(dir);
  auto ccj = dir / "compile_commands.json";
  xpto::blot::test_session sess{ccj, dir};
  auto [infer_tok, asm_tok, ann_tok] = run_pipeline(sess);

  json::object p{};
  p["token"] = ann_tok;
  json::object opts{};
  opts["demangle"] = false;
  p["options"] = std::move(opts);
  auto res = sess.call("blot/annotate", p);
  CHECK(std::string{res.at("cached").as_string()} == "token");
}

TEST_CASE("server_cache_infer_token") {
  auto dir = xpto::blot::fixture_dir("gcc-minimal");
  fs::current_path(dir);
  auto ccj = dir / "compile_commands.json";
  xpto::blot::test_session sess{ccj, dir};
  auto [infer_tok, asm_tok, ann_tok] = run_pipeline(sess);

  json::object p{};
  p["token"] = infer_tok;
  auto res = sess.call("blot/infer", p);
  CHECK(std::string{res.at("cached").as_string()} == "token");
  CHECK(res.at("token").as_int64() == infer_tok);
}

TEST_CASE("server_cache_is_session_scoped") {
  auto dir = xpto::blot::fixture_dir("gcc-minimal");
  fs::current_path(dir);
  auto ccj = dir / "compile_commands.json";
  int64_t infer_tok{};
  {
    xpto::blot::test_session sess1{ccj, dir};
    auto [it, at, ant] = run_pipeline(sess1);
    infer_tok = it;
  }

  // New session — tokens from the old session are unknown
  xpto::blot::test_session sess2{ccj, dir};
  sess2.call("initialize");
  json::object p{};
  p["token"] = infer_tok;
  CHECK_RPC_ERROR(sess2, "blot/infer", p, -32602);
}

TEST_CASE("server_cache_other_inference") {
  auto dir = xpto::blot::fixture_dir("gcc-minimal");
  fs::current_path(dir);
  auto ccj = dir / "compile_commands.json";
  xpto::blot::test_session sess{ccj, dir};
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

TEST_CASE("server_cache_other_pipelines") {
  auto dir = xpto::blot::fixture_dir("gcc-minimal");
  fs::current_path(dir);
  auto ccj = dir / "compile_commands.json";
  xpto::blot::test_session sess{ccj, dir};
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

TEST_CASE("server_http_status_fields") {
  auto ccj = xpto::blot::fixture_ccj("gcc-minimal");
  test_server srv{ccj};
  run_test(srv.ioc, [&]() -> net::awaitable<void> {
    auto resp = co_await xpto::blot::async_http_get(srv.port, "/api/status");
    REQUIRE(resp.status == 200);
    auto data = resp.json_body();
    CHECK(data.contains("tu_count"));
    CHECK(data.contains("ccj"));
    CHECK(data.contains("project_root"));
  }());
}

TEST_CASE("server_http_status_tu_count") {
  auto ccj = xpto::blot::fixture_ccj("gcc-minimal");
  test_server srv{ccj};
  run_test(srv.ioc, [&]() -> net::awaitable<void> {
    auto resp = co_await xpto::blot::async_http_get(srv.port, "/api/status");
    REQUIRE(resp.status == 200);
    CHECK(resp.json_body().at("tu_count").as_int64() == 1);
  }());
}

TEST_CASE("server_http_status_paths") {
  auto ccj = xpto::blot::fixture_ccj("gcc-minimal");
  test_server srv{ccj};
  run_test(srv.ioc, [&]() -> net::awaitable<void> {
    auto resp = co_await xpto::blot::async_http_get(srv.port, "/api/status");
    REQUIRE(resp.status == 200);
    auto data = resp.json_body();
    auto ccj_str = std::string{data.at("ccj").as_string()};
    CHECK(ccj_str.find("gcc-minimal") != std::string::npos);
    auto root_str = std::string{data.at("project_root").as_string()};
    CHECK(fs::is_directory(root_str));
  }());
}

TEST_CASE("server_http_files_lists_source") {
  auto ccj = xpto::blot::fixture_ccj("gcc-minimal");
  test_server srv{ccj};
  run_test(srv.ioc, [&]() -> net::awaitable<void> {
    auto resp = co_await xpto::blot::async_http_get(srv.port, "/api/files");
    REQUIRE(resp.status == 200);
    auto data = resp.json_body();
    CHECK(data.contains("files"));
    bool found = false;
    for (auto& f : data.at("files").as_array()) {
      if (std::string{f.as_string()} == "source.cpp") {
        found = true;
        break;
      }
    }
    CHECK(found);
  }());
}

TEST_CASE("server_http_files_source_content") {
  auto ccj = xpto::blot::fixture_ccj("gcc-minimal");
  test_server srv{ccj};
  run_test(srv.ioc, [&]() -> net::awaitable<void> {
    auto resp = co_await xpto::blot::async_http_get(
        srv.port, "/api/source?file=source.cpp");
    REQUIRE(resp.status == 200);
    auto data = resp.json_body();
    CHECK(data.contains("content"));
    auto content = std::string{data.at("content").as_string()};
    CHECK(content.find("int main") != std::string::npos);
  }());
}

TEST_CASE("server_http_files_source_missing_param") {
  auto ccj = xpto::blot::fixture_ccj("gcc-minimal");
  test_server srv{ccj};
  run_test(srv.ioc, [&]() -> net::awaitable<void> {
    auto resp = co_await xpto::blot::async_http_get(srv.port, "/api/source");
    CHECK(resp.status == 400);
  }());
}

TEST_CASE("server_http_files_source_path_traversal") {
  auto ccj = xpto::blot::fixture_ccj("gcc-minimal");
  test_server srv{ccj};
  run_test(srv.ioc, [&]() -> net::awaitable<void> {
    auto resp = co_await xpto::blot::async_http_get(
        srv.port, "/api/source?file=../../etc/passwd");
    CHECK(resp.status == 403);
  }());
}

TEST_CASE("server_http_files_source_not_found") {
  auto ccj = xpto::blot::fixture_ccj("gcc-minimal");
  test_server srv{ccj};
  run_test(srv.ioc, [&]() -> net::awaitable<void> {
    auto resp = co_await xpto::blot::async_http_get(
        srv.port, "/api/source?file=does_not_exist.cpp");
    CHECK(resp.status == 404);
  }());
}
