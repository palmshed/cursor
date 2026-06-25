cmake_minimum_required(VERSION 3.14)

# Try to get version from git tag
find_package(Git QUIET)
set(PROJECT_VERSION "0.0.0")
if(GIT_FOUND)
  execute_process(
    COMMAND ${GIT_EXECUTABLE} describe --tags --abbrev=0
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_TAG
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
  if(GIT_TAG)
    string(REGEX REPLACE "^v" "" GIT_TAG_NO_V ${GIT_TAG})
    string(REGEX MATCH "^[0-9]+(\\.[0-9]+)*" MATCHED_VERSION "${GIT_TAG_NO_V}")
    if(MATCHED_VERSION)
      set(PROJECT_VERSION ${MATCHED_VERSION})
    endif()
  endif()
endif()

# Fallback: extract from source dir name (e.g., cursor-0.1.8 from tarball)
if(PROJECT_VERSION STREQUAL "0.0.0")
  get_filename_component(SOURCE_DIR_NAME ${CMAKE_SOURCE_DIR} NAME)
  string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" EXTRACTED_VERSION ${SOURCE_DIR_NAME})
  if(EXTRACTED_VERSION)
    set(PROJECT_VERSION ${EXTRACTED_VERSION})
  endif()
endif()

project(cursor VERSION ${PROJECT_VERSION} LANGUAGES CXX)

# Configure version header
configure_file(include/utils/version.h.in include/version.h @ONLY)

# Include FetchContent for downloading dependencies
include(FetchContent)

# Disable CMake's built-in pkg-config integration on macOS to avoid stale
# Xcode SDK paths from CMake 3.31+'s native pkg-config support. These paths
# don't exist on CI runners but are returned for system libraries (zlib,
# curl, etc.), causing "Imported target includes non-existent path" errors
# when FetchContent builds (CPR, curl) find and use the same targets.
# The FindPkgConfig module (find_package + pkg_check_modules) still works.
if(APPLE)
    set(CMAKE_FIND_USE_PKG_CONFIG OFF)
endif()

# Determine active macOS SDK path as fallback for sanitizing stale paths
if(APPLE AND NOT CMAKE_OSX_SYSROOT)
    execute_process(
        COMMAND xcrun --show-sdk-path
        OUTPUT_VARIABLE CMAKE_OSX_SYSROOT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
endif()

# Macro to sanitize lists of paths by verifying existence and correcting stale SDK paths
macro(sanitize_path_list _list_var)
    if(APPLE AND ${_list_var})
        set(_sanitized_list "")
        foreach(_path ${${_list_var}})
            # Check if this is a default system include path that breaks libc++ search order
            if(_path MATCHES "usr/include/?$")
                message(STATUS "Sanitizer: Removing system include path '${_path}' to avoid breaking compiler search order")
            elseif(EXISTS "${_path}")
                list(APPEND _sanitized_list "${_path}")
            else()
                # Correct stale/dead Xcode SDK paths from cached pkg-config files
                if(_path MATCHES ".*MacOSX\\.sdk/(.*)")
                    set(_suffix "${CMAKE_MATCH_1}")
                    set(_fixed_path "${CMAKE_OSX_SYSROOT}/${_suffix}")
                    if(_fixed_path MATCHES "usr/include/?$")
                        message(STATUS "Sanitizer: Skipping corrected system path '${_fixed_path}'")
                    elseif(EXISTS "${_fixed_path}")
                        list(APPEND _sanitized_list "${_fixed_path}")
                        message(STATUS "Sanitizer: Corrected stale SDK path '${_path}' -> '${_fixed_path}'")
                    else()
                        message(STATUS "Sanitizer: Removing non-existent SDK path '${_path}'")
                    endif()
                else()
                    # If it's a file path (starts with / or has common library extension) and doesn't exist, remove it
                    if(_path MATCHES "^/" OR _path MATCHES "\\.(tbd|dylib|a|framework)$")
                        message(STATUS "Sanitizer: Removing non-existent path/library '${_path}'")
                    else()
                        # Keep target names, raw linker flags, etc.
                        list(APPEND _sanitized_list "${_path}")
                    endif()
                endif()
            endif()
        endforeach()
        set(${_list_var} "${_sanitized_list}")
    endif()
endmacro()

# Macro to sanitize imported targets on macOS by removing or correcting non-existent
# include directories, system include directories, link libraries, and imported locations.
macro(sanitize_target _target)
    if(APPLE AND TARGET ${_target})
        set(_properties
            INTERFACE_INCLUDE_DIRECTORIES
            INTERFACE_SYSTEM_INCLUDE_DIRECTORIES
            INTERFACE_LINK_LIBRARIES
            IMPORTED_LOCATION
            IMPORTED_LOCATION_RELEASE
            IMPORTED_LOCATION_DEBUG
            IMPORTED_LOCATION_NOCONFIG
        )

        foreach(_prop ${_properties})
            get_target_property(_val ${_target} ${_prop})
            if(_val)
                set(_temp_val "${_val}")
                sanitize_path_list(_temp_val)
                set_property(TARGET ${_target} PROPERTY ${_prop} "${_temp_val}")
            endif()
        endforeach()
        unset(_properties)
        unset(_prop)
        unset(_val)
        unset(_temp_val)
    endif()
endmacro()

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Generate compile_commands.json for clang-tidy
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build configuration
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

if(MSVC)
    add_compile_options(/W4 /WX /MP /EHsc /utf-8)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL$<$<CONFIG:Debug>:DebugDLL>")
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fprofile-arcs -ftest-coverage")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fprofile-arcs -ftest-coverage")
    endif()
endif()

# Clang-Tidy configuration (disabled due to libpqxx compatibility issues)
# find_program(CLANG_TIDY_EXE NAMES clang-tidy)
# if(CLANG_TIDY_EXE)
#     set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE};--config-file=${CMAKE_SOURCE_DIR}/.clang-tidy")
#     message(STATUS "Clang-Tidy found: ${CLANG_TIDY_EXE}")
# else()
#     message(STATUS "Clang-Tidy not found. Please install it via your system's package manager (e.g., 'brew install llvm' or 'apt install clang-tidy').")
# endif()

# Find dependencies
find_package(Threads REQUIRED)

# Find zlib with platform-specific logic, fallback to FetchContent
set(zlib_is_found FALSE)
if(APPLE)
    # On macOS, prefer pkg-config as it's more reliable with Homebrew
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(ZLIB QUIET IMPORTED_TARGET zlib)
        if(ZLIB_FOUND)
            message(STATUS "Found zlib using pkg-config: ${ZLIB_LINK_LIBRARIES}")
            set(ZLIB_TARGET PkgConfig::ZLIB)
            set(zlib_is_found TRUE)
            sanitize_target(PkgConfig::ZLIB)
        endif()
    endif()

    # Also find ZLIB via standard find_package to create/configure ZLIB::ZLIB target
    # which is expected by dependencies like curl/cpr when they call find_package(ZLIB).
    find_package(ZLIB QUIET)
    if(TARGET ZLIB::ZLIB)
        sanitize_target(ZLIB::ZLIB)
    endif()
    sanitize_path_list(ZLIB_INCLUDE_DIRS)
elseif(WIN32)
    # On Windows, first try to find zlib via vcpkg
    find_package(ZLIB QUIET)
    if(ZLIB_FOUND)
        message(STATUS "Found zlib via vcpkg: ${ZLIB_LIBRARIES}")
        # Create an imported target for zlib if it doesn't exist
        if(NOT TARGET zlib_imported)
            add_library(zlib_imported INTERFACE IMPORTED)
            target_link_libraries(zlib_imported INTERFACE ${ZLIB_LIBRARIES})
            target_include_directories(zlib_imported INTERFACE ${ZLIB_INCLUDE_DIRS})
        endif()
        set(ZLIB_TARGET zlib_imported)
        set(zlib_is_found TRUE)
    endif()
else()
    # Linux/other Unix-like systems
    find_package(ZLIB QUIET)
    if(ZLIB_FOUND)
        set(ZLIB_TARGET ZLIB::ZLIB)
        set(zlib_is_found TRUE)
    else()
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(ZLIB QUIET IMPORTED_TARGET zlib)
            if(ZLIB_FOUND)
                set(ZLIB_TARGET PkgConfig::ZLIB)
                set(zlib_is_found TRUE)
            endif()
        endif()
    endif()
endif()

if(NOT zlib_is_found)
    message(STATUS "zlib not found, downloading using FetchContent...")
    FetchContent_Declare(
        zlib
        GIT_REPOSITORY https://github.com/madler/zlib.git
        GIT_TAG v1.3
    )
    FetchContent_MakeAvailable(zlib)
    set(ZLIB_TARGET zlib)
    set(zlib_is_found TRUE)
endif()

# Find libcurl
find_package(CURL REQUIRED)

# Sanitize CURL target's include directories and library paths on macOS
if(APPLE)
    sanitize_target(CURL::libcurl)
endif()

# Find OpenSSL, fallback to FetchContent
set(openssl_is_found FALSE)
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Build architectures for macOS" FORCE)
    if(DEFINED ENV{CI} OR DEFINED ENV{GITHUB_ACTIONS})
        set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "Minimum macOS version" FORCE)
    else()
        execute_process(
            COMMAND sw_vers -productVersion
            OUTPUT_VARIABLE MACOS_PRODUCT_VERSION
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        string(REGEX MATCH "^[0-9]+\\.[0-9]+" MACOS_DEPLOYMENT_VERSION "${MACOS_PRODUCT_VERSION}")
        if(MACOS_DEPLOYMENT_VERSION)
            set(CMAKE_OSX_DEPLOYMENT_TARGET "${MACOS_DEPLOYMENT_VERSION}" CACHE STRING "Minimum macOS version" FORCE)
        else()
            set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "Minimum macOS version" FORCE)
        endif()
    endif()

    # Use Homebrew's OpenSSL on macOS/arm64
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64")
        # Try to find Homebrew's OpenSSL
        find_program(HOMEBREW NAMES brew)
        if(HOMEBREW)
            execute_process(
                COMMAND ${HOMEBREW} --prefix openssl@3
                OUTPUT_VARIABLE OPENSSL_ROOT_DIR
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
            if(EXISTS ${OPENSSL_ROOT_DIR})
                set(OPENSSL_ROOT_DIR ${OPENSSL_ROOT_DIR} CACHE PATH "OpenSSL root directory")
                find_package(OpenSSL QUIET)
                if(OpenSSL_FOUND)
                    set(openssl_is_found TRUE)
                    message(STATUS "Using Homebrew's OpenSSL: ${OPENSSL_INCLUDE_DIR}")
                endif()
            endif()

            # Also find Homebrew's curl if available
            execute_process(
                COMMAND ${HOMEBREW} --prefix curl
                OUTPUT_VARIABLE CURL_ROOT_DIR
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
            if(EXISTS ${CURL_ROOT_DIR})
                set(CURL_INCLUDE_DIRS ${CURL_ROOT_DIR}/include)
                set(CURL_LIBRARIES ${CURL_ROOT_DIR}/lib/libcurl.dylib)
                message(STATUS "Using Homebrew's curl: ${CURL_ROOT_DIR}")
            endif()
        endif()
    endif()

    if(NOT openssl_is_found)
        find_package(OpenSSL QUIET)
        if(OpenSSL_FOUND)
            set(openssl_is_found TRUE)
        endif()
    endif()
else()
    # On other platforms, use system OpenSSL
    find_package(OpenSSL QUIET)
    if(OpenSSL_FOUND)
        set(openssl_is_found TRUE)
    endif()


endif()

if(NOT openssl_is_found)
    message(STATUS "OpenSSL not found, downloading using FetchContent...")
    FetchContent_Declare(
        openssl
        GIT_REPOSITORY https://github.com/openssl/openssl.git
        GIT_TAG openssl-3.2.1
    )
    set(OPENSSL_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(openssl)
    add_library(OpenSSL::SSL ALIAS ssl)
    add_library(OpenSSL::Crypto ALIAS crypto)
    set(OPENSSL_INCLUDE_DIR ${openssl_SOURCE_DIR}/include)
    set(openssl_is_found TRUE)
endif()

# Add compiler and linker flags for macOS
if(APPLE)
    add_compile_options(-march=armv8.4-a+fp16+rcpc+dotprod+crypto)
    add_link_options(-Wl,-no_weak_imports)
    set(CMAKE_INSTALL_RPATH "@executable_path/../lib")
    set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
    set(CMAKE_MACOSX_RPATH TRUE)
endif()

# Make sure we have the curl include directory
if(NOT CURL_INCLUDE_DIRS)
    set(CURL_INCLUDE_DIRS ${CURL_INCLUDE_DIR})
endif()

# Sanitize include directories variables to avoid adding default system paths on macOS
if(APPLE)
    sanitize_path_list(CURL_INCLUDE_DIRS)
    sanitize_path_list(OPENSSL_INCLUDE_DIR)
endif()

message(STATUS "Using curl: ${CURL_INCLUDE_DIRS} : ${CURL_LIBRARIES}")

# Find nlohmann_json
find_package(nlohmann_json 3.11.2 QUIET)
if(NOT nlohmann_json_FOUND)
    message(STATUS "nlohmann_json not found, downloading using FetchContent...")
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.2
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

# Find libpqxx (optional)
find_library(PQXX_LIBRARY pqxx)
find_path(PQXX_INCLUDE_DIR pqxx/pqxx)
if(PQXX_LIBRARY AND PQXX_INCLUDE_DIR)
    set(HAVE_PQXX TRUE)
    message(STATUS "Found libpqxx: ${PQXX_LIBRARY}")
else()
    set(HAVE_PQXX FALSE)
    message(STATUS "libpqxx not found. Database support will be disabled.")
endif()

# Find CPR - try to find the package first
set(CPR_MIN_VERSION "1.10.0")
set(CPR_FOUND FALSE)

# First try to find CPR using config mode.
# Skip on macOS — Homebrew cpr bottles often reference stale Xcode SDK
# paths that don't exist on the runner, causing CMake 3.31+ to error.
# Build from source via FetchContent on macOS for reliability.
if(NOT APPLE)
    find_package(cpr ${CPR_MIN_VERSION} QUIET)
endif()

if(TARGET cpr::cpr)
    message(STATUS "Found CPR (config mode): ${cpr_VERSION}")
    set(CPR_FOUND TRUE)
elseif(NOT APPLE)
    # If not found (non-macOS), try to find the library directly
    find_library(CPR_LIBRARY NAMES cpr)
    find_path(CPR_INCLUDE_DIR NAMES cpr/cpr.h)

    if(CPR_LIBRARY AND CPR_INCLUDE_DIR)
        message(STATUS "Found CPR (library): ${CPR_LIBRARY}")
        add_library(cpr::cpr INTERFACE IMPORTED)
        target_include_directories(cpr::cpr INTERFACE ${CPR_INCLUDE_DIR})
        target_link_libraries(cpr::cpr INTERFACE
            ${CPR_LIBRARY}
            CURL::libcurl
            OpenSSL::SSL
            OpenSSL::Crypto
            ${ZLIB_TARGET}
        )
        set(CPR_FOUND TRUE)
    endif()
endif()

if(NOT CPR_FOUND)
    # If still not found, try to fetch it using FetchContent
    message(STATUS "CPR not found, downloading using FetchContent...")
    include(FetchContent)

    FetchContent_Declare(
        cpr
        GIT_REPOSITORY https://github.com/libcpr/cpr.git
        GIT_TAG 1.10.5  # Updated to a more recent version
    )

    # Prevent building tests and examples
    set(CPR_BUILD_TESTS OFF CACHE BOOL "")
    set(CPR_BUILD_TESTS_SSL OFF CACHE BOOL "")
    set(CPR_BUILD_TESTS_VERBOSE OFF CACHE BOOL "")

    # Build CPR as static library for better linking compatibility
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "")

    # Add compiler flags to suppress deprecation warnings
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-deprecated-declarations")
    endif()

    # Make sure we're using a compatible version of CPR
    set(CPR_CURL_NOSIGNAL ON CACHE BOOL "")

    # Make the directory to suppress warnings about missing directory
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/_deps/cpr-build")

    FetchContent_MakeAvailable(cpr)

    # Add the include directory if it exists
    if(EXISTS "${CMAKE_BINARY_DIR}/_deps/cpr-src/include")
        target_include_directories(cpr PUBLIC "${CMAKE_BINARY_DIR}/_deps/cpr-src/include")
    endif()

    if(TARGET cpr::cpr)
        message(STATUS "CPR downloaded and built from source")
        set(CPR_FOUND TRUE)
    else()
        message(FATAL_ERROR "Failed to find or build CPR. Please install it manually or check your network connection.")
    endif()
endif()
option(CURSOR_BUILD_TESTS "Build tests" ON)

if(CURSOR_BUILD_TESTS)
    enable_testing()

    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

    if(NOT TARGET gtest)
        message(STATUS "Downloading and building Google Test...")
        FetchContent_Declare(
            googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG v1.14.0
            GIT_SHALLOW TRUE
        )

         set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
         set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
         set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
         set(BUILD_GMOCK ON CACHE BOOL "" FORCE)
         set(BUILD_GTEST ON CACHE BOOL "" FORCE)

         FetchContent_MakeAvailable(googletest)

        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            foreach(GTEST_TARGET gtest gtest_main gmock gmock_main)
                if(TARGET ${GTEST_TARGET})
                    target_compile_options(${GTEST_TARGET} PRIVATE -Wno-character-conversion)
                endif()
            endforeach()
        endif()

        if(NOT TARGET gtest_all_tests)
            add_library(gtest_all_tests INTERFACE)
            target_link_libraries(gtest_all_tests
                INTERFACE
                    GTest::GTest
                    GTest::Main
            )
        endif()
    endif()
endif()

# Only set up the cpr target if we're building it ourselves
if(TARGET cpr)
    # Ensure proper linking on all platforms for our local cpr build
    target_link_libraries(cpr PRIVATE
        OpenSSL::SSL
        OpenSSL::Crypto
        ${ZLIB_TARGET}  # Use the appropriate zlib target for this platform
    )
endif()

# On macOS, add additional settings
if(APPLE)
    # Set minimum deployment target (already set above)
    # Use system version to avoid linker warnings with Homebrew libraries

    # Set rpath
    set(CMAKE_INSTALL_RPATH "@executable_path/../lib")
    set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
    set(CMAKE_MACOSX_RPATH TRUE)

    # Add compiler and linker flags to all targets
    add_compile_options(
        -march=armv8.4-a+fp16+rcpc+dotprod+crypto
        -Wno-deprecated-declarations
    )

    add_link_options(
        -Wl,-no_weak_imports
    )

    # Add these flags to the main target as well
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=armv8.4-a+fp16+rcpc+dotprod+crypto -Wno-deprecated-declarations")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-no_weak_imports")

    # Add OpenSSL include directories to all targets
    include_directories(${OPENSSL_INCLUDE_DIR})

    # Add OpenSSL definitions to all targets
    add_compile_definitions(
        CURL_USE_OPENSSL=1
        CURL_USE_DARWINSSL=0
        CURL_USE_SECTRANSP=0
    )
endif()

# Include directories
include_directories(include)

# Source files organized by modules
set(APP_SOURCES
    src/app/menu.cpp
    src/app/startup.cpp
    src/app/command_router.cpp
    src/app/session.cpp
)

set(UI_SOURCES
    src/ui/ui_manager.cpp
)

set(CORE_SOURCES
    src/agent.cpp
    src/core/session_state.cpp
    src/core/model_catalog.cpp
)

set(SERVICE_SOURCES
    src/services/command_service.cpp
    src/services/file_service.cpp
    src/services/web_service.cpp
    src/services/ai_service.cpp
    src/services/git_service.cpp
    src/services/github_service.cpp
    src/services/codebase_service.cpp
    src/services/multi_file_service.cpp
    src/services/context_service.cpp
    src/services/checkpoint_service.cpp
    src/services/mcp_service.cpp
    src/services/theme_service.cpp
    src/services/auth_service.cpp
    src/services/error_service.cpp
    src/services/sandbox_service.cpp
    src/services/database_service.cpp
    src/services/replay_service.cpp
    src/services/verification_service.cpp
    src/services/self_test_service.cpp
    src/services/discovery_service.cpp
    src/services/planning_service.cpp
    src/services/ci_investigation_service.cpp
    src/services/execution_engine.cpp
    src/services/capability_registry.cpp
    src/services/workflow_benchmark_service.cpp
    src/services/confidence_service.cpp
    src/services/dashboard_service.cpp
    src/services/dependency_service.cpp
    src/services/find_service.cpp
    src/services/symbol_service.cpp
)

set(DIAGNOSTICS_SOURCES
    src/diagnostics/diagnostics.cpp
)

set(UTILS_SOURCES
    src/utils/ui.cpp
    src/utils/config.cpp
    src/utils/memory_utils.cpp
    src/utils/version.cpp
    src/utils/validation.cpp
)

set(DATA_SOURCES
    src/memory_manager.cpp
)

set(ALL_SOURCES
    ${CORE_SOURCES}
    ${APP_SOURCES}
    ${UI_SOURCES}
    ${SERVICE_SOURCES}
    ${DIAGNOSTICS_SOURCES}
    ${UTILS_SOURCES}
    ${DATA_SOURCES}
)

# Create library with all sources
add_library(cursor_lib ${ALL_SOURCES})
target_include_directories(cursor_lib PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:include>
    ${CURL_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
    ${ZLIB_INCLUDE_DIRS}
    $<$<BOOL:${HAVE_PQXX}>:${PQXX_INCLUDE_DIR}>
)

# Set compile definitions for symbol visibility
target_compile_definitions(cursor_lib PRIVATE CURSOR_LIBRARY $<$<BOOL:${HAVE_PQXX}>:HAVE_PQXX>)
set_target_properties(cursor_lib PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

# Set platform-specific compiler flags
if(MSVC)
    target_compile_options(cursor_lib PRIVATE /W4 /WX /EHsc /utf-8)
    target_compile_definitions(cursor_lib PRIVATE _CRT_SECURE_NO_WARNINGS)
    set_target_properties(cursor_lib PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS ON
        CXX_VISIBILITY_PRESET default
    )
    target_compile_definitions(cursor_lib PUBLIC "CURSOR_API=__declspec(dllexport)")
else()
    target_compile_options(cursor_lib PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_definitions(cursor_lib PUBLIC "CURSOR_API=__attribute__((visibility(\"default\")))")
endif()

# Link dependencies as PRIVATE to avoid exposing them
target_link_libraries(cursor_lib PUBLIC
    nlohmann_json::nlohmann_json
)

target_link_libraries(cursor_lib PRIVATE
    ${CURL_LIBRARIES}
    cpr::cpr
    OpenSSL::SSL
    OpenSSL::Crypto
    ${ZLIB_TARGET}
    ${CMAKE_DL_LIBS}
    Threads::Threads
    $<$<BOOL:${HAVE_PQXX}>:${PQXX_LIBRARY}>
)


# Set additional linker flags for macOS
if(APPLE)
    target_link_options(cursor_lib PRIVATE -stdlib=libc++)
    target_link_libraries(cursor_lib PRIVATE "-framework CoreFoundation" "-framework Security")
endif()

# Main executable
add_executable(cursor-agent src/main.cpp)
target_include_directories(cursor-agent PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}/include
)
target_link_libraries(cursor-agent PRIVATE
    cursor_lib
)

if(CURSOR_BUILD_TESTS)
    add_executable(cursor-tests tests/main_test.cpp tests/ci_investigation_test.cpp src/utils/version.cpp)
    target_include_directories(cursor-tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_BINARY_DIR}/include
    )
    target_link_libraries(cursor-tests PRIVATE
        cursor_lib
        gtest_main
        gtest
    )
    add_test(NAME basic_tests COMMAND cursor-tests)
    add_test(
        NAME local_batch_e2e
        COMMAND bash ${CMAKE_SOURCE_DIR}/tests/e2e/run_local_batch.sh $<TARGET_FILE:cursor-agent>
    )

    set_target_properties(cursor-tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        MACOSX_RPATH ON
        INSTALL_RPATH "@executable_path/lib"
        BUILD_WITH_INSTALL_RPATH TRUE
    )

    add_executable(engine_extraction_test tests/engine_extraction_test.cpp)
    target_include_directories(engine_extraction_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_BINARY_DIR}/include
    )
    target_link_libraries(engine_extraction_test PRIVATE cursor_lib)
    set_target_properties(engine_extraction_test PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        MACOSX_RPATH ON
    )

    add_executable(benchmark_runner tests/benchmark_runner.cpp)
    target_include_directories(benchmark_runner PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_BINARY_DIR}/include
    )
    target_link_libraries(benchmark_runner PRIVATE cursor_lib)
    set_target_properties(benchmark_runner PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        MACOSX_RPATH ON
    )

    add_executable(validation_runner tests/validation_runner.cpp)
    target_include_directories(validation_runner PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_BINARY_DIR}/include
    )
    target_link_libraries(validation_runner PRIVATE cursor_lib)
    set_target_properties(validation_runner PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        MACOSX_RPATH ON
    )
