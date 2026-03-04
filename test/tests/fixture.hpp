#include <filesystem>
#include <string_view>

namespace xpto::blot::tests {

namespace fs = std::filesystem;

inline fs::path fixture_dir(std::string_view name) {
  return fs::path{TEST_FIXTURE_DIR} / name;
}

inline fs::path fixture_ccj(std::string_view name) {
  return fixture_dir(name) / "compile_commands.json";
}
}
