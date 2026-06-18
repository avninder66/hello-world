/* =============================================================================
 * STM32H743ZITX_FLASH.ld
 * Linker script for STM32H743ZIT6 — Lumina LCU-100 main controller
 *
 * MCU resources:
 *   Flash     : 2 MB  @ 0x08000000 (single bank, or dual-bank with DBANK=1)
 *   ITCMRAM   : 64 KB @ 0x00000000 (zero-wait-state instruction TCM)
 *   DTCMRAM   : 128 KB @ 0x20000000 (zero-wait-state data TCM)
 *   AXI SRAM (D1) : 512 KB @ 0x24000000
 *   AHB SRAM (D2) : 288 KB @ 0x30000000  (split: 128 KB + 128 KB + 32 KB)
 *   AHB SRAM (D3) : 64 KB  @ 0x38000000  (backup SRAM / low-power domain)
 *   FLASH_SAFE     : 256 KB reserved for rollback / golden image
 *
 * Stack/Heap:
 *   _estack   placed at top of DTCMRAM (fast, zero-wait access for ISR stacks)
 *   FreeRTOS heap is placed in AXI SRAM D1 (largest contiguous region)
 *   Ethernet/DMA descriptors should be placed in D2 SRAM (DMA-accessible)
 *
 * Usage:
 *   Passed to the linker via -T in target_link_options() in lcu/CMakeLists.txt.
 *   arm-none-eabi-ld (via GCC) processes this file.
 * ============================================================================= */

/* Entry point — the reset handler defined in the startup assembly file */
ENTRY(Reset_Handler)

/* Minimum sizes for heap and stack.
 * The linker will error if these regions do not fit in available RAM. */
_Min_Heap_Size  = 0x2000;   /* 8 KB  — C runtime heap (malloc/free) */
_Min_Stack_Size = 0x2000;   /* 8 KB  — MSP (main/interrupt stack) in DTCMRAM */

/* FreeRTOS heap size — placed in D2 SRAM so DMA peripherals can reach it */
_FreeRTOS_Heap_Size = 0x20000;  /* 128 KB */

/* =============================================================================
 * MEMORY MAP
 * ============================================================================= */
MEMORY
{
    /* -------------------------------------------------------------------------
     * Flash: two logical regions so the bootloader can write the "safe" image
     * without touching the main application area.
     * The first 256 KB of the 2 MB bank is used by the bootloader itself.
     * Application code occupies 0x08040000 – 0x080FFFFF (1.75 MB).
     * 0x08100000 – 0x0817FFFF is the rollback / golden image shadow area.
     * ------------------------------------------------------------------------- */
    FLASH       (rx)  : ORIGIN = 0x08000000, LENGTH = 2048K

    /* Rollback-safe golden image area (upper 256 KB of second 1 MB bank).
     * The bootloader copies this region to main flash on a failed update.
     * Place nothing here automatically — content is written by the OTA manager. */
    FLASH_SAFE  (rx)  : ORIGIN = 0x08100000, LENGTH = 256K

    /* -------------------------------------------------------------------------
     * RAM regions — ordered by access speed (fastest first)
     * ------------------------------------------------------------------------- */

    /* D-TCM RAM: 128 KB, zero-wait-state, accessible only by CPU core.
     * Used for: interrupt stack (MSP), time-critical ISR data, DWT-profiled vars.
     * NOTE: Cannot be used as source/destination for DMA transfers. */
    DTCMRAM     (xrw) : ORIGIN = 0x20000000, LENGTH = 128K

    /* I-TCM RAM: 64 KB, zero-wait-state instruction memory.
     * Used for: latency-critical code (e.g. phase-change ISR, CAN Tx handler).
     * Mark functions with __attribute__((section(".itcmram"))) to place here. */
    ITCMRAM     (xrw) : ORIGIN = 0x00000000, LENGTH = 64K

    /* AXI SRAM (D1 domain): 512 KB, accessible by CPU and most DMA controllers.
     * Default run-time RAM: .data, .bss, FreeRTOS task stacks (heap_4). */
    RAM_D1      (xrw) : ORIGIN = 0x24000000, LENGTH = 512K

    /* AHB SRAM (D2 domain): 288 KB, accessible by D2 DMA1, DMA2, BDMA.
     * Used for: DMA rx/tx buffers, Ethernet descriptors, USB FIFO areas.
     * Place buffers here with __attribute__((section(".dma_buffers"))). */
    RAM_D2      (xrw) : ORIGIN = 0x30000000, LENGTH = 288K

    /* AHB SRAM (D3 domain): 64 KB, accessible by BDMA in low-power Stop modes.
     * Used for: ADC result buffers, RTC backup registers mirror, safe-state vars. */
    RAM_D3      (xrw) : ORIGIN = 0x38000000, LENGTH = 64K
}

