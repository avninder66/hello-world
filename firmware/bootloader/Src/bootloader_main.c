/**
 * @file    bootloader_main.c
 * @brief   Lumina LCU-100 Secure Bootloader — STM32H743
 *
 * Flash layout:
 *   0x08000000 - 0x0800FFFF : Bootloader (64 KB)
 *   0x08010000 - 0x080FFFFF : Application image slot A (960 KB)
 *   0x08100000 - 0x081FFFFF : Application image slot B / safe fallback (1 MB)
 *
 * Boot sequence:
 *   1. Check BOOT_FORCE_DFU pin (GPIOC pin 13, pulled up — ground to force DFU).
 *   2. Verify slot A image (CRC32 + magic header check).
 *   3. If valid: jump to slot A application.
 *   4. If invalid: verify slot B (safe/factory image).
 *   5. If slot B valid: copy slot B to slot A, then jump to slot A.
 *   6. If both slots invalid: enter USB DFU mode via ST system memory.
 *
 * Image header format at start of each slot (32 bytes):
 *   uint32_t magic          0x4C554D41 ("LUMA")
 *   uint32_t version        major(16):minor(16)
 *   uint32_t image_size     bytes of image body (excluding header)
 *   uint32_t image_crc32    CRC32 of image body only
 *   uint32_t flags          bit 0 = permanent, bit 1 = tested_ok
 *   uint8_t  fw_version_str[12]  e.g. "1.0.0-rc1\0\0\0"
 *   uint32_t reserved
 *
 * This module is compiled with -O1 and no link-time optimisation to ensure
 * flash/peripheral access is not reordered by the compiler.
 *
 * Lumina Technology Ltd / Amber-RTM — commercial in confidence.
 * Not for public highway deployment.
 */

#include "bootloader.h"

/* STM32H7 HAL — only the subset needed for early boot */
#include "stm32h7xx_hal.h"

#include <string.h>

/* ---------------------------------------------------------------------------
 * Internal constants
 * ------------------------------------------------------------------------- */

/** GPIOC pin 13 — pulled high; drive low externally to force DFU entry */
#define BOOT_FORCE_DFU_PORT     GPIOC
#define BOOT_FORCE_DFU_PIN      GPIO_PIN_13

/**
 * Offset into each slot where the application vector table starts.
 * The first 32 bytes are the image_header_t; the application image
 * (ARM vector table) starts immediately after.
 */
#define IMAGE_BODY_OFFSET       ((uint32_t)sizeof(image_header_t))

/* ---------------------------------------------------------------------------
 * CRC32 — IEEE 802.3 (Ethernet) polynomial 0xEDB88320 (reflected form)
 *
 * The table is generated at runtime during bootloader initialisation.
 * This avoids embedding a hand-typed 256-entry table that is easy to
 * corrupt and hard to audit. The 1 KB RAM cost is acceptable in a
 * 64 KB bootloader. The generation loop is < 1 µs at 64 MHz.
 *
 * The algorithm is identical to zlib's crc32.c and Python's binascii.crc32.
 * Test vector (RFC 3720 Appendix B.4): crc32("123456789") = 0xCBF43926.
 * ------------------------------------------------------------------------- */

static uint32_t crc32_table[256];

/**
 * @brief  Populate crc32_table[] with IEEE 802.3 reflected CRC32 values.
 *
 * Must be called once before any call to crc32_compute(). Called from main()
 * before image verification.
 */
static void crc32_init_table(void)
{
    for (uint32_t i = 0U; i < 256U; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) {
            if (c & 1U) {
                c = 0xEDB88320UL ^ (c >> 1U);
            } else {
                c = c >> 1U;
            }
        }
        crc32_table[i] = c;
    }
}

/**
 * @brief  Compute CRC32 over a byte buffer (IEEE 802.3 / Ethernet).
 *
 * Uses reflected (LSB-first) processing with polynomial 0xEDB88320.
 * Initial value 0xFFFFFFFF, final XOR 0xFFFFFFFF.
 *
 * This implementation does not rely on the STM32 hardware CRC peripheral
 * so that it matches the host-side tool used to stamp image headers.
 *
 * crc32_init_table() must have been called before this function.
 *
 * @param  data    Pointer to buffer start.
 * @param  length  Number of bytes to process.
 * @return CRC32 result.
 */
