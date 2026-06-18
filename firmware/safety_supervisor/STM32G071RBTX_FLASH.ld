/* =============================================================================
 * STM32G071RBTX_FLASH.ld
 * Linker script for STM32G071RBT6 — Lumina safety supervisor MCU
 *
 * MCU resources:
 *   Flash : 128 KB @ 0x08000000
 *   RAM   :  36 KB @ 0x20000000 (single contiguous SRAM block)
 *
 * No RTOS, no dynamic heap — the safety supervisor is a bare-metal superloop.
 * All stack and buffer allocations are static and verified at link time.
 *
 * Stack:
 *   _estack placed at top of RAM (0x20009000).
 *   MSP is the only stack pointer used (no PSP / process stack).
 *
 * ============================================================================= */

ENTRY(Reset_Handler)

_Min_Heap_Size  = 0x0;      /* No dynamic heap — static allocation only */
_Min_Stack_Size = 0x800;    /* 2 KB minimum main stack                  */

/* =============================================================================
 * MEMORY MAP
 * ============================================================================= */
MEMORY
{
    /* Internal flash — 128 KB, read/execute */
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 128K

    /* Internal SRAM — 36 KB, read/write/execute */
    RAM   (xrw) : ORIGIN = 0x20000000, LENGTH = 36K
}

/* =============================================================================
 * SECTIONS
 * ============================================================================= */
SECTIONS
{
    /* -------------------------------------------------------------------------
     * Interrupt vector table
     * Must be at 0x08000000 immediately after reset.
     * VTOR is set by the startup code if the bootloader relocates the application.
     * ------------------------------------------------------------------------- */
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } >FLASH

    /* -------------------------------------------------------------------------
     * Code
     * ------------------------------------------------------------------------- */
    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text*)
        *(.glue_7)
        *(.glue_7t)
        *(.eh_frame)
        KEEP(*(.init))
        KEEP(*(.fini))
        . = ALIGN(4);
        _etext = .;
    } >FLASH

    /* -------------------------------------------------------------------------
     * Read-only data
     * ------------------------------------------------------------------------- */
    .rodata :
    {
        . = ALIGN(4);
        *(.rodata)
        *(.rodata*)
        . = ALIGN(4);
    } >FLASH

    /* ARM exception unwinding (needed for CMSIS fault handlers) */
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
     * Constructor/destructor arrays
     * The safety supervisor is C-only, but these sections are kept for
     * compatibility with the HAL startup code which may reference them.
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

    /* Load address of .data initialisation image in flash */
    _sidata = LOADADDR(.data);

    /* -------------------------------------------------------------------------
     * Initialised data — loaded from flash, run from RAM
     * ------------------------------------------------------------------------- */
    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data)
        *(.data*)
        *(.RamFunc)
        *(.RamFunc*)
        . = ALIGN(4);
        _edata = .;
    } >RAM AT>FLASH

    /* -------------------------------------------------------------------------
     * Zero-initialised BSS
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
    } >RAM

    /* -------------------------------------------------------------------------
     * Safety-critical state variables — grouped for easy watchdog verification
     * Place variables here with: __attribute__((section(".safety_state")))
     * Example: volatile SafetyState_t g_safety_state __attribute__((section(".safety_state")));
     * ------------------------------------------------------------------------- */
    .safety_state (NOLOAD) :
    {
        . = ALIGN(4);
        _s_safety_state = .;
        *(.safety_state)
        *(.safety_state*)
        . = ALIGN(4);
        _e_safety_state = .;
    } >RAM

    /* -------------------------------------------------------------------------
     * Stack — at top of RAM, MSP only (no FreeRTOS PSP)
     * The .stack_dummy section is just a size reservation; _estack points to
     * the absolute top of RAM which is the initial MSP value in the vector table.
     * ------------------------------------------------------------------------- */
    ._user_heap_stack :
    {
        . = ALIGN(8);
        PROVIDE(end = .);
        PROVIDE(_end = .);
        /* No heap (_Min_Heap_Size = 0) */
        . = . + _Min_Stack_Size;
        . = ALIGN(8);
    } >RAM

    /* Initial MSP value: top of RAM */
    _estack = ORIGIN(RAM) + LENGTH(RAM);

    /* -------------------------------------------------------------------------
     * Debug attributes — not loaded into the target binary
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
 * Sanity checks
 * ============================================================================= */

/* Ensure minimum stack fits in RAM after all sections */
ASSERT((_estack - ABSOLUTE(.)) >= _Min_Stack_Size,
       "Insufficient RAM for minimum stack size")

/* Safety state section must not overflow RAM */
ASSERT(_e_safety_state <= (ORIGIN(RAM) + LENGTH(RAM)),
       "Safety state variables overflow RAM")
