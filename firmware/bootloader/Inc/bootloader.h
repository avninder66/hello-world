/**
 * @file    bootloader.h
 * @brief   Lumina LCU-100 Secure Bootloader — public interface
 *
 * Flash layout (STM32H743, 2 MB internal flash):
 *
 *   0x08000000 - 0x0800FFFF   Bootloader          (64 KB)
 *   0x08010000 - 0x080FFFFF   Application slot A  (960 KB)
 *   0x08100000 - 0x081FFFFF   Application slot B  (1 MB)  — safe/factory image
 *
 * Image header format (32 bytes, at the start of each slot):
 *
 *   Offset  Size  Field
 *   0       4     magic         0x4C554D41 ("LUMA")
 *   4       4     version       major(16):minor(16)  e.g. 0x00010002 = 1.2
 *   8       4     image_size    bytes of image body (excluding this 32-byte header)
 *   12      4     image_crc32   CRC32 (IEEE 802.3) of image body only
 *   16      4     flags         bit 0 = permanent, bit 1 = tested_ok
 *   20      12    fw_version_str  null-padded ASCII e.g. "1.0.0-rc1\0\0\0"
 *   32      4     reserved      write 0x00000000
 *
 * Lumina Technology Ltd / Amber-RTM — commercial in confidence.
 * Not for public highway deployment.
 */

#ifndef BOOTLOADER_H
#define BOOTLOADER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Flash address map
 * ------------------------------------------------------------------------- */

/** Start of bootloader region (sector 0, 128 KB bank) */
#define BOOTLOADER_BASE         0x08000000UL

/** Start of application slot A — primary image */
#define SLOT_A_BASE             0x08010000UL

/** Start of application slot B — safe / factory fallback image */
#define SLOT_B_BASE             0x08100000UL

/** Maximum usable size of slot A image body (excl. 32-byte header) */
#define SLOT_A_MAX_SIZE         (0x000F0000UL - sizeof(image_header_t))  /* ~959.97 KB */

/** Maximum usable size of slot B image body (excl. 32-byte header) */
#define SLOT_B_MAX_SIZE         (0x00100000UL - sizeof(image_header_t))  /* ~1023.97 KB */

/** STM32H743 internal ROM bootloader (DFU / USB) — from DS12110 Table 7 */
#define STM32H743_SYSBOOT_ADDR  0x1FF09800UL

/* ---------------------------------------------------------------------------
 * Image header
 * ------------------------------------------------------------------------- */

/** Magic number "LUMA" (0x4C 0x55 0x4D 0x41) */
#define IMAGE_MAGIC             0x4C554D41UL

/** image_header_t.flags bit definitions */
#define IMAGE_FLAG_PERMANENT    (1U << 0)   /**< Do not overwrite this slot */
#define IMAGE_FLAG_TESTED_OK    (1U << 1)   /**< Image has completed a successful boot cycle */

/**
 * @brief Image header structure.
 *
 * This struct must be exactly 32 bytes. It is placed at the very start of
 * each flash slot (SLOT_A_BASE and SLOT_B_BASE).
 *
 * The CRC32 covers only the image body that immediately follows the header —
 * it does NOT include the header itself.
 */
typedef struct __attribute__((packed)) {
    uint32_t magic;             /**< Must equal IMAGE_MAGIC (0x4C554D41)   */
    uint32_t version;           /**< Packed version: major[31:16]:minor[15:0] */
    uint32_t image_size;        /**< Image body length in bytes (excl. header) */
    uint32_t image_crc32;       /**< CRC32 (IEEE 802.3) of image body       */
    uint32_t flags;             /**< IMAGE_FLAG_* bitmask                   */
    uint8_t  fw_version_str[12];/**< Null-padded ASCII string, e.g. "1.0.0-rc1" */
    uint32_t reserved;          /**< Write 0x00000000                       */
} image_header_t;

