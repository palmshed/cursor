#include "services/discovery_service.h"
#include "utils/platform.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>

namespace Services {

namespace {

static std::mutex cache_mutex;
static ProjectDiscovery cached_discovery;
static std::string cached_root_path;
static bool cache_valid = false;

std::string run_cmd(const std::string &cmd) {
  std::array<char, 512> buffer;
  std::string result;
  std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"),
                                                  pclose);
  if (!pipe)
    return "";
  while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe.get()) != nullptr) {
    result += buffer.data();
  }
  return result;
}

std::string to_lower(std::string s) {
  for (auto &c : s)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

} // namespace

bool DiscoveryService::has_cache() { return cache_valid; }

ProjectDiscovery &DiscoveryService::cached() { return cached_discovery; }

std::string DiscoveryService::cached_root() { return cached_root_path; }

void DiscoveryService::invalidate_cache() {
  std::lock_guard<std::mutex> lock(cache_mutex);
  cache_valid = false;
  cached_root_path.clear();
}

std::string DiscoveryService::detect_project_type(const std::string &root) {
  std::string cmake_file = root + "/CMakeLists.txt";
  if (std::filesystem::exists(cmake_file)) {
    std::ifstream f(cmake_file);
    std::string line;
    std::string std_ver;
    while (std::getline(f, line)) {
      if (line.find("CMAKE_CXX_STANDARD") != std::string::npos) {
        std_ver = line;
      }
    }
    if (!std_ver.empty()) {
      auto pos = std_ver.find_last_of("0123456789");
      if (pos != std::string::npos) {
        return "C++" + std_ver.substr(pos) + " / CMake";
      }
    }
    return "C++ / CMake";
  }
  if (std::filesystem::exists(root + "/package.json"))
    return "Node.js / npm";
  if (std::filesystem::exists(root + "/Cargo.toml"))
    return "Rust / Cargo";
  if (std::filesystem::exists(root + "/go.mod"))
    return "Go";
  if (std::filesystem::exists(root + "/pyproject.toml") ||
      std::filesystem::exists(root + "/setup.py"))
    return "Python";
  return "Unknown";
}

std::vector<std::string>
DiscoveryService::detect_ci(const std::string &root) {
  std::vector<std::string> ci;
  if (std::filesystem::exists(root + "/.github/workflows"))
    ci.push_back("GitHub Actions");
  if (std::filesystem::exists(root + "/.gitlab-ci.yml"))
    ci.push_back("GitLab CI");
  if (std::filesystem::exists(root + "/Jenkinsfile"))
    ci.push_back("Jenkins");
  if (std::filesystem::exists(root + "/.circleci/config.yml"))
    ci.push_back("CircleCI");
  return ci;
}

std::vector<std::string>
DiscoveryService::detect_package_managers(const std::string &root) {
  std::vector<std::string> pm;
  if (std::filesystem::exists(root + "/package.json"))
    pm.push_back("npm");
  if (std::filesystem::exists(root + "/CMakeLists.txt")) {
    std::ifstream f(root + "/CMakeLists.txt");
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    if (content.find("FetchContent") != std::string::npos)
      pm.push_back("CMake FetchContent");
    if (content.find("find_package") != std::string::npos)
      pm.push_back("CMake find_package");
    if (content.find("vcpkg") != std::string::npos)
      pm.push_back("vcpkg");
  }
  // Check for Homebrew (macOS)
  std::string brew_check = run_cmd("which brew 2>/dev/null");
  if (!brew_check.empty())
    pm.push_back("Homebrew");
  return pm;
}

int DiscoveryService::count_files(const std::string &dir,
                                  const std::string &extension) {
  int count = 0;
  if (!std::filesystem::exists(dir))
    return 0;
  try {
    for (auto &entry : std::filesystem::recursive_directory_iterator(dir)) {
      if (entry.is_regular_file()) {
        std::string path = entry.path().string();
        if (extension.empty() ||
            (path.size() >= extension.size() &&
                path.substr(path.size() - extension.size()) == extension)) {
          count++;
        }
      }
    }
  } catch (...) {
  }
  return count;
}