/* =============================================================================
 * SECTIONS
 * ============================================================================= */
SECTIONS
{
    /* -------------------------------------------------------------------------
     * Interrupt vector table
     * Must be at the start of flash (or at an offset configured in SCB->VTOR).
     * STM32H743 expects the vector table at 0x08000000 after reset.
     * ------------------------------------------------------------------------- */
    .isr_vector :
    {
        . = ALIGN(512);         /* H743 VTOR requires 512-byte alignment */
        KEEP(*(.isr_vector))    /* KEEP prevents gc-sections from removing it */
        . = ALIGN(4);
    } >FLASH

    /* -------------------------------------------------------------------------
     * Code and read-only data
     * ------------------------------------------------------------------------- */
    .text :
    {
        . = ALIGN(4);
        *(.text)                /* .text sections (program code) */
        *(.text*)               /* .text* sections (inline functions, templates) */
        *(.glue_7)              /* ARM/Thumb interworking glue */
        *(.glue_7t)
        *(.eh_frame)
        KEEP(*(.init))
        KEEP(*(.fini))
        . = ALIGN(4);
    } >FLASH

    /* Read-only data: string literals, const globals, lookup tables */
    .rodata :
    {
        . = ALIGN(4);
        *(.rodata)
        *(.rodata*)
        . = ALIGN(4);
    } >FLASH

    /* ARM exception unwinding tables (needed if C++ exceptions or CMSIS-DSP used) */
    .ARM.extab :
    {
        . = ALIGN(4);
        *(.ARM.extab* .gnu.linkonce.armextab.*)
        . = ALIGN(4);
    } >FLASH

    .ARM :
    {
        . = ALIGN(4);
        __exidx_start = .;
        *(.ARM.exidx*)
        __exidx_end = .;
        . = ALIGN(4);
    } >FLASH

    /* -------------------------------------------------------------------------
     * C/C++ constructor/destructor arrays
     * Startup code iterates these arrays to call global constructors before main.
     * ------------------------------------------------------------------------- */
    .preinit_array :
    {
        . = ALIGN(4);
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array*))
        PROVIDE_HIDDEN(__preinit_array_end = .);
        . = ALIGN(4);
    } >FLASH

    .init_array :
    {
        . = ALIGN(4);
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array*))
        PROVIDE_HIDDEN(__init_array_end = .);
        . = ALIGN(4);
    } >FLASH

    .fini_array :
    {
        . = ALIGN(4);
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array*))
        PROVIDE_HIDDEN(__fini_array_end = .);
        . = ALIGN(4);
    } >FLASH

    /* _sidata marks the load address (LMA) of the .data section in flash.
     * The startup code copies from _sidata to _sdata at boot time. */
    _sidata = LOADADDR(.data);

    /* -------------------------------------------------------------------------
     * Initialised data — loaded from flash, executed from AXI SRAM (D1)
     * ------------------------------------------------------------------------- */
    .data :
    {
        . = ALIGN(4);
        _sdata = .;             /* Provide start symbol to startup code */
        *(.data)
        *(.data*)
        *(.RamFunc)             /* Functions declared __ramfunc (execute from RAM) */
        *(.RamFunc*)
        . = ALIGN(4);
        _edata = .;             /* Provide end symbol to startup code */
    } >RAM_D1 AT>FLASH          /* VMA = RAM_D1, LMA = FLASH */

    /* -------------------------------------------------------------------------
     * Zero-initialised data (BSS) — entirely in AXI SRAM D1
     * Startup code zeroes _sbss..._ebss before calling main().
     * ------------------------------------------------------------------------- */
    .bss :
    {
        . = ALIGN(4);
        _sbss = .;
        __bss_start__ = _sbss;
        *(.bss)
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
        __bss_end__ = _ebss;
    } >RAM_D1

    /* -------------------------------------------------------------------------
     * FreeRTOS heap — placed in D2 SRAM so BDMA/DMA2 can reach task buffers
     * (e.g. USB, CAN Tx/Rx queues allocated from pvPortMalloc).
     * heap_4.c honours the ucHeap[] array size; we override the default
     * linker-placed symbol so the array lands in this dedicated section.
     *
     * In FreeRTOSConfig.h set:
     *   #define configAPPLICATION_ALLOCATED_HEAP  1
     * Then declare in a .c file:
     *   uint8_t ucHeap[configTOTAL_HEAP_SIZE] __attribute__((section(".freertos_heap")));
     * ------------------------------------------------------------------------- */
    .freertos_heap (NOLOAD) :
    {
        . = ALIGN(8);
        _sfreertos_heap = .;
        KEEP(*(.freertos_heap))
        . = ALIGN(8);
        _efreertos_heap = .;
    } >RAM_D2

    /* -------------------------------------------------------------------------
     * DMA-accessible buffers (D2 SRAM)
     * Ethernet Tx/Rx descriptors, USB FIFOs, CAN FIFO mirrors, GNSS UART buffers.
     * Use: uint8_t g_uart_rx_buf[256] __attribute__((section(".dma_buffers")));
     * ------------------------------------------------------------------------- */
    .dma_buffers (NOLOAD) :
    {
        . = ALIGN(32);          /* Cache-line alignment for D-cache maintenance */
        _sdma_buffers = .;
        *(.dma_buffers)
        *(.dma_buffers*)
        . = ALIGN(32);
        _edma_buffers = .;
    } >RAM_D2

    /* -------------------------------------------------------------------------
     * D3 domain: low-power / backup data
     * Variables placed here retain their values in STOP2 / Standby modes when
     * VBAT is applied.
     * Use: uint32_t g_fault_log __attribute__((section(".d3_data")));
     * ------------------------------------------------------------------------- */
    .d3_data (NOLOAD) :
    {
        . = ALIGN(4);
        *(.d3_data)
        *(.d3_data*)
        . = ALIGN(4);
    } >RAM_D3

    /* -------------------------------------------------------------------------
     * ITCM code — zero-wait ISR / signal-phase routines
     * The startup code copies from flash (_sitcmfuncs) to ITCMRAM before main().
     * Use: void phase_isr(void) __attribute__((section(".itcmram")));
     * ------------------------------------------------------------------------- */
    _sitcmfuncs = LOADADDR(.itcmram);
    .itcmram :
    {
        . = ALIGN(4);
        _sitcmram = .;
        *(.itcmram)
        *(.itcmram*)
        . = ALIGN(4);
        _eitcmram = .;
    } >ITCMRAM AT>FLASH

    /* -------------------------------------------------------------------------
     * C runtime heap and MSP stack — at the top of DTCMRAM
     * DTCMRAM gives the interrupt stack zero-wait-state access for fast ISR entry.
     * ------------------------------------------------------------------------- */
    ._user_heap_stack :
    {
        . = ALIGN(8);
        PROVIDE(end = .);       /* Used by newlib's _sbrk */
        PROVIDE(_end = .);
        . = . + _Min_Heap_Size;
        . = . + _Min_Stack_Size;
        . = ALIGN(8);
    } >DTCMRAM

    /* _estack is placed at the TOP of DTCMRAM (stack grows downward) */
    _estack = ORIGIN(DTCMRAM) + LENGTH(DTCMRAM);

    /* -------------------------------------------------------------------------
     * Debug / symbol sections — discarded from the binary
     * ------------------------------------------------------------------------- */
    /DISCARD/ :
    {
        libc.a(*)
        libm.a(*)
        libgcc.a(*)
    }

    .ARM.attributes 0 : { *(.ARM.attributes) }
}

/* =============================================================================
 * Sanity checks — the linker will report an error if violated
 * ============================================================================= */

/* Check that the FreeRTOS heap fits in D2 SRAM */
ASSERT((_efreertos_heap - _sfreertos_heap) <= LENGTH(RAM_D2),
       "FreeRTOS heap overflows RAM_D2 (D2 SRAM)")

/* Check minimum stack space was satisfied */
ASSERT((_estack - ORIGIN(DTCMRAM)) >= _Min_Stack_Size,
       "MSP stack does not fit in DTCMRAM")
