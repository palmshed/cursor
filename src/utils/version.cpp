#include "version.h"
#include <filesystem>
#include <iostream>
#include <string>

#include <curl/curl.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

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

std::string get_exe_path() {
  try {
    return std::filesystem::canonical("/proc/self/exc").string();
  } catch (...) {
  }
#ifdef __APPLE__
  char buf[1024];
  uint32_t size = sizeof(buf);
  if (_NSGetExecutablePath(buf, &size) == 0) {
    return std::filesystem::canonical(buf).string();
  }
#endif
  return {};
}

std::string fetch_latest_version() {
  CURL *curl = curl_easy_init();
  if (!curl)
    return {};

  std::string response;
  curl_easy_setopt(curl, CURLOPT_URL,
                   "https://api.github.com/repos/bniladridas/cursor/releases/"
                   "latest");
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "cursor-agent");
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_to_string);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);

  CURLcode res = curl_easy_perform(curl);
  curl_easy_cleanup(curl);

  if (res != CURLE_OK)
    return {};

  auto tag_pos = response.find("\"tag_name\":\"");
  if (tag_pos == std::string::npos)
    return {};
  tag_pos += 13;
  auto tag_end = response.find("\"", tag_pos);
  return response.substr(tag_pos, tag_end - tag_pos);
}

} // namespace

namespace Version {

const char *get_version() { return cursor_version_string; }

const char *get_build_info() {
  static std::string build_info;
  if (build_info.empty()) {
    build_info = std::string(__DATE__) + " " + __TIME__;
  }
  return build_info.c_str();
}

void print_version_info() {
  std::cout << "Cursor v" << get_version() << "\n";
  std::cout << "Built: " << get_build_info() << "\n";
}

std::string check_update() {
  std::string latest = fetch_latest_version();
  if (latest.empty() || latest == get_version())
    return {};
  return latest;
}

bool download_and_install(const std::string &version) {
  std::string binary_name = get_platform_binary_name();
  std::string url =
      "https://github.com/bniladridas/cursor/releases/download/" + version +
      "/" + binary_name;

  std::string tmp = "/tmp/cursor-update-" + version;
#ifdef _WIN32
  tmp += ".exe";
#endif

  std::cout << "Downloading v" << version << "...\n";

  FILE *fp = fopen(tmp.c_str(), "wb");
  if (!fp) {
    std::cerr << "Failed to create temp file\n";
    return false;
  }

  CURL *curl = curl_easy_init();
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "cursor-agent");
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_to_file);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);

  CURLcode res = curl_easy_perform(curl);
  curl_easy_cleanup(curl);
  fclose(fp);

  if (res != CURLE_OK) {
    std::cerr << "Download failed: " << curl_easy_strerror(res) << "\n";
    std::filesystem::remove(tmp);
    return false;
  }

  std::filesystem::permissions(tmp,
                               std::filesystem::perms::owner_exec |
                                   std::filesystem::perms::owner_read |
                                   std::filesystem::perms::owner_write,
                               std::filesystem::perm_options::add);

  std::string exe = get_exe_path();
  if (exe.empty()) {
    std::cerr << "Downloaded to: " << tmp << "\nManually replace binary.\n";
    return false;
  }

  std::string backup = exe + ".bak";
  std::error_code ec;
  std::filesystem::rename(exe, backup, ec);
  if (ec) {
    std::cerr << "Failed to backup: " << ec.message() << "\n";
    std::filesystem::remove(tmp);
    return false;
  }

  std::filesystem::rename(tmp, exe, ec);
  if (ec) {
    std::cerr << "Failed to install: " << ec.message() << "\n";
    std::filesystem::rename(backup, exe);
    std::filesystem::remove(tmp);
    return false;
  }

  std::cout << "Updated to v" << version << " successfully!\n";
  return true;
}

} // namespace Version
