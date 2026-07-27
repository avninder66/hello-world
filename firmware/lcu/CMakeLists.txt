# =============================================================================
# lcu/CMakeLists.txt
# Build definition for the LCU-100 main controller firmware.
# Target MCU: STM32H743ZIT6 — ARM Cortex-M7 @ 480 MHz, 2 MB Flash, 1 MB RAM
#
# ENGINEER NOTE:
#   Before this target will compile you must populate:
#     lcu/Drivers/STM32H7xx_HAL_Driver/   — STM32H7 HAL from STM32CubeMX / CubeIDE
#     lcu/Middlewares/FreeRTOS/            — FreeRTOS kernel (or use FreeRTOS-Kernel via FetchContent)
#     lcu/Core/Inc/FreeRTOSConfig.h        — application FreeRTOS configuration header
#   Stubs for the above are referenced below so CMake succeeds as soon as the
#   directories exist; the build itself will fail with clear "file not found"
#   messages pointing to the missing HAL/RTOS sources.
# =============================================================================

cmake_minimum_required(VERSION 3.20)

# ---------------------------------------------------------------------------
# Source files
# ---------------------------------------------------------------------------
# Explicit glob patterns make dependency tracking reliable.  GLOB_RECURSE is
# used here for convenience during initial development; a production build
# should list sources explicitly so that adding a file never silently changes
# what is compiled.
file(GLOB_RECURSE LCU_CORE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/*.s
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/*.S
)

file(GLOB_RECURSE LCU_APP_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/App/Src/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/Src/*.cpp
)

file(GLOB_RECURSE LCU_DRIVER_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/Src/*.c
)

# HAL sources — populated after running STM32CubeMX
file(GLOB_RECURSE LCU_HAL_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32H7xx_HAL_Driver/Src/*.c
)

# FreeRTOS kernel sources — populate Middlewares/FreeRTOS/Source/ from
# https://github.com/FreeRTOS/FreeRTOS-Kernel or from STM32CubeMX output.
file(GLOB_RECURSE LCU_FREERTOS_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Middlewares/FreeRTOS/Source/*.c
    # Port file for Cortex-M7 (hard-float ABI)
    ${CMAKE_CURRENT_SOURCE_DIR}/Middlewares/FreeRTOS/Source/portable/GCC/ARM_CM7/r0p1/port.c
    # Heap implementation — heap_4 is recommended for most embedded targets
    ${CMAKE_CURRENT_SOURCE_DIR}/Middlewares/FreeRTOS/Source/portable/MemMang/heap_4.c
)

# ---------------------------------------------------------------------------
# Firmware executable target
# ---------------------------------------------------------------------------
add_executable(lcu-firmware
    ${LCU_CORE_SOURCES}
    ${LCU_APP_SOURCES}
    ${LCU_DRIVER_SOURCES}
    ${LCU_HAL_SOURCES}
    ${LCU_FREERTOS_SOURCES}
)

# ---------------------------------------------------------------------------
# Cortex-M7 compile flags
# MCU_FLAGS_CORTEX_M7 is defined in cmake/arm-none-eabi.cmake
# ---------------------------------------------------------------------------
target_compile_options(lcu-firmware PRIVATE
    ${MCU_FLAGS_CORTEX_M7}
    # Extra safety-oriented warnings for application code
    -Wconversion
    -Wno-sign-conversion        # HAL uses mixed signed/unsigned — suppress here
    -Wimplicit-fallthrough
    -Wundef
)

# ---------------------------------------------------------------------------
# Preprocessor definitions
# ---------------------------------------------------------------------------
target_compile_definitions(lcu-firmware PRIVATE
    # MCU family selector — required by STM32H7 HAL headers
    STM32H743xx
    USE_HAL_DRIVER

    # Lumina application defines
    LCU_MAIN_CONTROLLER=1
    LUMINA_PLATFORM_LCU100=1

    # FreeRTOS tick rate is set here as a global fallback; the authoritative
    # value lives in FreeRTOSConfig.h
    configUSE_TRACE_FACILITY=1

    # Version macros (from top-level lumina_version_defs INTERFACE library)
    # — inherited via target_link_libraries below
)

# ---------------------------------------------------------------------------
# Include directories
# ---------------------------------------------------------------------------
target_include_directories(lcu-firmware PRIVATE
    # Application headers
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/App/Inc

    # Board-support and driver headers
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/Inc

    # STM32H7 HAL (populated from CubeMX)
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32H7xx_HAL_Driver/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32H7xx_HAL_Driver/Inc/Legacy
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/CMSIS/Device/ST/STM32H7xx/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/CMSIS/Include

    # FreeRTOS
    ${CMAKE_CURRENT_SOURCE_DIR}/Middlewares/FreeRTOS/Source/include
    ${CMAKE_CURRENT_SOURCE_DIR}/Middlewares/FreeRTOS/Source/portable/GCC/ARM_CM7/r0p1

    # Shared Lumina protocol and config headers
    ${CMAKE_SOURCE_DIR}/common/protocol
    ${CMAKE_SOURCE_DIR}/common/config
)

# ---------------------------------------------------------------------------
# Inherited interface libraries
# ---------------------------------------------------------------------------
target_link_libraries(lcu-firmware PRIVATE
    lumina_version_defs         # LUMINA_FW_VERSION_*, BUILD_DATE macros
    lumina_compiler_options     # Project-wide warning flags
    lumina_common_includes      # common/protocol, common/config paths
)

# ---------------------------------------------------------------------------
# Linker script and linker flags
# ---------------------------------------------------------------------------
set(LCU_LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/STM32H743ZITX_FLASH.ld)

target_link_options(lcu-firmware PRIVATE
    ${MCU_FLAGS_CORTEX_M7}
    -T${LCU_LINKER_SCRIPT}
    -Wl,--gc-sections
    -Wl,--print-memory-usage
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/lcu-firmware.map,--cref
    --specs=nano.specs
    --specs=nosys.specs
    # Retain FreeRTOS idle and timer task stack symbols
    -Wl,-u,vTaskSwitchContext
)

# Ensure the linker script is a proper dependency so re-linking occurs when it changes
set_target_properties(lcu-firmware PROPERTIES
    LINK_DEPENDS ${LCU_LINKER_SCRIPT}
    OUTPUT_NAME  "lcu-firmware"
    SUFFIX       ".elf"
)

# ---------------------------------------------------------------------------
# Post-build steps: generate .hex / .bin and print memory usage
# Macro defined in cmake/arm-none-eabi.cmake
# ---------------------------------------------------------------------------
add_post_build_steps(lcu-firmware)

# ---------------------------------------------------------------------------
# Custom target: flash via ST-LINK using OpenOCD
# Run with:  cmake --build . --target lcu-flash
# ---------------------------------------------------------------------------
find_program(OPENOCD openocd)
if(OPENOCD)
    add_custom_target(lcu-flash
        DEPENDS lcu-firmware
        COMMAND ${OPENOCD}
                -f interface/stlink.cfg
                -f target/stm32h7x.cfg
                -c "program lcu-firmware.hex verify reset exit"
        COMMENT "Flashing LCU-100 via ST-LINK / OpenOCD"
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
else()
    message(STATUS "[LCU] OpenOCD not found — 'lcu-flash' target not available")
endif()

# ---------------------------------------------------------------------------
# Build summary
# ---------------------------------------------------------------------------
message(STATUS "[LCU-100] lcu-firmware target configured")
message(STATUS "[LCU-100]   Core sources  : ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src")
message(STATUS "[LCU-100]   App sources   : ${CMAKE_CURRENT_SOURCE_DIR}/App/Src")
message(STATUS "[LCU-100]   Linker script : ${LCU_LINKER_SCRIPT}")
message(STATUS "[LCU-100]   MCU flags     : ${MCU_FLAGS_CORTEX_M7}")
