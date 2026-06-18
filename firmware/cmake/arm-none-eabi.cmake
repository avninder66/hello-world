# =============================================================================
# arm-none-eabi.cmake
# CMake toolchain file for bare-metal ARM cross-compilation using the
# GNU Arm Embedded Toolchain (arm-none-eabi-gcc).
#
# Usage (from the build directory):
#   cmake -DCMAKE_TOOLCHAIN_FILE=../cmake/arm-none-eabi.cmake ..
#
# The file is automatically picked up by the top-level CMakeLists.txt via
#   set(CMAKE_TOOLCHAIN_FILE ... CACHE ... FORCE)
# =============================================================================

# ---------------------------------------------------------------------------
# Target system description
# CMake uses these values to skip host-specific link tests and to select the
# correct sysroot when one is provided.
# ---------------------------------------------------------------------------
set(CMAKE_SYSTEM_NAME      Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# ---------------------------------------------------------------------------
# Toolchain executable discovery
# Prefer an explicit toolchain prefix set in the environment; otherwise fall
# back to the standard arm-none-eabi- prefix on PATH.
# ---------------------------------------------------------------------------
if(DEFINED ENV{ARM_TOOLCHAIN_DIR})
    set(_TC_PREFIX "$ENV{ARM_TOOLCHAIN_DIR}/arm-none-eabi-")
else()
    set(_TC_PREFIX "arm-none-eabi-")
endif()

find_program(CMAKE_C_COMPILER       "${_TC_PREFIX}gcc"     REQUIRED)
find_program(CMAKE_CXX_COMPILER     "${_TC_PREFIX}g++"     REQUIRED)
find_program(CMAKE_ASM_COMPILER     "${_TC_PREFIX}gcc"     REQUIRED)
find_program(CMAKE_AR               "${_TC_PREFIX}ar"      REQUIRED)
find_program(CMAKE_RANLIB           "${_TC_PREFIX}ranlib"  REQUIRED)
find_program(CMAKE_OBJCOPY          "${_TC_PREFIX}objcopy" REQUIRED)
find_program(CMAKE_OBJDUMP          "${_TC_PREFIX}objdump" REQUIRED)
find_program(CMAKE_SIZE             "${_TC_PREFIX}size"    REQUIRED)

# Expose objcopy/size to sub-directories through cache variables so individual
# target CMakeLists.txt can reference them without re-running find_program().
set(ARM_OBJCOPY ${CMAKE_OBJCOPY} CACHE FILEPATH "arm-none-eabi-objcopy path")
set(ARM_SIZE    ${CMAKE_SIZE}    CACHE FILEPATH "arm-none-eabi-size path")

# ---------------------------------------------------------------------------
# Avoid CMake's compiler sanity-check link step.
# Bare-metal targets have no OS entry point (_start), so the default link
# test would fail.  STATIC_LIBRARY compile-tests avoid the link phase.
# ---------------------------------------------------------------------------
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# ---------------------------------------------------------------------------
# Common ARM Thumb-2 compiler flags
# Applied unconditionally to all targets compiled with this toolchain.
# Per-MCU flags (CPU, FPU, float-abi) are added in each target's CMakeLists.
# ---------------------------------------------------------------------------
set(_COMMON_FLAGS
    "-mthumb"
    "-fdata-sections"           # Place each data object in its own section  → gc-sections removes unused data
    "-ffunction-sections"       # Place each function in its own section     → gc-sections removes unused code
    "-fno-strict-aliasing"      # Required for safe peripheral register access via pointer casts
    "-fstack-usage"             # Emit .su files with per-function stack usage (useful for safety analysis)
    "-Wall"
    "-Wextra"
    "-Wno-unused-parameter"     # HAL generated code has many unused parameters
    "-MMD"                      # Auto-generate dependency files (.d) for incremental builds
    "-MP"                       # Add phony targets for headers so removed headers don't break make
)

# Join the list into a single string for CMake flag variables
list(JOIN _COMMON_FLAGS " " _COMMON_FLAGS_STR)

set(CMAKE_C_FLAGS_INIT   "${_COMMON_FLAGS_STR}" CACHE STRING "Initial C flags"   FORCE)
set(CMAKE_CXX_FLAGS_INIT "${_COMMON_FLAGS_STR} -fno-exceptions -fno-rtti" CACHE STRING "Initial C++ flags" FORCE)
set(CMAKE_ASM_FLAGS_INIT "${_COMMON_FLAGS_STR} -x assembler-with-cpp"     CACHE STRING "Initial ASM flags" FORCE)

# ---------------------------------------------------------------------------
# Build-type-specific optimisation flags
# ---------------------------------------------------------------------------
# Debug: no optimisation, full debug info
set(CMAKE_C_FLAGS_DEBUG   "-Og -g3 -DDEBUG" CACHE STRING "C debug flags"   FORCE)
set(CMAKE_CXX_FLAGS_DEBUG "-Og -g3 -DDEBUG" CACHE STRING "C++ debug flags" FORCE)

# Release: optimise for size (typical for embedded), strip debug info
set(CMAKE_C_FLAGS_RELEASE   "-Os -DNDEBUG" CACHE STRING "C release flags"   FORCE)
set(CMAKE_CXX_FLAGS_RELEASE "-Os -DNDEBUG" CACHE STRING "C++ release flags" FORCE)

# RelWithDebInfo: optimise for size but keep debug symbols
set(CMAKE_C_FLAGS_RELWITHDEBINFO   "-Os -g -DNDEBUG" CACHE STRING "C relwithdebinfo flags"   FORCE)
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-Os -g -DNDEBUG" CACHE STRING "C++ relwithdebinfo flags" FORCE)

# ---------------------------------------------------------------------------
# Cortex-M7 convenience variable
# Sub-directories targeting the STM32H743 should append these flags to their
# target_compile_options() and target_link_options() calls.
# ---------------------------------------------------------------------------
set(MCU_FLAGS_CORTEX_M7
    "-mcpu=cortex-m7"
    "-mfpu=fpv5-d16"
    "-mfloat-abi=hard"
    CACHE STRING "Compile/link flags for Cortex-M7 with FPv5-D16 hard-float ABI")

# ---------------------------------------------------------------------------
# Cortex-M0+ convenience variable
# Sub-directories targeting the STM32G071 should append these flags.
# M0+ has no FPU — software float only.
# ---------------------------------------------------------------------------
set(MCU_FLAGS_CORTEX_M0PLUS
    "-mcpu=cortex-m0plus"
    "-mfloat-abi=soft"
    CACHE STRING "Compile/link flags for Cortex-M0+")

# ---------------------------------------------------------------------------
# Linker flags common to all Lumina firmware targets
# ---------------------------------------------------------------------------
set(CMAKE_EXE_LINKER_FLAGS_INIT
    "-mthumb \
     -Wl,--gc-sections \
     -Wl,--print-memory-usage \
     -Wl,-Map=firmware.map,--cref \
     --specs=nano.specs \
     --specs=nosys.specs"
    CACHE STRING "Common EXE linker flags" FORCE)

# ---------------------------------------------------------------------------
# Search path hints: only look for headers and libraries in the sysroot,
# not in the host system's default paths.
# ---------------------------------------------------------------------------
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# ---------------------------------------------------------------------------
# Helper macro: add_firmware_target(<target>)
# Convenience wrapper used by lcu/CMakeLists.txt and
# safety_supervisor/CMakeLists.txt to attach post-build objcopy/size steps.
# Defined here so both sub-directories can use it without duplication.
# ---------------------------------------------------------------------------
macro(add_post_build_steps TARGET_NAME)
    # Generate Intel HEX file (for programming via ST-LINK / DFU)
    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${ARM_OBJCOPY} -O ihex
                $<TARGET_FILE:${TARGET_NAME}>
                ${TARGET_NAME}.hex
        COMMENT "Generating HEX: ${TARGET_NAME}.hex"
    )

    # Generate raw binary file (for SCP / bootloader transfers)
    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${ARM_OBJCOPY} -O binary -S
                $<TARGET_FILE:${TARGET_NAME}>
                ${TARGET_NAME}.bin
        COMMENT "Generating BIN: ${TARGET_NAME}.bin"
    )

    # Print section sizes (text, data, bss)
    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${ARM_SIZE} --format=berkeley $<TARGET_FILE:${TARGET_NAME}>
        COMMENT "Memory usage for ${TARGET_NAME}:"
    )
endmacro()