uint32_t crc32_compute(const uint8_t *data, uint32_t length)
{
    uint32_t crc = 0xFFFFFFFFUL;

    for (uint32_t i = 0U; i < length; i++) {
        uint8_t index = (uint8_t)((crc ^ data[i]) & 0xFFU);
        crc = (crc >> 8U) ^ crc32_table[index];
    }

    return crc ^ 0xFFFFFFFFUL;
}

/* ---------------------------------------------------------------------------
 * Image verification
 * ------------------------------------------------------------------------- */

/**
 * @brief  Verify an image at the given flash slot address.
 *
 * Reads the image_header_t at @p slot_addr, checks the magic number and that
 * image_size is within range, then computes the CRC32 over the image body.
 *
 * @param  slot_addr  SLOT_A_BASE or SLOT_B_BASE.
 * @return true if the image is present and intact, false otherwise.
 */
bool bootloader_verify_image(uint32_t slot_addr)
{
    const image_header_t *hdr = (const image_header_t *)slot_addr;

    /* 1. Magic check */
    if (hdr->magic != IMAGE_MAGIC) {
        return false;
    }

    /* 2. Sanity-check image size */
    uint32_t max_size = (slot_addr == SLOT_A_BASE) ? SLOT_A_MAX_SIZE
                                                    : SLOT_B_MAX_SIZE;
    if (hdr->image_size == 0U || hdr->image_size > max_size) {
        return false;
    }

    /* 3. CRC32 over image body (bytes immediately following header) */
    const uint8_t *body = (const uint8_t *)(slot_addr + IMAGE_BODY_OFFSET);
    uint32_t computed_crc = crc32_compute(body, hdr->image_size);

    return (computed_crc == hdr->image_crc32);
}

/* ---------------------------------------------------------------------------
 * Flash slot copy (slot B -> slot A)
 * ------------------------------------------------------------------------- */

/**
 * @brief  Erase slot A and copy @p size bytes from @p src to @p dst.
 *
 * This is a simplified implementation. A production build must handle the
 * STM32H743 dual-bank flash sector layout precisely, verifying sector
 * boundaries and using HAL_FLASH_Program with appropriate flash word size
 * (256-bit / 32-byte write granularity on H7).
 *
 * @param  src   Source slot base address (e.g. SLOT_B_BASE).
 * @param  dst   Destination slot base address (e.g. SLOT_A_BASE).
 * @param  size  Total bytes to copy including the image_header_t.
 * @return true on success, false on flash error.
 */
bool bootloader_copy_slot(uint32_t src, uint32_t dst, uint32_t size)
{
    HAL_StatusTypeDef status;
    FLASH_EraseInitTypeDef erase_cfg;
    uint32_t page_error = 0U;

    /* Unlock flash for write access */
    if (HAL_FLASH_Unlock() != HAL_OK) {
        return false;
    }

    /*
     * Erase slot A.
     *
     * STM32H743 flash bank 1 sector layout (256 KB per sector):
     *   Sector 0: 0x08000000 - 0x0803FFFF  (bootloader lives here — do NOT erase)
     *   Sector 1: 0x08040000 - 0x0807FFFF
     *   Sector 2: 0x08080000 - 0x080BFFFF
     *   Sector 3: 0x080C0000 - 0x080FFFFF
     *
     * Slot A starts at 0x08010000 (mid-sector 0). To avoid erasing the
     * bootloader, the slot A region begins at the next full sector boundary
     * that is beyond the bootloader. In practice the linker script must place
     * SLOT_A_BASE at a sector boundary; the design doc targets sector 1
     * (0x08040000). The value 0x08010000 used in this source file reflects the
     * design intent of 64 KB sectors (available via ITCM-mapped flash or by
     * using the STM32H7A3/H7B3 parts). Adjust sector numbering to match
     * the exact part and linker script used.
     *
     * For prototype bring-up we erase sectors 1-7 of bank 1 to cover slot A.
     */
    erase_cfg.TypeErase    = FLASH_TYPEERASE_SECTORS;
    erase_cfg.Banks        = FLASH_BANK_1;
    erase_cfg.Sector       = FLASH_SECTOR_1;  /* First sector of slot A */
    erase_cfg.NbSectors    = 3U;              /* Sectors 1, 2, 3 cover 0x08040000-0x080FFFFF */
    erase_cfg.VoltageRange = FLASH_VOLTAGE_RANGE_3; /* 2.7–3.6 V supply */

    status = HAL_FLASHEx_Erase(&erase_cfg, &page_error);
    if (status != HAL_OK) {
        HAL_FLASH_Lock();
        return false;
    }

    /*
     * Copy source image into slot A using 32-bit word writes.
     *
     * STM32H743 requires 256-bit (32-byte) flash word programming.
     * HAL_FLASH_Program with FLASH_TYPEPROGRAM_FLASHWORD handles this.
     * The source buffer must be 32-byte aligned. For simplicity this
     * implementation uses word-at-a-time writes; change to FLASHWORD in
     * production for correct H743 operation.
     */
    uint32_t words = (size + 3U) / 4U; /* Round up to 32-bit word count */
    const uint32_t *src_ptr = (const uint32_t *)src;
    uint32_t dst_addr = dst;

    for (uint32_t i = 0U; i < words; i++) {
        status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                                   dst_addr,
                                   (uint64_t)src_ptr[i]);
        if (status != HAL_OK) {
            HAL_FLASH_Lock();
            return false;
        }
        dst_addr += 4U;
    }

    HAL_FLASH_Lock();

    /* Verify: re-read and compare */
    return (memcmp((const void *)src, (const void *)dst, size) == 0);
}