endif()

# Set target properties
set_target_properties(cursor-agent PROPERTIES
    OUTPUT_NAME "cursor-agent"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    MACOSX_RPATH ON
    INSTALL_RPATH "@executable_path/../lib"
    BUILD_WITH_INSTALL_RPATH FALSE
    SKIP_BUILD_RPATH FALSE
)

# cursor-tester: batch scenario runner
add_executable(cursor-tester src/tester_main.cpp)
target_include_directories(cursor-tester PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}/include
)
target_link_libraries(cursor-tester PRIVATE cursor_lib)
set_target_properties(cursor-tester PROPERTIES
    OUTPUT_NAME "cursor-tester"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    MACOSX_RPATH ON
)

# check-scenarios: runs all scenarios through cursor-tester
add_custom_target(check-scenarios
    COMMAND $<TARGET_FILE:cursor-tester> scenarios/
    DEPENDS cursor-tester
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running scenario verification suite"
)

# On macOS, add additional settings
if(APPLE)
    target_link_options(cursor-agent PRIVATE
        -Wl,-rpath,@loader_path/../lib
        -Wl,-rpath,/usr/local/lib
        -Wl,-rpath,/opt/homebrew/lib
    )
    target_link_options(cursor-tester PRIVATE
        -Wl,-rpath,@loader_path/../lib
        -Wl,-rpath,/usr/local/lib
        -Wl,-rpath,/opt/homebrew/lib
    )
