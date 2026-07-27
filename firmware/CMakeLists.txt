cmake_minimum_required(VERSION 3.20)

# ---------------------------------------------------------------------------
# Cross-compilation toolchain must be set BEFORE project() so that CMake
# uses the arm-none-eabi-gcc compiler for all language tests.
# ---------------------------------------------------------------------------
set(CMAKE_TOOLCHAIN_FILE ${CMAKE_SOURCE_DIR}/cmake/arm-none-eabi.cmake
    CACHE FILEPATH "Toolchain file" FORCE)

project(lumina-firmware
    VERSION 1.0.0
    DESCRIPTION "Lumina Temporary Traffic Signal Controller Firmware"
    LANGUAGES C CXX ASM)

# ---------------------------------------------------------------------------
# Build options
# ---------------------------------------------------------------------------
option(BUILD_LCU              "Build LCU-100 main controller firmware"       ON)
option(BUILD_SAFETY_SUPERVISOR "Build safety supervisor firmware"             ON)
option(BUILD_UNIT_TESTS       "Build unit tests (requires host compiler)"     OFF)

# ---------------------------------------------------------------------------
# Language standards
# ---------------------------------------------------------------------------
set(CMAKE_C_STANDARD            11)
set(CMAKE_C_STANDARD_REQUIRED   ON)
set(CMAKE_C_EXTENSIONS          OFF)

set(CMAKE_CXX_STANDARD          17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)

# ---------------------------------------------------------------------------
# Firmware version compile definitions
# Propagated to all sub-targets via an INTERFACE library so every translation
# unit has consistent version macros without each CMakeLists.txt repeating them.
# ---------------------------------------------------------------------------
string(TIMESTAMP BUILD_DATE "%Y-%m-%d" UTC)
string(TIMESTAMP BUILD_TIME "%H:%M:%S" UTC)

add_library(lumina_version_defs INTERFACE)
target_compile_definitions(lumina_version_defs INTERFACE
    LUMINA_FW_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    LUMINA_FW_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    LUMINA_FW_VERSION_PATCH=${PROJECT_VERSION_PATCH}
    LUMINA_FW_VERSION_STR="${PROJECT_VERSION}"
    BUILD_DATE="${BUILD_DATE}"
    BUILD_TIME="${BUILD_TIME}"
)

# ---------------------------------------------------------------------------
# Global compiler warnings — applied as INTERFACE so sub-targets inherit them
# ---------------------------------------------------------------------------
add_library(lumina_compiler_options INTERFACE)
target_compile_options(lumina_compiler_options INTERFACE
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wdouble-promotion
    -Wformat=2
    -Wformat-truncation
    -fno-common
    # Treat warnings as errors in CI; override locally with -DWERROR=OFF
    $<$<BOOL:${WERROR}>:-Werror>
)

option(WERROR "Treat compiler warnings as errors" OFF)

# ---------------------------------------------------------------------------
# Common include path library — protocol headers shared by LCU and safety MCU
# ---------------------------------------------------------------------------
add_library(lumina_common_includes INTERFACE)
target_include_directories(lumina_common_includes INTERFACE
    ${CMAKE_SOURCE_DIR}/common/protocol
    ${CMAKE_SOURCE_DIR}/common/config
)

# ---------------------------------------------------------------------------
# Subdirectories
# ---------------------------------------------------------------------------
if(BUILD_LCU)
    message(STATUS "[Lumina] Configuring LCU-100 main controller firmware (STM32H743)")
    add_subdirectory(lcu)
endif()

if(BUILD_SAFETY_SUPERVISOR)
    message(STATUS "[Lumina] Configuring safety supervisor firmware (STM32G071)")
    add_subdirectory(safety_supervisor)
endif()

if(BUILD_UNIT_TESTS)
    message(STATUS "[Lumina] Configuring unit tests (host build)")
    # Unit tests require a host compiler — switch toolchain via a CMake preset
    # or invoke separately:
    #   cmake -DBUILD_UNIT_TESTS=ON -DCMAKE_TOOLCHAIN_FILE="" ..
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Configuration summary
# ---------------------------------------------------------------------------
message(STATUS "")
message(STATUS "========================================================")
message(STATUS "  Lumina Firmware Build Configuration")
message(STATUS "========================================================")
message(STATUS "  Project version  : ${PROJECT_VERSION}")
message(STATUS "  Build date (UTC) : ${BUILD_DATE} ${BUILD_TIME}")
message(STATUS "  Build type       : ${CMAKE_BUILD_TYPE}")
message(STATUS "  C standard       : C${CMAKE_C_STANDARD}")
message(STATUS "  C++ standard     : C++${CMAKE_CXX_STANDARD}")
message(STATUS "  Toolchain        : ${CMAKE_TOOLCHAIN_FILE}")
message(STATUS "  Warnings=errors  : ${WERROR}")
message(STATUS "--------------------------------------------------------")
message(STATUS "  BUILD_LCU              : ${BUILD_LCU}")
message(STATUS "  BUILD_SAFETY_SUPERVISOR: ${BUILD_SAFETY_SUPERVISOR}")
message(STATUS "  BUILD_UNIT_TESTS       : ${BUILD_UNIT_TESTS}")
message(STATUS "========================================================")
message(STATUS "")
