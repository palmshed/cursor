#include "services/capability_registry.h"

#include <array>
#include <cstdio>
#include <memory>

namespace Services {

bool CapabilityRegistry::check_tool(const std::string &name) {
  std::string cmd = name + " --version 2>/dev/null || echo __NOT_FOUND__";
  std::array<char, 256> buffer;
  std::string result;
  std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"),
                                                  pclose);
  if (!pipe) return false;
  while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr)
    result += buffer.data();
  return result.find("__NOT_FOUND__") == std::string::npos;
}

bool CapabilityRegistry::check_git_remote() {
  std::array<char, 256> buffer;
  std::string result;
  std::unique_ptr<FILE, decltype(&pclose)> pipe(
      popen("git remote -v 2>/dev/null | head -1", "r"), pclose);
  if (!pipe) return false;
  while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr)
    result += buffer.data();
  return !result.empty();
}

bool CapabilityRegistry::check_git_diff() {
  std::array<char, 256> buffer;
  std::string result;
  std::unique_ptr<FILE, decltype(&pclose)> pipe(
      popen("git diff --stat 2>/dev/null", "r"), pclose);
  if (!pipe) return false;
  while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr)
    result += buffer.data();
  return true; // git diff works even with empty output
}

bool CapabilityRegistry::check_git_status() {
  std::array<char, 256> buffer;
  std::string result;
  std::unique_ptr<FILE, decltype(&pclose)> pipe(
      popen("git status --porcelain 2>/dev/null", "r"), pclose);
  if (!pipe) return false;
  while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr)
    result += buffer.data();
  return true;
}

bool CapabilityRegistry::check_docker() {
  std::array<char, 256> buffer;
  std::string result;
  std::unique_ptr<FILE, decltype(&pclose)> pipe(
      popen("docker info 2>/dev/null | head -1 || echo __NOT_FOUND__", "r"),
      pclose);
  if (!pipe) return false;
  while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr)
    result += buffer.data();
  return result.find("__NOT_FOUND__") == std::string::npos &&
         !result.empty();
}

bool CapabilityRegistry::check_brew() {
  return check_tool("brew");
}

std::vector<Capability> CapabilityRegistry::list_all() {
  std::vector<Capability> caps;

  caps.push_back({"grep", check_tool("grep"),
                   "Search file contents with regex"});
  caps.push_back({"read", true,
                   "Read file contents from disk"});
  caps.push_back({"write", true,
                   "Write file contents to disk"});
  caps.push_back({"git diff", check_git_diff(),
                   "Show unstaged changes in working tree"});
  caps.push_back({"git status", check_git_status(),
                   "Show working tree status"});
  caps.push_back({"git remote", check_git_remote(),
                   "Detect remote repository"});
  caps.push_back({"build", check_tool("cmake"),
                   "Build project via cmake"});
  caps.push_back({"test", check_tool("ctest") || check_tool("make"),
                   "Run project tests"});
  caps.push_back({"github actions", check_tool("gh"),
                   "List and inspect GitHub Actions workflow runs"});
  caps.push_back({"gh pr create", check_tool("gh"),
                   "Create GitHub pull requests (requires auth)"});
  caps.push_back({"gh issue view", check_tool("gh"),
                   "View GitHub issues (requires auth)"});
  caps.push_back({"npm", check_tool("npm"),
                   "Node.js package manager"});
  caps.push_back({"npm publish", check_tool("npm"),
                   "Publish npm packages (requires auth)"});
  caps.push_back({"docker", check_docker(),
                   "Container build and run"});
  caps.push_back({"brew", check_brew(),
                   "Homebrew package manager (macOS)"});
  caps.push_back({"shell", true,
                   "Execute arbitrary shell commands"});
  caps.push_back({"web fetch", check_tool("curl"),
                   "Fetch URLs via HTTP"});
  caps.push_back({"ci investigation", check_tool("gh"),
                   "Investigate and repair CI failures"});

  return caps;
}

} // namespace Services
