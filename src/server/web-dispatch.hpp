#pragma once

#include <boost/beast/http.hpp>
#include <filesystem>

namespace xpto::blot {
using request_t = boost::beast::http::request<boost::beast::http::string_body>;
using response_t =
    boost::beast::http::response<boost::beast::http::string_body>;
response_t dispatch(
    const request_t& req, const std::filesystem::path& ccj_path,
    const std::filesystem::path& project_root);
}  // namespace xpto::blot
