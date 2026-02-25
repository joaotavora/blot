#include "blot/ccj.hpp"

#include <clang/Basic/Diagnostic.h>
#include <clang/Basic/FileManager.h>
#include <clang/Basic/FileSystemOptions.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Lex/PPCallbacks.h>
#include <clang/Lex/Preprocessor.h>
#include <clang/Tooling/JSONCompilationDatabase.h>
#include <clang/Tooling/Tooling.h>
#include <fmt/std.h>

#include <filesystem>
#include <optional>
#include <string>

#include "logger.hpp"
#include "utils.hpp"

namespace xpto::blot {

namespace fs = std::filesystem;

std::optional<fs::path> find_ccj() {
  auto probe = fs::current_path() / "compile_commands.json";
  if (fs::exists(probe)) return probe;
  return std::nullopt;
}

namespace {

class find_action : public clang::PreprocessOnlyAction {
  struct finder : clang::PPCallbacks {
    find_action* action;
    finder(find_action* a) : action{a} {}

    void InclusionDirective(
        clang::SourceLocation, const clang::Token&, llvm::StringRef, bool,
        clang::CharSourceRange, clang::OptionalFileEntryRef file,
        llvm::StringRef, llvm::StringRef, const clang::Module*, bool,
        clang::SrcMgr::CharacteristicKind) override {
      if (!file || action->match_) return;
      fs::path raw{std::string{file->getName()}};
      fs::path path = (action->working_dir_ / raw).lexically_normal();

      LOG_TRACE("     saw includee '{}' -> abs '{}'", file->getName(), path);
      if (path == action->needle_) action->match_ = true;
    }
  };

 public:
  find_action(const fs::path& needle, const fs::path& working_dir, bool& match)
      : needle_{needle}, working_dir_{working_dir}, match_{match} {}

  bool BeginSourceFileAction(clang::CompilerInstance& ci) override {
    ci.getPreprocessor().addPPCallbacks(std::make_unique<finder>(this));
    return true;
  }

  void ExecuteAction() override {
    clang::Preprocessor& pp = getCompilerInstance().getPreprocessor();
    pp.EnterMainSourceFile();
    clang::Token tok{};
    // NOLINTNEXTLINE(*-do-while)
    do {
      pp.Lex(tok);
    } while (!match_ && tok.isNot(clang::tok::eof));
  }

 private:
  const fs::path& needle_;
  const fs::path& working_dir_;
  bool& match_;
};

}  // namespace

std::optional<compile_command> infer(
    const fs::path& compile_commands_path, const fs::path& source_file) {
  LOG_INFO(
      "Searching TU's including '{}' in '{}'", source_file,
      compile_commands_path);

  std::string err;
  auto db = clang::tooling::JSONCompilationDatabase::loadFromFile(
      fs::absolute(compile_commands_path).string(), err,
      clang::tooling::JSONCommandLineSyntax::AutoDetect);
  if (!db) utils::throwf("Can't load {}: {}", compile_commands_path, err);

  fs::path needle = fs::absolute(source_file).lexically_normal();

  clang::IgnoringDiagConsumer silent{};
  for (auto& cmd : db->getAllCompileCommands()) {
    LOG_DEBUG("OK: Examining entry for '{}'", cmd.Filename);

    fs::path working_dir = fs::absolute(cmd.Directory).lexically_normal();
    fs::path tu_file =
        (working_dir / fs::path{cmd.Filename}).lexically_normal();

    // PPCallbacks::InclusionDirective doesn't fire for the TU itself, so
    // handle that case directly before invoking the preprocessor.
    bool match = (tu_file == needle);

    if (!match) {
      llvm::IntrusiveRefCntPtr<clang::FileManager> fm{
        new clang::FileManager{clang::FileSystemOptions{cmd.Directory}}};
      clang::tooling::ToolInvocation inv{
        cmd.CommandLine,
        std::make_unique<find_action>(needle, working_dir, match), fm.get()};
      inv.setDiagnosticConsumer(&silent);
      inv.run();
    }

    if (match) {
      fs::path file = fs::absolute(fs::path{cmd.Directory} / cmd.Filename)
                          .lexically_normal();
      std::string command;
      for (const auto& arg : cmd.CommandLine) {
        if (!command.empty()) command += ' ';
        command += arg;
      }
      LOG_INFO(
          "SUCCESS: Found '{}', TU includer of '{}'", cmd.Filename,
          source_file);
      LOG_INFO("SUCCESS: Using compilation command '{}'", command);
      return compile_command{
        .directory = working_dir, .command = command, .file = file};
    }
  }
  return std::nullopt;
}

}  // namespace xpto::blot
