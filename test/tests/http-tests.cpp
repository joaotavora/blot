#include "http-tests.hpp"

#include <doctest/doctest.h>

#include <boost/asio/io_context.hpp>
#include <boost/asio/thread_pool.hpp>
#include <cstdint>
#include <string>
#include <string_view>

#include "fixture.hpp"
#include "session.hpp"

// NOLINTBEGIN(*-avoid-capturing-lambda-coroutines)
namespace xpto::blot::tests {

struct http_fixture {
  net::io_context ioc;
  net::thread_pool pool{4};
  test_server http_server{
    pool.get_executor(), fixture_ccj("gcc-minimal"),
    fixture_dir("gcc-minimal")};

  http_fixture() { fs::current_path(fixture_dir("gcc-minimal")); }
};

TEST_CASE_FIXTURE(http_fixture, "server_http_status_fields") {
  run_ioc_test(ioc, [&]() -> net::awaitable<void> {
    auto client = connect_http(http_server.port);
    auto resp = co_await client->get("/api/status");
    REQUIRE(resp.status == 200);
    auto data = resp.json_body();
    CHECK(data.contains("tu_count"));
    CHECK(data.contains("ccj"));
    CHECK(data.contains("project_root"));
  }());
}

TEST_CASE_FIXTURE(http_fixture, "server_http_status_tu_count") {
  run_ioc_test(ioc, [&]() -> net::awaitable<void> {
    auto client = connect_http(http_server.port);
    auto resp = co_await client->get("/api/status");
    REQUIRE(resp.status == 200);
    CHECK(resp.json_body().at("tu_count").as_int64() == 1);
  }());
}

TEST_CASE_FIXTURE(http_fixture, "server_http_status_paths") {
  run_ioc_test(ioc, [&]() -> net::awaitable<void> {
    auto client = connect_http(http_server.port);
    auto resp = co_await client->get("/api/status");
    REQUIRE(resp.status == 200);
    auto data = resp.json_body();
    auto ccj_str = std::string{data.at("ccj").as_string()};
    CHECK(ccj_str.find("gcc-minimal") != std::string::npos);
    auto root_str = std::string{data.at("project_root").as_string()};
    CHECK(fs::is_directory(root_str));
  }());
}

TEST_CASE_FIXTURE(http_fixture, "server_http_files_lists_source") {
  run_ioc_test(ioc, [&]() -> net::awaitable<void> {
    auto client = connect_http(http_server.port);
    auto resp = co_await client->get("/api/files");
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

TEST_CASE_FIXTURE(http_fixture, "server_http_files_source_content") {
  run_ioc_test(ioc, [&]() -> net::awaitable<void> {
    auto client = connect_http(http_server.port);
    auto resp = co_await client->get("/api/source?file=source.cpp");
    REQUIRE(resp.status == 200);
    auto data = resp.json_body();
    CHECK(data.contains("content"));
    auto content = std::string{data.at("content").as_string()};
    CHECK(content.find("int main") != std::string::npos);
  }());
}

TEST_CASE_FIXTURE(http_fixture, "server_http_files_source_missing_param") {
  run_ioc_test(ioc, [&]() -> net::awaitable<void> {
    auto client = connect_http(http_server.port);
    auto resp = co_await client->get("/api/source");
    CHECK(resp.status == 400);
  }());
}

TEST_CASE_FIXTURE(http_fixture, "server_http_files_source_path_traversal") {
  run_ioc_test(ioc, [&]() -> net::awaitable<void> {
    auto client = connect_http(http_server.port);
    auto resp = co_await client->get("/api/source?file=../../etc/passwd");
    CHECK(resp.status == 403);
  }());
}

TEST_CASE_FIXTURE(http_fixture, "server_http_files_source_not_found") {
  run_ioc_test(ioc, [&]() -> net::awaitable<void> {
    auto client = connect_http(http_server.port);
    auto resp = co_await client->get("/api/source?file=does_not_exist.cpp");
    CHECK(resp.status == 404);
  }());
}

static net::awaitable<void> ws_send(
    ws_client* ws, int id, std::string_view method, json::object params) {
  json::object req{};
  req["jsonrpc"] = "2.0";
  req["id"] = id;
  req["method"] = method;
  req["params"] = std::move(params);
  co_await ws->send(req);
}

TEST_CASE_FIXTURE(http_fixture, "server_ws_concurrent_grab_asm") {
  testing::inflight_high_water() = 0;
  run_ioc_test(ioc, [&]() -> net::awaitable<void> {
    auto ws = co_await connect_ws(http_server.port);

    // Bootstrap: initialize + infer to get a token
    co_await ws_send(ws.get(), 1, "initialize", {});
    co_await ws->recv_response();

    co_await ws_send(ws.get(), 2, "blot/infer", {{"file", "source.cpp"}});
    auto infer_resp = co_await ws->recv_response();
    REQUIRE(!infer_resp.contains("error"));
    auto token = infer_resp.at("result").as_object().at("token").as_int64();

    // Fire N grab_asm requests back-to-back without reading any
    // response.  The server posts+co_spawns each as a separate
    // coroutine on the thread pool, so all N compilations should run
    // concurrently.
    constexpr int N = 4;
    for (int i = 0; i < N; ++i)
      co_await ws_send(ws.get(), 10 + i, "blot/grab_asm", {{"token", token}});

    // Collect all N responses in whatever order they arrive
    auto resps = co_await ws->recv_n_responses(N);
    REQUIRE(static_cast<int>(resps.size()) == N);
    for (int i = 0; i < N; ++i) {
      int64_t id{10 + i};
      REQUIRE(resps.count(id));
      CHECK(!resps.at(id).contains("error"));
      CHECK(resps.at(id).at("result").as_object().contains("token"));
    }
  }());
  // Must have observed at least 2 handlers in flight simultaneously;
  // anything less means the server serialized them.
  CHECK(testing::inflight_high_water() >= 2);
}

TEST_CASE_FIXTURE(http_fixture, "server_ws_grabasm_cache_token") {
  run_ioc_test(ioc, [&]() -> net::awaitable<void> {
    auto ws = co_await connect_ws(http_server.port);

    co_await ws_send(ws.get(), 1, "initialize", {});
    co_await ws->recv_response();

    co_await ws_send(ws.get(), 2, "blot/infer", {{"file", "source.cpp"}});
    auto infer_resp = co_await ws->recv_response();
    REQUIRE(!infer_resp.contains("error"));
    auto tok = infer_resp.at("result").as_object().at("token").as_int64();

    co_await ws_send(ws.get(), 3, "blot/grab_asm", {{"token", tok}});
    auto cold = co_await ws->recv_response();
    REQUIRE(!cold.contains("error"));
    CHECK(cold.at("result").as_object().at("cached") == false);

    co_await ws_send(ws.get(), 4, "blot/grab_asm", {{"token", tok}});
    auto warm = co_await ws->recv_response();
    REQUIRE(!warm.contains("error"));
    CHECK(
        std::string{warm.at("result").as_object().at("cached").as_string()} ==
        "token");
  }());
}

TEST_CASE_FIXTURE(http_fixture, "server_ws_grabasm_cache_other") {
  run_ioc_test(ioc, [&]() -> net::awaitable<void> {
    auto ws = co_await connect_ws(http_server.port);

    co_await ws_send(ws.get(), 1, "initialize", {});
    co_await ws->recv_response();

    co_await ws_send(ws.get(), 2, "blot/infer", {{"file", "source.cpp"}});
    auto infer_resp = co_await ws->recv_response();
    REQUIRE(!infer_resp.contains("error"));
    auto& infer_result = infer_resp.at("result").as_object();
    auto tok = infer_result.at("token").as_int64();

    co_await ws_send(ws.get(), 3, "blot/grab_asm", {{"token", tok}});
    auto cold = co_await ws->recv_response();
    REQUIRE(!cold.contains("error"));
    CHECK(cold.at("result").as_object().at("cached") == false);

    // Re-request via inference object — should hit asm_cache_2
    co_await ws_send(ws.get(), 4, "blot/grab_asm",
                    {{"inference", infer_result.at("inference").as_object()}});
    auto warm = co_await ws->recv_response();
    REQUIRE(!warm.contains("error"));
    auto& warm_result = warm.at("result").as_object();
    CHECK(std::string{warm_result.at("cached").as_string()} == "other");
    CHECK(warm_result.at("token").as_int64() == tok);
  }());
}

TEST_CASE_FIXTURE(http_fixture, "server_ws_second_cycle_cache_hits") {
  // Two independent infer+grab_asm+annotate cycles on the same file.
  // Cycle 1 is fully cold.  Cycle 2 demonstrates what is and isn't cached:
  //   - infer:     cold again (no content-keyed infer cache)
  //   - grab_asm:  "other" hit via asm_cache_2 (command+dir keyed), returns tok1
  //   - annotate:  "token" hit via annotate_cache_1, riding on tok1 from above
  run_ioc_test(ioc, [&]() -> net::awaitable<void> {
    auto ws = co_await connect_ws(http_server.port);

    co_await ws_send(ws.get(), 1, "initialize", {});
    co_await ws->recv_response();

    // Cycle 1 — cold
    co_await ws_send(ws.get(), 2, "blot/infer", {{"file", "source.cpp"}});
    auto infer1 = co_await ws->recv_response();
    REQUIRE(!infer1.contains("error"));
    auto& infer1_result = infer1.at("result").as_object();
    auto tok1 = infer1_result.at("token").as_int64();
    CHECK(infer1_result.at("cached") == false);

    co_await ws_send(ws.get(), 3, "blot/grab_asm", {{"token", tok1}});
    auto asm1 = co_await ws->recv_response();
    REQUIRE(!asm1.contains("error"));
    CHECK(asm1.at("result").as_object().at("cached") == false);

    co_await ws_send(ws.get(), 4, "blot/annotate", {{"token", tok1}});
    auto ann1 = co_await ws->recv_response();
    REQUIRE(!ann1.contains("error"));
    CHECK(ann1.at("result").as_object().at("cached") == false);

    // Cycle 2 — same work, independent token
    co_await ws_send(ws.get(), 5, "blot/infer", {{"file", "source.cpp"}});
    auto infer2 = co_await ws->recv_response();
    REQUIRE(!infer2.contains("error"));
    CHECK(infer2.at("result").as_object().at("cached") == false);  // no infer cache_2

    // grab_asm via inference object hits asm_cache_2 → "other", returns tok1
    co_await ws_send(ws.get(), 6, "blot/grab_asm",
                    {{"inference", infer1_result.at("inference").as_object()}});
    auto asm2 = co_await ws->recv_response();
    REQUIRE(!asm2.contains("error"));
    auto& asm2_result = asm2.at("result").as_object();
    CHECK(std::string{asm2_result.at("cached").as_string()} == "other");
    auto tok_returned = asm2_result.at("token").as_int64();
    CHECK(tok_returned == tok1);

    // annotate has no content-keyed cache; it rides on tok1 from the "other" hit
    co_await ws_send(ws.get(), 7, "blot/annotate", {{"token", tok_returned}});
    auto ann2 = co_await ws->recv_response();
    REQUIRE(!ann2.contains("error"));
    CHECK(std::string{ann2.at("result").as_object().at("cached").as_string()} ==
          "token");
  }());
}

}  // namespace xpto::blot::tests
// NOLINTEND(*-avoid-capturing-lambda-coroutines)
