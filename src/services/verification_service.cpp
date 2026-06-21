#include "services/verification_service.h"
#include "utils/platform.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>

namespace Services {

std::string VerificationService::trim_output(const std::string &s) {
  size_t start = s.find_first_not_of(" \t\n\r");
  size_t end = s.find_last_not_of(" \t\n\r");
  if (start == std::string::npos)
    return {};
  std::string out = s.substr(start, end - start + 1);
  // Keep only first line
  size_t nl = out.find('\n');
  if (nl != std::string::npos)
    out = out.substr(0, nl);
  return out;
}

std::string VerificationService::run_command(const std::string &cmd) {
  std::array<char, 256> buffer;
  std::string result;
  std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"),
                                                  pclose);
  if (!pipe)
    return "";
  while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
    result += buffer.data();
  }
  return result;
}

CheckResult VerificationService::check_file_read_write() {
  CheckResult r;
  r.name = "file_read_write";

  std::string tmp = (std::filesystem::temp_directory_path() / "cursor_verify.tmp").string();
  {
    std::ofstream out(tmp);
    out << "hello cursor";
  }
  if (!std::filesystem::exists(tmp)) {
    r.passed = false;
    r.details = "cannot create file";
    r.fix_suggestion = "check filesystem permissions on " +
                       std::filesystem::temp_directory_path().string();
    return r;
  }

  std::ifstream in(tmp);
  std::string content;
  std::getline(in, content);
  in.close();
  std::filesystem::remove(tmp);

  if (content == "hello cursor") {
    r.passed = true;
    r.details = "read/write OK";
  } else {
    r.passed = false;
    r.details = "read mismatch";
  }
  return r;
}

CheckResult VerificationService::check_grep() {
  CheckResult r;
  r.name = "grep";
  std::string out = run_command("grep --version 2>/dev/null || echo not found");
  if (out.find("not found") != std::string::npos) {
    r.passed = false;
    r.details = "not found";
    r.fix_suggestion = "install grep via package manager";
  } else {
    r.passed = true;
    r.details = trim_output(out);
  }
  return r;
}

CheckResult VerificationService::check_git() {
  CheckResult r;
  r.name = "git";
  std::string out = run_command("git --version 2>/dev/null || echo not found");
  if (out.find("not found") != std::string::npos) {
    r.passed = false;
    r.details = "not found";
    r.fix_suggestion = "install git via package manager";
  } else {
    r.passed = true;
    r.details = trim_output(out);
  }
  return r;
}

CheckResult VerificationService::check_gh() {
  CheckResult r;
  r.name = "gh";
  std::string out = run_command("gh --version 2>/dev/null || echo not found");
  if (out.find("not found") != std::string::npos) {
    r.passed = false;
    r.details = "not found";
    r.fix_suggestion = "install gh via `brew install gh` or package manager";
  } else {
    r.passed = true;
    r.details = trim_output(out);
  }
  return r;
}

CheckResult VerificationService::check_npm() {
  CheckResult r;
  r.name = "npm";
  std::string out = run_command("npm --version 2>/dev/null || echo not found");
  if (out.find("not found") != std::string::npos) {
    r.passed = false;
    r.details = "not found";
    r.fix_suggestion = "install node/npm via `brew install node` or nvm";
  } else {
    r.passed = true;
    r.details = "v" + trim_output(out);
  }
  return r;
}

CheckResult VerificationService::check_node() {
  CheckResult r;
  r.name = "node";
  std::string out =
      run_command("node --version 2>/dev/null || echo not found");
  if (out.find("not found") != std::string::npos) {
    r.passed = false;
    r.details = "not found";
    r.fix_suggestion = "install node via `brew install node` or nvm";
  } else {
    r.passed = true;
    r.details = trim_output(out);
  }
  return r;
}

CheckResult VerificationService::check_python() {
  CheckResult r;
  r.name = "python";
  std::string out =
      run_command("python3 --version 2>/dev/null || python --version 2>/dev/null || echo not found");
  if (out.find("not found") != std::string::npos) {
    r.passed = false;
    r.details = "not found";
    r.fix_suggestion = "install python via `brew install python` or package manager";
  } else {
    r.passed = true;
    r.details = trim_output(out);
  }
  return r;
}

