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

std::pair<std::string, std::string> get_release_asset_names(const std::string &version) {
#ifdef _WIN32
  return {"cursor_v" + version + "_windows_amd64.zip", "cursor-windows.exe"};
#elif defined(__APPLE__)
  return {"cursor_v" + version + "_darwin_arm64.tar.gz", "cursor-macos"};
#else
  return {"cursor_v" + version + "_linux_amd64.tar.gz", "cursor-linux"};
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
  if (latest.empty() || latest == ('v' + std::string(get_version())))
    return {};
  if (latest.size() > 1 && latest[0] == 'v')
    latest.erase(0, 1);
  return latest;
}

bool download_and_install(const std::string &version) {
  auto [archive, binary_name] = get_release_asset_names(version);
  std::string url =
      "https://github.com/bniladridas/cursor/releases/download/v" + version +
      "/" + archive;

  std::string tmp = "/tmp/cursor-update-" + version;
  std::string tmpDir = "/tmp/cursor-update-extract-" + version;
#ifdef _WIN32
  tmp += ".zip";
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

  std::filesystem::create_directories(tmpDir, ec);
  if (ec) {
    std::cerr << "Failed to create extract dir: " << ec.message() << "\n";
    std::filesystem::rename(backup, exe);
    std::filesystem::remove(tmp);
    return false;
  }

  {
#ifdef _WIN32
    std::string cmd =
        "powershell -NoProfile -Command \"Expand-Archive -LiteralPath '" +
        tmp + "' -DestinationPath '" + tmpDir + "' -Force\"";
#else
    std::string cmd = "tar -xzf \"" + tmp + "\" -C \"" + tmpDir + "\"";
#endif
    int rc = std::system(cmd.c_str());
    if (rc != 0) {
      std::cerr << "Extract failed\n";
      std::filesystem::rename(backup, exe);
      std::filesystem::remove(tmp);
      std::filesystem::remove_all(tmpDir);
      return false;
    }
  }

  std::string extracted = tmpDir + "/" + binary_name;
  if (!std::filesystem::exists(extracted)) {
    std::cerr << "Extracted binary not found: " << extracted << "\n";
    std::filesystem::rename(backup, exe);
    std::filesystem::remove(tmp);
    std::filesystem::remove_all(tmpDir);
    return false;
  }

  std::filesystem::rename(extracted, exe, ec);
  if (ec) {
    std::cerr << "Failed to install: " << ec.message() << "\n";
    std::filesystem::rename(backup, exe);
    std::filesystem::remove(tmp);
    std::filesystem::remove_all(tmpDir);
    return false;
  }

  std::filesystem::remove(tmp);
  std::filesystem::remove_all(tmpDir);
  std::cout << "Updated to v" << version << " successfully!\n";
  return true;
}

} // namespace Version