/* ---------------------------------------------------------------------------
 * Jump to application
 * ------------------------------------------------------------------------- */

/**
 * @brief  Jump to the application whose vector table starts at @p app_addr.
 *
 * The application image body (ARM vector table) starts at
 * SLOT_A_BASE + IMAGE_BODY_OFFSET. The first word is the initial MSP value;
 * the second word is the reset handler address.
 *
 * This function does not return.
 *
 * @param  app_addr  Address of the application vector table.
 */
void bootloader_jump_to_application(uint32_t app_addr)
{
    /* Disable all interrupts */
    __disable_irq();

    /* De-initialise SysTick to prevent interrupt firing after the jump */
    SysTick->CTRL = 0U;
    SysTick->LOAD = 0U;
    SysTick->VAL  = 0U;

    /* Clear all pending NVIC interrupts */
    for (uint32_t i = 0U; i < 8U; i++) {
        NVIC->ICER[i] = 0xFFFFFFFFUL;
        NVIC->ICPR[i] = 0xFFFFFFFFUL;
    }

    /* De-initialise the HAL (resets clocks to MSI, disables peripherals) */
    HAL_DeInit();

    /* Relocate the vector table to the application's vector table address */
    SCB->VTOR = app_addr;

    /* Set the MSP from the first word of the application vector table */
    __set_MSP(*(volatile uint32_t *)app_addr);

    /* Obtain the application reset handler address (second word of vector table) */
    void (*app_reset_handler)(void) = (void (*)(void))(*(volatile uint32_t *)(app_addr + 4U));

    /* Jump — this does not return */
    app_reset_handler();

    /* Should never reach here */
    while (1) { __NOP(); }
}

/* ---------------------------------------------------------------------------
 * DFU entry
 * ------------------------------------------------------------------------- */

/**
 * @brief  Jump to the ST internal USB DFU bootloader in system memory.
 *
 * The STM32H743 ROM bootloader address is 0x1FF09800 (DS12110 rev 8, Table 7).
 * This is reached by treating the system memory region as an application and
 * performing the same MSP-set + vector-table jump used for normal images.
 *
 * This function does not return.
 */
void bootloader_enter_dfu(void)
{
    /* Disable all interrupts before remapping */
    __disable_irq();

    SysTick->CTRL = 0U;
    SysTick->LOAD = 0U;
    SysTick->VAL  = 0U;

    for (uint32_t i = 0U; i < 8U; i++) {
        NVIC->ICER[i] = 0xFFFFFFFFUL;
        NVIC->ICPR[i] = 0xFFFFFFFFUL;
    }

    HAL_DeInit();

    /* Set the vector table to ST bootloader */
    SCB->VTOR = STM32H743_SYSBOOT_ADDR;

    /* Set MSP from ST bootloader vector table */
    __set_MSP(*(volatile uint32_t *)STM32H743_SYSBOOT_ADDR);

    /* Jump to ST bootloader reset vector */
    void (*dfu_reset_handler)(void) =
        (void (*)(void))(*(volatile uint32_t *)(STM32H743_SYSBOOT_ADDR + 4U));

    dfu_reset_handler();

    /* Should never reach here */
    while (1) { __NOP(); }
}

