#include "services/dependency_service.h"
#include "services/file_service.h"
#include "utils/validation.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>

namespace fs = std::filesystem;

namespace Services {

bool DependencyService::is_source_file(const std::string &ext) {
  static const std::set<std::string> source_exts = {
    ".cpp", ".cxx", ".cc", ".c", ".h", ".hpp", ".hxx", ".hh",
    ".ts", ".tsx", ".js", ".jsx", ".mjs", ".cjs",
    ".py", ".rs", ".go", ".java", ".kt", ".swift",
    ".rb", ".php", ".scala", ".clj"
  };
  return source_exts.count(ext) > 0;
}

std::vector<std::string> DependencyService::extract_imports(
    const std::string &file_path) {
  std::vector<std::string> imports;
  std::ifstream f(file_path);
  if (!f.is_open()) return imports;

  std::string line;
  std::string ext = fs::path(file_path).extension().string();

  while (std::getline(f, line)) {
    // C/C++: #include "..." or #include <...>
    if (ext == ".cpp" || ext == ".cxx" || ext == ".cc" || ext == ".c" ||
        ext == ".h" || ext == ".hpp" || ext == ".hxx" || ext == ".hh") {
      std::smatch m;
      if (std::regex_search(line, m, std::regex(R"(#include\s+["<]([^">]+)[">])"))) {
        imports.push_back(m[1].str());
      }
    }
    // Python: import X or from X import Y
    else if (ext == ".py") {
      std::smatch m;
      if (std::regex_search(line, m, std::regex(R"(^\s*(?:import|from)\s+(\S+))"))) {
        imports.push_back(m[1].str());
      }
    }
    // TypeScript/JavaScript: import ... from "..." or require("...")
    else if (ext == ".ts" || ext == ".tsx" || ext == ".js" || ext == ".jsx" || ext == ".mjs") {
      std::smatch m;
      if (std::regex_search(line, m, std::regex(R"(import\s+(?:\{[^}]*\}\s+from\s+)?['"]([^'"]+)['"])")) ||
          std::regex_search(line, m, std::regex(R"(require\s*\(\s*['"]([^'"]+)['"]\s*\))"))) {
        imports.push_back(m[1].str());
      }
    }
    // Rust: use X or mod X
    else if (ext == ".rs") {
      std::smatch m;
      if (std::regex_search(line, m, std::regex(R"(^\s*(?:use|mod)\s+(\S+))"))) {
        imports.push_back(m[1].str());
      }
    }
    // Go: import "X" or import ( "X" )
    else if (ext == ".go") {
      std::smatch m;
      if (std::regex_search(line, m, std::regex("^\\s*import\\s+\"([^\"]+)\""))) {
        imports.push_back(m[1].str());
      }
    }
    // Java: import X.Y.Z
    else if (ext == ".java") {
      std::smatch m;
      if (std::regex_search(line, m, std::regex(R"(^\s*import\s+(\S+);)"))) {
        imports.push_back(m[1].str());
      }
    }
  }

  return imports;
}

std::vector<DependencyNode> DependencyService::build_graph(
    const std::string &root_dir) {
  std::vector<DependencyNode> graph;

  for (auto &entry : fs::recursive_directory_iterator(root_dir)) {
    if (!entry.is_regular_file()) continue;
    std::string ext = entry.path().extension().string();
    if (!is_source_file(ext)) continue;

    std::string file = fs::relative(entry.path(), root_dir).string();
    if (file.find("node_modules") != std::string::npos ||
        file.find(".git") != std::string::npos ||
        file.find("build") != std::string::npos)
      continue;

    DependencyNode node;
    node.file = file;
    node.imports = extract_imports(entry.path().string());
    graph.push_back(std::move(node));
  }

  // Build reverse index: who imports what
  auto &nodes = graph;
  std::map<std::string, std::set<std::string>> reverse;
  for (auto &n : nodes) {
    for (auto &imp : n.imports) {
      reverse[imp].insert(n.file);
    }
  }

  for (auto &n : nodes) {
    for (auto &imp : n.imports) {
      auto it = reverse.find(imp);
      if (it != reverse.end()) {
        for (auto &importer : it->second) {
          if (importer != n.file) {
            n.imported_by.push_back(importer);
          }
        }
      }
    }
  }

  return graph;
}

std::string DependencyService::format_graph_text(
    const std::vector<DependencyNode> &graph,
    const std::string &focus) {
  std::ostringstream out;

  if (focus.empty()) {
    out << "Dependency graph: " << graph.size() << " files\n\n";
    for (auto &n : graph) {
      out << n.file << "\n";
      for (auto &imp : n.imports) {
        out << "  ├── " << imp << "\n";
      }
      if (!n.imported_by.empty()) {
        out << "  (imported by " << n.imported_by.size() << " files)\n";
      }
    }
  } else {
    // Show focused view: a specific file and its neighbors
    for (auto &n : graph) {
      if (n.file.find(focus) != std::string::npos) {
        out << n.file << "\n";
        out << "  Imports:\n";
        for (auto &imp : n.imports) {
          out << "    • " << imp << "\n";
        }
        out << "  Imported by:\n";
        for (auto &imp : n.imported_by) {
          out << "    • " << imp << "\n";
        }
        out << "\n";
      }
    }
    if (out.str().empty()) {
      out << "No dependencies found for \"" << focus << "\"\n";
    }
  }

  return out.str();
}

std::string DependencyService::format_graph_dot(
    const std::vector<DependencyNode> &graph,
    const std::string &focus) {
  std::ostringstream out;
  out << "digraph Dependencies {\n";
  out << "  rankdir=LR;\n";
  out << "  node [shape=box, style=rounded];\n\n";

  for (auto &n : graph) {
    if (!focus.empty() && n.file.find(focus) == std::string::npos)
      continue;
    for (auto &imp : n.imports) {
      out << "  \"" << n.file << "\" -> \"" << imp << "\";\n";
    }
  }

  out << "}\n";
  return out.str();
}

std::vector<std::string> DependencyService::find_dependents(
    const std::string &root_dir, const std::string &target_file) {
  auto graph = build_graph(root_dir);
  std::vector<std::string> result;

  for (auto &n : graph) {
    for (auto &imp : n.imports) {
      if (target_file.find(imp) != std::string::npos ||
          imp.find(target_file) != std::string::npos) {
        result.push_back(n.file);
        break;
      }
    }
  }

  return result;
}

} // namespace Services
