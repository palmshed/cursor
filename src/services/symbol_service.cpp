#include "services/symbol_service.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace Services {

bool SymbolService::is_source_ext(const std::string &ext) {
  static const std::set<std::string> exts = {
    ".cpp", ".cxx", ".cc", ".c", ".h", ".hpp", ".hxx", ".hh",
    ".ts", ".tsx", ".js", ".jsx", ".mjs",
    ".py", ".rs", ".go", ".java", ".kt", ".swift",
    ".rb", ".php"
  };
  return exts.count(ext) > 0;
}

std::vector<std::string> SymbolService::get_source_files(
    const std::string &root_dir, const std::string &sub_path) {
  std::vector<std::string> files;
  std::string search_dir = sub_path.empty()
      ? root_dir
      : (fs::path(root_dir) / sub_path).string();

  if (!fs::exists(search_dir)) return files;

  for (auto &entry : fs::recursive_directory_iterator(search_dir)) {
    if (!entry.is_regular_file()) continue;
    std::string ext = entry.path().extension().string();
    if (!is_source_ext(ext)) continue;
    std::string rel = fs::relative(entry.path(), root_dir).string();
    if (rel.find("node_modules") != std::string::npos ||
        rel.find(".git") != std::string::npos ||
        rel.find("build") != std::string::npos)
      continue;
    files.push_back(entry.path().string());
  }

  return files;
}

std::string SymbolService::read_line(const std::string &file, int line) {
  std::ifstream f(file);
  if (!f.is_open()) return "";
  std::string l;
  for (int i = 0; i <= line && std::getline(f, l); i++) {
    if (i == line - 1) {
      // Trim trailing whitespace
      while (!l.empty() && (l.back() == ' ' || l.back() == '\t' || l.back() == '\r'))
        l.pop_back();
      return l;
    }
  }
  return "";
}

std::vector<SymbolDefinition> SymbolService::find_symbols(
    const std::string &root_dir, const std::string &symbol_name) {
  std::vector<SymbolDefinition> results;
  auto files = get_source_files(root_dir);

  for (auto &fp : files) {
    std::ifstream f(fp);
    if (!f.is_open()) continue;
    std::string line;
    int line_num = 0;

    while (std::getline(f, line)) {
      line_num++;
      if (symbol_name.empty() ||
          line.find(symbol_name) != std::string::npos) {

        SymbolDefinition def;
        def.file = fs::relative(fp, root_dir).string();
        def.line = line_num;

        // Try to determine kind
        std::smatch m;
        if (std::regex_search(line, m, std::regex(R"(\bclass\s+(\w+))")))
          { def.name = m[1].str(); def.kind = "class"; }
        else if (std::regex_search(line, m, std::regex(R"(\bstruct\s+(\w+))")))
          { def.name = m[1].str(); def.kind = "struct"; }
        else if (std::regex_search(line, m, std::regex(R"(\b(?:void|int|bool|char|float|double|string|auto|const)\s+\**\s*(\w+)\s*\()")))
          { def.name = m[1].str(); def.kind = "function"; }
        else if (std::regex_search(line, m, std::regex(R"(^\s*(?:pub\s+)?fn\s+(\w+))")))
          { def.name = m[1].str(); def.kind = "function"; }
        else if (std::regex_search(line, m, std::regex(R"(^\s*(?:export\s+)?(?:function|const)\s+(\w+))")))
          { def.name = m[1].str(); def.kind = "function"; }
        else if (std::regex_search(line, m, std::regex(R"(^\s*def\s+(\w+))")))
          { def.name = m[1].str(); def.kind = "function"; }
        else if (std::regex_search(line, m, std::regex(R"(^\s*func\s+(\w+))")))
          { def.name = m[1].str(); def.kind = "function"; }
        else if (std::regex_search(line, m, std::regex(R"(^\s*(?:public|private|protected)?\s*(?:static\s+)?(?:[\w:]+\s+)?(\w+)\s*\([^)]*\)\s*(?:const\s*)?\{)")))
          { def.name = m[1].str(); def.kind = "method"; }
        else
          { def.name = symbol_name; def.kind = "reference"; }

        def.signature = line;
        if (!symbol_name.empty() && def.name != symbol_name) continue;
        results.push_back(def);
      }
    }
  }

  return results;
}

std::vector<SymbolOccurrence> SymbolService::find_references(
    const std::string &root_dir, const std::string &symbol_name) {
  std::vector<SymbolOccurrence> refs;
  auto files = get_source_files(root_dir);

  for (auto &fp : files) {
    std::ifstream f(fp);
    if (!f.is_open()) continue;
    std::string line;
    int line_num = 0;

    while (std::getline(f, line)) {
      line_num++;
      size_t col = 0;
      while ((col = line.find(symbol_name, col)) != std::string::npos) {
        // Skip if it's the definition (line containing class/struct/def/fn etc.)
        bool is_def = false;
        std::smatch m;
        if (std::regex_search(line, m, std::regex(R"(\b(?:class|struct|def|fn|func|function)\s+)" + symbol_name))) {
          is_def = true;
        }
        if (!is_def) {
          SymbolOccurrence o;
          o.file = fs::relative(fp, root_dir).string();
          o.line = line_num;
          o.column = static_cast<int>(col + 1);
          o.context = line.substr(0, std::min<size_t>(80, line.size()));
          refs.push_back(o);
        }
        col += symbol_name.size();
      }
    }
  }

  return refs;
}

