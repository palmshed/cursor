#pragma once
#include "utils/config.h" // For CURSOR_API
#include <atomic>
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

// UI namespace
namespace UI {
// Core functions
CURSOR_API void print_logo();
CURSOR_API void print_help();
CURSOR_API void print_enterprise_status();
CURSOR_API void spinner(const std::string &message, int duration_ms);
CURSOR_API void spinner(std::atomic<bool> &done); // For threaded spinner

// Status messages
CURSOR_API void print_success(const std::string &message);
CURSOR_API void print_error(const std::string &message);
CURSOR_API void print_warning(const std::string &message);
CURSOR_API void print_info(const std::string &message);

// Utility functions
CURSOR_API void print_divider();
CURSOR_API void print_quick_help();
CURSOR_API void print_system_info(const std::string &mode,
                                     const std::string &model);
CURSOR_API void print_ready_interface(const std::string &mode,
                                         const std::string &model);
CURSOR_API std::string prompt_user(const std::string &prompt_text);
} // namespace UI
} // namespace Utils