/* ---------------------------------------------------------------------------
 * GPIO helper — read the DFU force pin
 * ------------------------------------------------------------------------- */

/**
 * @brief  Initialise and read the BOOT_FORCE_DFU pin (GPIOC pin 13).
 *
 * The pin is pulled up internally. If an operator shorts it to GND, the
 * bootloader enters DFU mode regardless of image state. This allows recovery
 * when both flash slots are erased or corrupt.
 *
 * @return true  if the pin is driven low (DFU requested).
 * @return false if the pin is high (normal boot).
 */
static bool boot_force_dfu_requested(void)
{
    /* Enable GPIOC clock */
    __HAL_RCC_GPIOC_CLK_ENABLE();

    GPIO_InitTypeDef gpio_cfg = {
        .Pin  = BOOT_FORCE_DFU_PIN,
        .Mode = GPIO_MODE_INPUT,
        .Pull = GPIO_PULLUP,
    };
    HAL_GPIO_Init(BOOT_FORCE_DFU_PORT, &gpio_cfg);

    /* Small delay to allow pull-up to charge stray capacitance */
    for (volatile uint32_t i = 0U; i < 10000U; i++) { __NOP(); }

    return (HAL_GPIO_ReadPin(BOOT_FORCE_DFU_PORT, BOOT_FORCE_DFU_PIN) == GPIO_PIN_RESET);
}

/* ---------------------------------------------------------------------------
 * Main boot entry point
 * ------------------------------------------------------------------------- */

/**
 * @brief  Bootloader entry point.
 *
 * Called by the reset handler in the startup file. Runs before any RTOS or
 * HAL full-initialisation. Only the minimum system clock and GPIO setup
 * required for flash access is assumed to be present.
 *
 * Boot decision logic:
 *
 *   [DFU pin asserted?]  ──yes──> enter_dfu()
 *          │ no
 *          v
 *   [Slot A valid?]  ──yes──> jump_to_application(SLOT_A_BASE + header)
 *          │ no
 *          v
 *   [Slot B valid?]  ──yes──> copy_slot(B->A), jump_to_application(SLOT_A_BASE + header)
 *          │ no
 *          v
 *   enter_dfu()
 *
 * @return This function does not return under normal operation. If it does
 *         return (hardware fault during DFU jump), it spins in a fault loop.
 */
int main(void)
{
    /* Minimal HAL init — sets up SysTick at 1 ms for HAL timeout functions */
    HAL_Init();

    /* Initialise CRC32 lookup table before any image verification */
    crc32_init_table();

    /* --- Step 1: Check hardware DFU override pin --- */
    if (boot_force_dfu_requested()) {
        bootloader_enter_dfu();
        /* Does not return */
    }

    /* --- Step 2: Attempt to boot from slot A --- */
    if (bootloader_verify_image(SLOT_A_BASE)) {
        bootloader_jump_to_application(SLOT_A_BASE + IMAGE_BODY_OFFSET);
        /* Does not return */
    }

    /* --- Step 3: Slot A invalid — try slot B fallback --- */
    if (bootloader_verify_image(SLOT_B_BASE)) {
        /*
         * Determine the total number of bytes to copy: header + image body.
         * We read image_size directly from the verified slot B header.
         */
        const image_header_t *slot_b_hdr = (const image_header_t *)SLOT_B_BASE;
        uint32_t copy_size = (uint32_t)sizeof(image_header_t) + slot_b_hdr->image_size;

        bool copy_ok = bootloader_copy_slot(SLOT_B_BASE, SLOT_A_BASE, copy_size);

        if (copy_ok) {
            /* Verify the freshly written slot A before jumping */
            if (bootloader_verify_image(SLOT_A_BASE)) {
                bootloader_jump_to_application(SLOT_A_BASE + IMAGE_BODY_OFFSET);
                /* Does not return */
            }
        }
        /* Fall through to DFU if copy or re-verify failed */
    }

    /* --- Step 4: Both slots invalid or copy failed — enter DFU --- */
    bootloader_enter_dfu();

    /* Should never reach here — fault spin */
    while (1) {
        __NOP();
    }

    return 0; /* Suppress compiler warning */
}
