#pragma once

// Define DLL export/import macros for Windows
#if defined(_WIN32) || defined(_WIN64)
#ifndef CURSOR_API
#ifdef CURSOR_LIBRARY
#define CURSOR_API __declspec(dllexport)
#else
#define CURSOR_API __declspec(dllimport)
#endif
#endif

// Disable warning about STL types in the interface
#ifndef _HAS_CXX17
#define _HAS_CXX17 1
#endif

#ifndef _SILENCE_CXX17_ALLOCATOR_VOID_DEPRECATION_WARNING
#define _SILENCE_CXX17_ALLOCATOR_VOID_DEPRECATION_WARNING
#endif

#pragma warning(disable                                                        \
                : 4251) // Disable warning about STL types in the interface
#pragma warning(                                                               \
    disable                                                                    \
    : 4275) // Disable warning about non dll-interface class used as base

// Enable secure CRT functions
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

// Disable min/max macros from windows.h
#ifndef NOMINMAX
#define NOMINMAX
#endif
#else
#ifndef CURSOR_API
#define CURSOR_API __attribute__((visibility("default")))
#endif

// On non-Windows, we can use visibility attributes for better control
#ifdef __GNUC__
#ifndef CURSOR_LOCAL
#define CURSOR_LOCAL __attribute__((visibility("hidden")))
#endif
#else
#ifndef CURSOR_LOCAL
#define CURSOR_LOCAL
#endif
#endif
#endif

// Disable exporting of STL templates
#ifdef _MSC_VER
#define CURSOR_NO_EXPORT_TEMPLATE template class CURSOR_API
#else
#define CURSOR_NO_EXPORT_TEMPLATE extern template class CURSOR_API
#endif

#include <string>

namespace Utils::Config {
CURSOR_API void load_environment(const std::string &filename = ".env");
CURSOR_API std::string get_env_var(const std::string &key,
                                      const std::string &default_value = "");
CURSOR_API bool has_env_var(const std::string &key);
} // namespace Utils::Config