std::vector<SymbolDefinition> SymbolService::list_functions(
    const std::string &root_dir, const std::string &file_path) {
  auto files = file_path.empty()
      ? get_source_files(root_dir)
      : get_source_files(root_dir, file_path);

  std::vector<SymbolDefinition> funcs;
  std::set<std::string> seen;

  for (auto &fp : files) {
    std::ifstream f(fp);
    if (!f.is_open()) continue;
    std::string line;
    int line_num = 0;

    while (std::getline(f, line)) {
      line_num++;
      std::smatch m;

      // C/C++ function definitions
      if (std::regex_search(line, m, std::regex(
              R"((?:static\s+)?(?:inline\s+)?(?:virtual\s+)?[\w:]+(?:\s*\*+)?\s+(\w+)\s*\()"))) {
        std::string name = m[1].str();
        if (name != "if" && name != "while" && name != "for" && name != "switch" &&
            name != "return" && name != "catch" && name.find("~") != 0) {
          if (seen.insert(name).second) {
            funcs.push_back({name, "function", fs::relative(fp, root_dir).string(), line_num, line});
          }
        }
      }
      // TS/JS function/const arrow
      else if (std::regex_search(line, m, std::regex(
                   R"((?:export\s+)?(?:function|const)\s+(\w+))"))) {
        if (seen.insert(m[1].str()).second) {
          funcs.push_back({m[1].str(), "function", fs::relative(fp, root_dir).string(), line_num, line});
        }
      }
      // Python def
      else if (std::regex_search(line, m, std::regex(R"(^\s*def\s+(\w+))"))) {
        if (seen.insert(m[1].str()).second) {
          funcs.push_back({m[1].str(), "function", fs::relative(fp, root_dir).string(), line_num, line});
        }
      }
      // Rust fn
      else if (std::regex_search(line, m, std::regex(R"(^\s*(?:pub\s+)?fn\s+(\w+))"))) {
        if (seen.insert(m[1].str()).second) {
          funcs.push_back({m[1].str(), "function", fs::relative(fp, root_dir).string(), line_num, line});
        }
      }
      // Go func
      else if (std::regex_search(line, m, std::regex(R"(^\s*func\s+(?:\w+\.)?(\w+))"))) {
        if (seen.insert(m[1].str()).second) {
          funcs.push_back({m[1].str(), "function", fs::relative(fp, root_dir).string(), line_num, line});
        }
      }
    }
  }

  return funcs;
}

std::vector<SymbolDefinition> SymbolService::list_classes(
    const std::string &root_dir, const std::string &file_path) {
  auto files = file_path.empty()
      ? get_source_files(root_dir)
      : get_source_files(root_dir, file_path);

  std::vector<SymbolDefinition> classes;

  for (auto &fp : files) {
    std::ifstream f(fp);
    if (!f.is_open()) continue;
    std::string line;
    int line_num = 0;

    while (std::getline(f, line)) {
      line_num++;
      std::smatch m;

      if (std::regex_search(line, m, std::regex(R"(\bclass\s+(\w+))"))) {
        classes.push_back({m[1].str(), "class", fs::relative(fp, root_dir).string(), line_num, line});
      } else if (std::regex_search(line, m, std::regex(R"(\bstruct\s+(\w+))"))) {
        classes.push_back({m[1].str(), "struct", fs::relative(fp, root_dir).string(), line_num, line});
      } else if (std::regex_search(line, m, std::regex(R"(^\s*(?:export\s+)?(?:default\s+)?class\s+(\w+))"))) {
        classes.push_back({m[1].str(), "class", fs::relative(fp, root_dir).string(), line_num, line});
      }
    }
  }

  return classes;
}

std::string SymbolService::format_symbols(
    const std::vector<SymbolDefinition> &symbols) {
  std::ostringstream out;
  if (symbols.empty()) {
    return "No symbols found.\n";
  }

  std::string last_file;
  for (auto &s : symbols) {
    if (s.file != last_file) {
      out << "\n" << s.file << ":\n";
      last_file = s.file;
    }
    out << "  " << s.line << ":";
    if (!s.kind.empty()) out << " [" << s.kind << "]";
    out << " " << s.signature << "\n";
  }

  return out.str();
}

std::string SymbolService::format_references(
    const std::vector<SymbolOccurrence> &refs) {
  std::ostringstream out;
  if (refs.empty()) {
    return "No references found.\n";
  }

  for (auto &r : refs) {
    out << r.file << ":" << r.line << ":" << r.column << "  "
        << r.context << "\n";
  }

  return out.str();
}

} // namespace Services
