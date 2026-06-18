#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include <curl/curl.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#include "agent.h"
#include "memory_manager.h"
#include "services/ai_service.h"
#include "utils/config.h"
#include "utils/ui.h"
#include "version.h"

namespace {

size_t write_to_string(void *ptr, size_t size, size_t nmemb, void *userdata) {
  auto *str = static_cast<std::string *>(userdata);
  str->append(static_cast<char *>(ptr), size * nmemb);
  return size * nmemb;
}

size_t write_to_file(void *ptr, size_t size, size_t nmemb, void *userdata) {
  auto *file = static_cast<FILE *>(userdata);
  return fwrite(ptr, size, nmemb, file);
}

std::string get_platform_binary_name() {
#ifdef _WIN32
  return "cursor-windows.exe";
#elif defined(__APPLE__)
  return "cursor-macos";
#else
  return "cursor-linux";
#endif
}

std::string get_current_exe_path() {
  return std::filesystem::canonical("/proc/self/exc").string();
}

bool self_update() {
  std::cout << "Checking for updates...\n";

  CURL *curl = curl_easy_init();
  if (!curl) {
    std::cerr << "Failed to initialize HTTP client\n";
    return false;
  }

  std::string api_response;
  curl_easy_setopt(curl, CURLOPT_URL,
                   "https://api.github.com/repos/bniladridas/cursor/releases/"
                   "latest");
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "cursor-agent");
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_to_string);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &api_response);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

  CURLcode res = curl_easy_perform(curl);
  curl_easy_cleanup(curl);

  if (res != CURLE_OK) {
    std::cerr << "Failed to check for updates: " << curl_easy_strerror(res)
              << "\n";
    return false;
  }

  auto tag_pos = api_response.find("\"tag_name\":\"");
  if (tag_pos == std::string::npos) {
    std::cerr << "Failed to parse release info\n";
    return false;
  }
  tag_pos += 13;
  auto tag_end = api_response.find("\"", tag_pos);
  std::string latest_version = api_response.substr(tag_pos, tag_end - tag_pos);

  std::string current = Version::get_version();
  if (latest_version == current) {
    std::cout << "Already up to date (v" << current << ")\n";
    return true;
  }

  std::cout << "Update available: v" << current << " -> v" << latest_version
            << "\n";
  std::cout << "Downloading...\n";

  std::string binary_name = get_platform_binary_name();
  std::string download_url =
      "https://github.com/bniladridas/cursor/releases/download/" +
      latest_version + "/" + binary_name;

  std::string tmp_path = "/tmp/cursor-update-" + latest_version;
#ifdef _WIN32
  tmp_path += ".exe";
#endif

  FILE *fp = fopen(tmp_path.c_str(), "wb");
  if (!fp) {
    std::cerr << "Failed to create temporary file: " << tmp_path << "\n";
    return false;
  }

  curl = curl_easy_init();
  curl_easy_setopt(curl, CURLOPT_URL, download_url.c_str());
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "cursor-agent");
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_to_file);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);

  res = curl_easy_perform(curl);
  curl_easy_cleanup(curl);
  fclose(fp);

  if (res != CURLE_OK) {
    std::cerr << "Failed to download update: " << curl_easy_strerror(res)
              << "\n";
    std::filesystem::remove(tmp_path);
    return false;
  }

  std::filesystem::permissions(tmp_path,
                               std::filesystem::perms::owner_exec |
                                   std::filesystem::perms::owner_read |
                                   std::filesystem::perms::owner_write,
                               std::filesystem::perm_options::add);

  std::string exe_path;
  try {
    exe_path = std::filesystem::canonical("/proc/self/exc").string();
  } catch (...) {
#ifdef __APPLE__
    char buf[1024];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) == 0) {
      exe_path = std::filesystem::canonical(buf).string();
    }
#endif
  }

  if (exe_path.empty()) {
    std::cerr << "Cannot determine executable path. Downloaded to: " << tmp_path
              << "\nManually replace your binary.\n";
    return false;
  }

  std::string backup = exe_path + ".bak";
  std::error_code ec;
  std::filesystem::rename(exe_path, backup, ec);
  if (ec) {
    std::cerr << "Failed to backup current binary: " << ec.message() << "\n";
    std::filesystem::remove(tmp_path);
    return false;
  }

  std::filesystem::rename(tmp_path, exe_path, ec);
  if (ec) {
    std::cerr << "Failed to install update: " << ec.message() << "\n";
    std::filesystem::rename(backup, exe_path);
    std::filesystem::remove(tmp_path);
    return false;
  }

  std::cout << "Updated to v" << latest_version << " successfully!\n";
  std::cout << "Restart to use the new version.\n";
  return true;
}

} // namespace

int main(int argc, char *argv[]) {
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--version" || arg == "-v") {
      Version::print_version_info();
      return 0;
    }
    if (arg == "--help" || arg == "-h") {
      std::cout << "Usage: cursor [OPTIONS]\n\n"
                << "Options:\n"
                << "  -v, --version    Print version info and exit\n"
                << "  -h, --help       Show this help and exit\n"
                << "  --update         Update to latest release\n";
      return 0;
    }
    if (arg == "--update") {
      return self_update() ? 0 : 1;
    }
  }

  try {
    Utils::Config::load_environment();
    Utils::UI::print_logo();
    Core::Agent agent;
    agent.run();
    std::cout << "Agent run completed" << std::endl;
  } catch (const std::exception &e) {
    std::cerr << Utils::Color::RED << "Fatal error: " << e.what()
              << Utils::Color::RESET << std::endl;
    return 1;
  }

  return 0;
}