std::vector<std::string>
DiscoveryService::find_relevant_files(const std::string &project_root,
                                       const std::string &query) {
  if (query.empty())
    return {};

  // Extract significant words from query
  std::istringstream iss(to_lower(query));
  std::string word;
  std::vector<std::string> terms;
  const std::vector<std::string> skip = {
      "add", "implement", "refactor", "fix",  "migrate",
      "create", "remove", "a",         "an",  "the",
      "for",   "to",       "in",       "of",  "with",
      "and",   "or",       "new",      "from"};

  while (iss >> word) {
    word.erase(std::remove_if(word.begin(), word.end(),
                               [](unsigned char c) { return std::ispunct(c); }),
               word.end());
    if (word.size() >= 3 && std::find(skip.begin(), skip.end(), word) == skip.end()) {
      terms.push_back(word);
    }
  }

  if (terms.empty())
    return {};

  // Grep for those terms across the project
  std::string grep_cmd =
      "cd \"" + project_root +
      "\" 2>/dev/null && grep -r -l --include=\"*.cpp\" --include=\"*.h\" ";
  for (size_t i = 0; i < terms.size(); i++) {
    if (i > 0)
      grep_cmd += " -e ";
    else
      grep_cmd += "-e ";
    grep_cmd += terms[i];
  }
  grep_cmd += " 2>/dev/null | head -10";

  std::string grep_out = run_cmd(grep_cmd);
  std::vector<std::string> files;
  std::istringstream out_stream(grep_out);
  std::string line;
  while (std::getline(out_stream, line)) {
    if (!line.empty())
      files.push_back(line);
  }
  return files;
}

ProjectDiscovery DiscoveryService::do_scan(const std::string &project_root,
                                            const std::string &query_hint) {
  ProjectDiscovery d;
  std::string root =
      std::filesystem::exists(project_root) ? project_root : ".";

  d.project_type = detect_project_type(root);
  d.source_file_count =
      count_files(root + "/src", ".cpp") + count_files(root, ".cpp");
  d.service_count = count_files(root + "/src/services", ".cpp");
  d.has_tests = std::filesystem::exists(root + "/tests") &&
                count_files(root + "/tests", ".cpp") > 0;
  d.ci_systems = detect_ci(root);
  d.package_managers = detect_package_managers(root);

  // Only scan relevant files if there's a query hint
  if (!query_hint.empty()) {
    d.relevant_files = find_relevant_files(root, query_hint);
    if (d.relevant_files.size() > 8)
      d.relevant_files.resize(8);

    // Build impact areas from what was found
    for (auto &f : d.relevant_files) {
      if (f.find("main.cpp") != std::string::npos)
        d.impact_areas.push_back("entry point");
      else if (f.find("command_router") != std::string::npos)
        d.impact_areas.push_back("command routing");
      else if (f.find("session") != std::string::npos)
        d.impact_areas.push_back("session loop");
      else if (f.find("replay") != std::string::npos)
        d.impact_areas.push_back("replay/logging");
      else if (f.find("service") != std::string::npos)
        d.impact_areas.push_back("services");
      else if (f.find("ui_manage") != std::string::npos || f.find("/ui/") != std::string::npos || f.find("\\ui\\") != std::string::npos)
        d.impact_areas.push_back("UI layer");
      else if (f.find("test") != std::string::npos)
        d.impact_areas.push_back("tests");
    }
    // Deduplicate
    std::sort(d.impact_areas.begin(), d.impact_areas.end());
    d.impact_areas.erase(
        std::unique(d.impact_areas.begin(), d.impact_areas.end()),
        d.impact_areas.end());
  }

  return d;
}

ProjectDiscovery DiscoveryService::scan(const std::string &project_root,
                                         const std::string &query_hint) {
  std::string root =
      std::filesystem::exists(project_root) ? project_root : ".";
  std::string abs_root = std::filesystem::absolute(root).string();

  {
    std::lock_guard<std::mutex> lock(cache_mutex);
    if (cache_valid && cached_root_path == abs_root && query_hint.empty()) {
      return cached_discovery;
    }
  }

  ProjectDiscovery d = do_scan(root, query_hint);

  // Cache only non-query-specific scans
  if (query_hint.empty()) {
    std::lock_guard<std::mutex> lock(cache_mutex);
    cached_discovery = d;
    cached_root_path = abs_root;
    cache_valid = true;
  }

  return d;
}

bool DiscoveryService::is_substantial_task(const std::string &input) {
  std::string lower = to_lower(input);
  std::vector<std::string> triggers = {
      "add ",    "implement ", "refactor ", "fix ",
      "migrate ", "create ",   "remove ",   "update ",
      "upgrade ", "delete ",   "rename ",   "extract ",
      "build ",   "install ",   "setup ",    "configure "};

  for (auto &t : triggers) {
    if (lower.find(t) == 0 || lower.find(t) != std::string::npos) {
      return true;
    }
  }

  return false;
}

} // namespace Services
