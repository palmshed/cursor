#pragma once
#include "utils/config.h" // For CURSOR_API
#include <string>

namespace Utils {
// Color namespace
namespace Color {
extern CURSOR_API const std::string RESET;
extern CURSOR_API const std::string GREEN;
extern CURSOR_API const std::string YELLOW;
extern CURSOR_API const std::string RED;
extern CURSOR_API const std::string CYAN;
extern CURSOR_API const std::string BOLD;
extern CURSOR_API const std::string DIM;
extern CURSOR_API const std::string PINK;
} // namespace Color

// UI namespace (legacy)
namespace UI {
CURSOR_API void print_warning(const std::string &message);
} // namespace UI
} // namespace Utils
