#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

namespace Services {

struct DependencyNode {
  std::string file;
  std::vector<std::string> imports;
  std::vector<std::string> imported_by;
};

class DependencyService {
public:
  static std::vector<DependencyNode> build_graph(const std::string &root_dir);
  static std::string format_graph_text(const std::vector<DependencyNode> &graph,
                                       const std::string &focus = "");
  static std::string format_graph_dot(const std::vector<DependencyNode> &graph,
                                      const std::string &focus = "");
  static std::vector<std::string> find_dependents(
      const std::string &root_dir, const std::string &target_file);

private:
  static std::vector<std::string> extract_imports(const std::string &file_path);
  static bool is_source_file(const std::string &ext);
  static std::map<std::string, std::set<std::string>> build_reverse_index(
      const std::vector<DependencyNode> &graph);
};

} // namespace Services