/* Verify struct is exactly 32 bytes at compile time */
_Static_assert(sizeof(image_header_t) == 32,
               "image_header_t must be exactly 32 bytes");

/* ---------------------------------------------------------------------------
 * Boot result codes
 * ------------------------------------------------------------------------- */

/**
 * @brief Result of the boot sequence.
 */
typedef enum {
    BOOT_OK        = 0, /**< Slot A was valid; jumped to application normally    */
    BOOT_FALLBACK  = 1, /**< Slot A invalid; fell back to slot B and jumped      */
    BOOT_DFU       = 2, /**< Both slots invalid or DFU forced; entered DFU mode  */
    BOOT_FAILED    = 3  /**< Unrecoverable internal error (should not be reached) */
} boot_result_t;

/* ---------------------------------------------------------------------------
 * Function prototypes
 * ------------------------------------------------------------------------- */

/**
 * @brief  Verify an image at the given flash slot address.
 *
 * Reads the image_header_t at @p slot_addr, checks the magic number, then
 * computes the CRC32 of the image body (the @p image_size bytes immediately
 * following the header) and compares it against @p image_crc32 in the header.
 *
 * @param  slot_addr  Base address of the slot to verify (SLOT_A_BASE or
 *                    SLOT_B_BASE).
 * @return true   Header magic matches AND computed CRC32 matches header CRC32.
 * @return false  Magic mismatch, size out of range, or CRC32 mismatch.
 */
bool bootloader_verify_image(uint32_t slot_addr);

/**
 * @brief  Jump to the application at @p app_addr.
 *
 * This function:
 *   1. Disables all interrupts (PRIMASK).
 *   2. De-initialises the HAL (resets SysTick, clears pending IRQs).
 *   3. Sets the MSP from the first 32-bit word at @p app_addr.
 *   4. Jumps to the reset handler at @p app_addr + 4.
 *
 * This function does not return if the application is valid. If the vector
 * table at @p app_addr is corrupt, behaviour is undefined.
 *
 * @param  app_addr  Start address of the application image body (i.e.
 *                   SLOT_A_BASE + sizeof(image_header_t)).
 */
void bootloader_jump_to_application(uint32_t app_addr);

/**
 * @brief  Enter the STM32H743 internal USB DFU bootloader.
 *
 * Remaps execution to the ST system memory ROM at STM32H743_SYSBOOT_ADDR and
 * jumps to its reset handler. The USB-C service port on the LCU-100 board will
 * enumerate as a DFU device.
 *
 * This function does not return.
 */
void bootloader_enter_dfu(void);

/**
 * @brief  Erase slot A and copy the slot B image into it.
 *
 * Used when slot A is invalid but slot B contains a valid fallback image.
 * Erases the flash pages covering slot A, then copies @p size bytes from
 * @p src to @p dst using 32-bit word writes.
 *
 * @param  src   Source address (typically SLOT_B_BASE).
 * @param  dst   Destination address (typically SLOT_A_BASE).
 * @param  size  Number of bytes to copy (must include the header; i.e.
 *               sizeof(image_header_t) + image_size from the slot B header).
 * @return true  Copy completed and flash verify passed.
 * @return false Flash erase or write error.
 */
bool bootloader_copy_slot(uint32_t src, uint32_t dst, uint32_t size);

/**
 * @brief  Software CRC32 computation (IEEE 802.3 / Ethernet polynomial).
 *
 * Processes @p length bytes starting at @p data. Initialises with 0xFFFFFFFF
 * and applies final XOR of 0xFFFFFFFF, matching the standard IEEE 802.3
 * result.
 *
 * @param  data    Pointer to data buffer.
 * @param  length  Number of bytes to process.
 * @return 32-bit CRC value.
 */
uint32_t crc32_compute(const uint8_t *data, uint32_t length);

#ifdef __cplusplus
}
#endif

#endif /* BOOTLOADER_H */