endif()

# Installation
install(TARGETS cursor-agent
    RUNTIME DESTINATION bin
)

# Install configuration files (only if .env.example exists)
if(EXISTS "${CMAKE_SOURCE_DIR}/.env.example")
    install(FILES .env.example
        DESTINATION etc/cursor
        RENAME config.env
    )
endif()

# CPack configuration for package generation
set(CPACK_PACKAGE_NAME "cursor-agent")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Professional AI Agent with Command Execution")
set(CPACK_PACKAGE_DESCRIPTION "A powerful, modular AI agent built with modern C++ that provides seamless interaction with language models and comprehensive system operations.")
set(CPACK_PACKAGE_VENDOR "bniladridas")
set(CPACK_PACKAGE_CONTACT "bniladridas@gmail.com")

# Platform-specific packaging
if(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
elseif(UNIX)
    set(CPACK_GENERATOR "DEB;RPM;TGZ")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libcpr1, nlohmann-json3-dev")
    set(CPACK_RPM_PACKAGE_REQUIRES "cpr-devel, json-devel")
elseif(WIN32)
    set(CPACK_GENERATOR "NSIS;ZIP")
endif()

include(CPack)



set(CURSOR_CHECK_TEST_COMMANDS "")
set(CURSOR_CHECK_DEPENDS cursor-agent)
if(CURSOR_BUILD_TESTS)
    list(APPEND CURSOR_CHECK_TEST_COMMANDS COMMAND ${CMAKE_BINARY_DIR}/cursor-tests)
    list(APPEND CURSOR_CHECK_TEST_COMMANDS COMMAND bash ${CMAKE_SOURCE_DIR}/tests/e2e/run_local_batch.sh $<TARGET_FILE:cursor-agent>)
    list(APPEND CURSOR_CHECK_DEPENDS cursor-tests)
    list(APPEND CURSOR_CHECK_DEPENDS cursor-tester)
endif()

# Add custom check target for testing and validation
add_custom_target(check
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target cursor-agent
    COMMAND echo ""
    COMMAND echo "============================================"
    COMMAND echo "  Unit Tests"
    COMMAND echo "============================================"
    COMMAND ${CMAKE_BINARY_DIR}/cursor-tests
    COMMAND echo "============================================"
    COMMAND echo "  Scenario Tests"
    COMMAND echo "============================================"
    COMMAND ${CMAKE_BINARY_DIR}/bin/cursor-tester ${CMAKE_SOURCE_DIR}/scenarios/
    COMMAND echo "============================================"
    COMMAND echo "  All checks passed!"
    COMMAND echo "============================================"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DEPENDS ${CURSOR_CHECK_DEPENDS}
    COMMENT "Running comprehensive project validation"
)

# Add quick test target
add_custom_target(test-quick
    COMMAND printf "2\\nexit\\n" | $<TARGET_FILE:cursor-agent>
    DEPENDS cursor-agent
    COMMENT "Quick functionality test"
)

if(CURSOR_BUILD_TESTS)
    add_custom_target(e2e-local
        COMMAND bash ${CMAKE_SOURCE_DIR}/tests/e2e/run_local_batch.sh $<TARGET_FILE:cursor-agent>
        DEPENDS cursor-agent
        COMMENT "Running local batch E2E checks"
    )
endif()

# Clang-Tidy target (disabled)
# if(CLANG_TIDY_EXE)
#     add_custom_target(clang-tidy
#         COMMAND ${CLANG_TIDY_EXE} --config-file=${CMAKE_SOURCE_DIR}/.clang-tidy -p ${CMAKE_BINARY_DIR} ${ALL_SOURCES}
#         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
#         COMMENT "Running clang-tidy on all source files"
#     )
# endif()

# Code coverage target (requires lcov)
find_program(LCOV_EXE lcov)
find_program(GENHTML_EXE genhtml)
if(LCOV_EXE AND CURSOR_BUILD_TESTS)
    add_custom_target(coverage
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/coverage
        COMMAND ctest --output-on-failure
        COMMAND ${LCOV_EXE} --capture --directory ${CMAKE_BINARY_DIR} --output-file ${CMAKE_BINARY_DIR}/coverage/coverage.info --exclude '/usr/*' --exclude '*/tests/*' --exclude '*/_deps/*'
        COMMAND ${LCOV_EXE} --list ${CMAKE_BINARY_DIR}/coverage/coverage.info
        COMMAND ${GENHTML_EXE} ${CMAKE_BINARY_DIR}/coverage/coverage.info --output-directory ${CMAKE_BINARY_DIR}/coverage/html
        DEPENDS cursor-tests
        COMMENT "Running tests and generating code coverage report"
    )
endif()

# Add preflight target (comprehensive pre-deployment checks)
add_custom_target(preflight
    COMMAND ${CMAKE_SOURCE_DIR}/.github/packaging/scripts/preflight.sh
    DEPENDS cursor-agent
    COMMENT "Running comprehensive preflight checks"
)
