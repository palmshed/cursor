#include "utils/ui.h"
#include <iostream>
#include <string>

namespace Utils {

// ===== Color Codes =====
namespace Color {
CURSOR_API const std::string RESET = "\033[0m";
CURSOR_API const std::string GREEN = "\033[32m";
CURSOR_API const std::string YELLOW = "\033[33m";
CURSOR_API const std::string RED = "\033[31m";
CURSOR_API const std::string CYAN = "\033[36m";
CURSOR_API const std::string BOLD = "\033[1m";
CURSOR_API const std::string DIM = "\033[2m";
CURSOR_API const std::string PINK = "\033[38;2;255;105;180m";
} // namespace Color

// ===== Status Messages =====
void UI::print_warning(const std::string &message) {
  std::cout << Color::YELLOW << "! " << Color::RESET << message << "\n";
}

} // namespace Utils
