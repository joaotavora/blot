#include "http-tests.hpp"
#include "fixture.hpp"
#include "session.hpp"

#include <doctest/doctest.h>

#include <boost/asio/io_context.hpp>
#include <boost/asio/thread_pool.hpp>
#include <cstdint>
#include <string>
#include <string_view>

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

TEST_CASE_FIXTURE(http_fixture, "server_ws_concurrent_grab_asm") {
  testing::reset_grabasm_max_concurrent();
  run_ioc_test(ioc, [&]() -> net::awaitable<void> {
    auto ws = co_await connect_ws(http_server.port);

    auto send_req = [&ws](
                        int id, std::string_view method,
                        json::object params) -> net::awaitable<void> {
      json::object req{};
      req["jsonrpc"] = "2.0";
      req["id"] = id;
      req["method"] = method;
      req["params"] = std::move(params);
      co_await ws->send(req);
    };

    // Bootstrap: initialize + infer to get a token
    co_await send_req(1, "initialize", {});
    co_await ws->recv_response();

    json::object infer_p{};
    infer_p["file"] = "source.cpp";
    co_await send_req(2, "blot/infer", infer_p);
    auto infer_resp = co_await ws->recv_response();
    REQUIRE(!infer_resp.contains("error"));
    auto token = infer_resp.at("result").as_object().at("token").as_int64();

    // Fire N grab_asm requests back-to-back without reading any response.
    // The server co_spawns each as a separate coroutine on the thread pool,
    // so all N compilations should run concurrently.
    constexpr int N = 4;
    for (int i = 0; i < N; ++i) {
      json::object p{};
      p["token"] = token;
      co_await send_req(10 + i, "blot/grab_asm", p);
    }

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
  CHECK(testing::grabasm_max_concurrent() >= 2);
}

}  // namespace xpto::blot::tests
// NOLINTEND(*-avoid-capturing-lambda-coroutines)
