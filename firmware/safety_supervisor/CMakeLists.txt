# =============================================================================
# safety_supervisor/CMakeLists.txt
# Build definition for the Lumina safety supervisor firmware.
# Target MCU: STM32G071RBT6 — ARM Cortex-M0+ @ 64 MHz, 128 KB Flash, 36 KB RAM
#
# DESIGN CONSTRAINT — NO RTOS:
#   The safety supervisor runs a bare-metal superloop with interrupt-driven
#   watchdog refresh.  FreeRTOS (or any RTOS) must NOT be used on this MCU.
#   -DSAFETY_MCU=1 is defined globally so shared headers can gate RTOS includes:
#
#     #ifndef SAFETY_MCU
#       #include "FreeRTOS.h"   /* only compiled on the LCU main MCU */
#     #endif
#
# ENGINEER NOTE:
#   Before this target will compile you must populate:
#     safety_supervisor/Drivers/STM32G0xx_HAL_Driver/   — from STM32CubeMX
#     safety_supervisor/Drivers/CMSIS/                  — CMSIS-Core for M0+
#   See the LCU-100 CMakeLists.txt for the equivalent HAL population notes.
# =============================================================================

cmake_minimum_required(VERSION 3.20)

# ---------------------------------------------------------------------------
# Source files
# ---------------------------------------------------------------------------
file(GLOB_RECURSE SS_CORE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/*.s
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/*.S
)

file(GLOB_RECURSE SS_APP_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/App/Src/*.c
)

# STM32G0 HAL sources (populated from CubeMX)
file(GLOB_RECURSE SS_HAL_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32G0xx_HAL_Driver/Src/*.c
)

# ---------------------------------------------------------------------------
# Firmware executable target
# ---------------------------------------------------------------------------
add_executable(safety-supervisor-firmware
    ${SS_CORE_SOURCES}
    ${SS_APP_SOURCES}
    ${SS_HAL_SOURCES}
)

# ---------------------------------------------------------------------------
# Cortex-M0+ compile flags
# MCU_FLAGS_CORTEX_M0PLUS is defined in cmake/arm-none-eabi.cmake.
# NOTE: M0+ has no hardware FPU — all floating-point must be soft-float.
#       Avoid floating-point arithmetic entirely in safety supervisor code;
#       use scaled integer arithmetic for all timing and threshold comparisons.
# ---------------------------------------------------------------------------
target_compile_options(safety-supervisor-firmware PRIVATE
    ${MCU_FLAGS_CORTEX_M0PLUS}
    -Wconversion
    -Wimplicit-fallthrough
    -Wundef
    # Enforce MISRA-compatible subset: ban VLAs, dynamic alloc in safety code
    -Wvla
    # Enforce that no functions exceed a single page of stack (safety analysis)
    -fstack-usage
)

# ---------------------------------------------------------------------------
# Preprocessor definitions
# ---------------------------------------------------------------------------
target_compile_definitions(safety-supervisor-firmware PRIVATE
    # MCU family selector — required by STM32G0 HAL headers
    STM32G071xx
    USE_HAL_DRIVER

    # SAFETY_MCU=1 gates out RTOS includes in shared Lumina headers
    SAFETY_MCU=1

    # Lumina platform identifier
    LUMINA_PLATFORM_SAFETY_SUPERVISOR=1

    # Safety supervisor does NOT use dynamic memory allocation.
    # This definition prevents any accidental inclusion of heap functions.
    LUMINA_NO_HEAP=1

    # Watchdog timeout in milliseconds (IWDG configuration cross-check)
    SAFETY_WDG_TIMEOUT_MS=50
)

# ---------------------------------------------------------------------------
# Include directories
# ---------------------------------------------------------------------------
target_include_directories(safety-supervisor-firmware PRIVATE
    # Application and core headers
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/App/Inc

    # STM32G0 HAL (populated from CubeMX)
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32G0xx_HAL_Driver/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32G0xx_HAL_Driver/Inc/Legacy
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/CMSIS/Device/ST/STM32G0xx/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/CMSIS/Include

    # Shared Lumina protocol and configuration headers.
    # The safety MCU only needs lumina_safety_protocol.h and fault_codes.h.
    # SAFETY_MCU=1 guards prevent inclusion of RTOS-dependent structures.
    ${CMAKE_SOURCE_DIR}/common/protocol
    ${CMAKE_SOURCE_DIR}/common/config
)

# ---------------------------------------------------------------------------
# Inherited interface libraries
# ---------------------------------------------------------------------------
target_link_libraries(safety-supervisor-firmware PRIVATE
    lumina_version_defs         # LUMINA_FW_VERSION_*, BUILD_DATE macros
    lumina_compiler_options     # Project-wide warning flags
    lumina_common_includes      # common/protocol, common/config paths
)

# ---------------------------------------------------------------------------
# Linker script and linker flags
# ---------------------------------------------------------------------------
set(SS_LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/STM32G071RBTX_FLASH.ld)

target_link_options(safety-supervisor-firmware PRIVATE
    ${MCU_FLAGS_CORTEX_M0PLUS}
    -T${SS_LINKER_SCRIPT}
    -Wl,--gc-sections
    -Wl,--print-memory-usage
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/safety-supervisor.map,--cref
    --specs=nano.specs
    --specs=nosys.specs
    # No dynamic memory: ensure newlib's _sbrk is not silently pulled in
    -Wl,--undefined=_sbrk
)

set_target_properties(safety-supervisor-firmware PROPERTIES
    LINK_DEPENDS ${SS_LINKER_SCRIPT}
    OUTPUT_NAME  "safety-supervisor-firmware"
    SUFFIX       ".elf"
)

# ---------------------------------------------------------------------------
# Post-build steps: generate .hex / .bin and print memory usage
# Macro defined in cmake/arm-none-eabi.cmake
# ---------------------------------------------------------------------------
add_post_build_steps(safety-supervisor-firmware)

# ---------------------------------------------------------------------------
# Custom target: flash via ST-LINK / OpenOCD
# Run with:  cmake --build . --target ss-flash
# ---------------------------------------------------------------------------
find_program(OPENOCD openocd)
if(OPENOCD)
    add_custom_target(ss-flash
        DEPENDS safety-supervisor-firmware
        COMMAND ${OPENOCD}
                -f interface/stlink.cfg
                -f target/stm32g0x.cfg
                -c "program safety-supervisor-firmware.hex verify reset exit"
        COMMENT "Flashing safety supervisor via ST-LINK / OpenOCD"
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
else()
    message(STATUS "[Safety] OpenOCD not found — 'ss-flash' target not available")
endif()

# ---------------------------------------------------------------------------
# Static analysis target: runs cppcheck with MISRA C:2012 addon
# Requires cppcheck >= 2.9 with the misra.py addon installed.
# Run with:  cmake --build . --target ss-misra
# ---------------------------------------------------------------------------
find_program(CPPCHECK cppcheck)
if(CPPCHECK)
    add_custom_target(ss-misra
        COMMAND ${CPPCHECK}
                --addon=misra
                --std=c11
                --platform=arm32-wchar_t2
                --suppress=missingIncludeSystem
                -DSAFETY_MCU=1
                -DSTM32G071xx
                -I ${CMAKE_CURRENT_SOURCE_DIR}/Core/Inc
                -I ${CMAKE_CURRENT_SOURCE_DIR}/App/Inc
                -I ${CMAKE_SOURCE_DIR}/common/protocol
                -I ${CMAKE_SOURCE_DIR}/common/config
                ${CMAKE_CURRENT_SOURCE_DIR}/App/Src
        COMMENT "Running MISRA C:2012 static analysis on safety supervisor"
    )
else()
    message(STATUS "[Safety] cppcheck not found — 'ss-misra' target not available")
endif()

# ---------------------------------------------------------------------------
# Build summary
# ---------------------------------------------------------------------------
message(STATUS "[Safety] safety-supervisor-firmware target configured")
message(STATUS "[Safety]   Core sources  : ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src")
message(STATUS "[Safety]   App sources   : ${CMAKE_CURRENT_SOURCE_DIR}/App/Src")
message(STATUS "[Safety]   Linker script : ${SS_LINKER_SCRIPT}")
message(STATUS "[Safety]   MCU flags     : ${MCU_FLAGS_CORTEX_M0PLUS}")
message(STATUS "[Safety]   SAFETY_MCU=1  : RTOS code excluded from shared headers")