CheckResult VerificationService::check_curl() {
  CheckResult r;
  r.name = "curl";
  std::string out =
      run_command("curl --version 2>/dev/null || echo not found");
  if (out.find("not found") != std::string::npos) {
    r.passed = false;
    r.details = "not found";
    r.fix_suggestion = "install curl via `brew install curl` or package manager";
  } else {
    r.passed = true;
    r.details = trim_output(out);
  }
  return r;
}

CheckResult VerificationService::check_cmake() {
  CheckResult r;
  r.name = "cmake";
  std::string out =
      run_command("cmake --version 2>/dev/null || echo not found");
  if (out.find("not found") != std::string::npos) {
    r.passed = false;
    r.details = "not found";
    r.fix_suggestion = "install cmake via `brew install cmake` or package manager";
  } else {
    r.passed = true;
    r.details = trim_output(out);
  }
  return r;
}

CheckResult VerificationService::check_make() {
  CheckResult r;
  r.name = "make";
  std::string out =
      run_command("make --version 2>/dev/null || echo not found");
  if (out.find("not found") != std::string::npos) {
    r.passed = false;
    r.details = "not found";
    r.fix_suggestion = "install make via `xcode-select --install` or package manager";
  } else {
    r.passed = true;
    r.details = trim_output(out);
  }
  return r;
}

CheckResult VerificationService::check_ollama() {
  CheckResult r;
  r.name = "ollama";
  std::string out =
      run_command("ollama --version 2>/dev/null || echo not found");
  if (out.find("not found") != std::string::npos) {
    r.passed = false;
    r.details = "not found";
    r.fix_suggestion = "install ollama via `brew install ollama` or ollama.ai";
  } else {
    r.passed = true;
    r.details = trim_output(out);
  }
  return r;
}

CheckResult VerificationService::check_network() {
  CheckResult r;
  r.name = "network";
  std::string out = run_command(
      "curl -s --connect-timeout 5 https://httpbin.org/get 2>/dev/null || echo no_network");
  if (out.find("no_network") != std::string::npos) {
    r.passed = false;
    r.details = "unreachable";
    r.fix_suggestion = "check internet connection";
  } else {
    r.passed = true;
    r.details = "OK";
  }
  return r;
}

CheckResult VerificationService::check_replay_dir() {
  CheckResult r;
  r.name = "replay_dir";
  std::string dir = std::string(std::getenv("HOME") ? std::getenv("HOME") : ".")
      + "/.cursor/replay";
  try {
    std::filesystem::create_directories(dir);
    std::string tmp = dir + "/.verify_write";
    { std::ofstream out(tmp); out << "ok"; }
    std::filesystem::remove(tmp);
    r.passed = true;
    r.details = dir;
  } catch (...) {
    r.passed = false;
    r.details = "cannot write to " + dir;
    r.fix_suggestion = "check permissions on ~/.cursor/replay";
  }
  return r;
}

CheckResult VerificationService::check_github_auth() {
  CheckResult r;
  r.name = "github_auth";
  std::string out =
      run_command("gh auth status 2>&1 || echo not_authenticated");
  if (out.find("not_authenticated") != std::string::npos) {
    r.passed = false;
    r.details = "not logged in";
    r.fix_suggestion = "run `gh auth login`";
  } else if (out.find("not found") != std::string::npos) {
    r.passed = false;
    r.details = "gh not installed";
    r.fix_suggestion = "install gh via `brew install gh`";
  } else {
    r.passed = true;
    r.details = "authenticated";
  }
  return r;
}

CheckResult VerificationService::check_npm_auth() {
  CheckResult r;
  r.name = "npm_auth";
  std::string out =
      run_command("npm whoami 2>&1 || echo not_authenticated");
  if (out.find("not_authenticated") != std::string::npos) {
    r.passed = false;
    r.details = "not logged in";
    r.fix_suggestion = "run `npm login`";
  } else {
    r.passed = true;
    r.details = "logged in as " + trim_output(out);
  }
  return r;
}

std::vector<CheckResult> VerificationService::run_all_checks() {
  std::vector<CheckResult> results;
  results.push_back(check_file_read_write());
  results.push_back(check_grep());
  results.push_back(check_git());
  results.push_back(check_gh());
  results.push_back(check_node());
  results.push_back(check_npm());
  results.push_back(check_python());
  results.push_back(check_curl());
  results.push_back(check_cmake());
  results.push_back(check_make());
  results.push_back(check_ollama());
  results.push_back(check_network());
  results.push_back(check_replay_dir());
  results.push_back(check_github_auth());
  results.push_back(check_npm_auth());
  return results;
}

} // namespace Services
