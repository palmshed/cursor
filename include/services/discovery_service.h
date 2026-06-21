#pragma once
#include <string>
#include <vector>

namespace Services {

struct ProjectDiscovery {
  std::string project_type;
  int source_file_count;
  int service_count;
  bool has_tests;
  std::vector<std::string> ci_systems;
  std::vector<std::string> package_managers;
  std::vector<std::string> relevant_files;
  std::vector<std::string> impact_areas;
};

class DiscoveryService {
public:
  static ProjectDiscovery scan(const std::string &project_root,
                                const std::string &query_hint = "");
  static bool is_substantial_task(const std::string &input);
  static void invalidate_cache();

private:
  static ProjectDiscovery do_scan(const std::string &project_root,
                                   const std::string &query_hint);
  static int count_files(const std::string &dir,
                         const std::string &extension);
  static std::vector<std::string> find_relevant_files(
      const std::string &project_root, const std::string &query);
  static std::string detect_project_type(const std::string &project_root);
  static std::vector<std::string> detect_ci(const std::string &project_root);
  static std::vector<std::string> detect_package_managers(
      const std::string &project_root);
  static bool has_cache();
  static ProjectDiscovery &cached();
  static std::string cached_root();
};

} // namespace Services
