#pragma once

#include <string>
#include <vector>

namespace Services {

struct SymbolOccurrence {
  std::string file;
  int line;
  int column;
  std::string context;
};

struct SymbolDefinition {
  std::string name;
  std::string kind; // "function", "class", "struct", "variable", "method", "type"
  std::string file;
  int line;
  std::string signature;
};

class SymbolService {
public:
  static std::vector<SymbolDefinition> find_symbols(
      const std::string &root_dir,
      const std::string &symbol_name = "");

  static std::vector<SymbolOccurrence> find_references(
      const std::string &root_dir, const std::string &symbol_name);

  static std::vector<SymbolDefinition> list_functions(
      const std::string &root_dir, const std::string &file_path = "");

  static std::vector<SymbolDefinition> list_classes(
      const std::string &root_dir, const std::string &file_path = "");

  static std::string format_symbols(
      const std::vector<SymbolDefinition> &symbols);

  static std::string format_references(
      const std::vector<SymbolOccurrence> &refs);

private:
  static std::vector<std::string> get_source_files(
      const std::string &root_dir, const std::string &sub_path = "");
  static std::string read_line(const std::string &file, int line);
  static bool is_source_ext(const std::string &ext);
};

} // namespace Services
