/**
 * @file    fault_log.c
 * @brief   LCU-100 FRAM-backed fault log — implementation
 *
 * The fault log is stored on a Fujitsu MB85RS4MT 4 Mbit (512 KB) FRAM chip
 * connected to SPI1 with chip-select on GPIOD pin 14.
 *
 * Memory layout
 * -------------
 *   0x000000 – 0x00001F  fault_log_header_t  (32 bytes)
 *   0x000020 – 0x07FFFF  fault_log_entry_t[] (ring buffer, ~16 000 entries)
 *
 * The header contains a magic word, a write index (next entry to overwrite),
 * a total entries counter, and a CRC-32 over the header fields.
 *
 * Fault entries are written in a circular ring.  When the ring is full the
 * oldest entry is silently overwritten.  The total_entries counter grows
 * monotonically so observers can detect log wraps.
 *
 * SPI protocol (MB85RS4MT)
 * ------------------------
 *   WREN  (0x06): single byte, CS cycle required before every WRITE
 *   READ  (0x03): 0x03 + 3-byte address + data bytes
 *   WRITE (0x02): 0x06 CS cycle, then 0x02 + 3-byte address + data bytes
 *
 * The MB85RS4MT supports unlimited write endurance and does not require
 * a write-complete poll (unlike flash).
 *
 * Thread safety
 * -------------
 * All SPI accesses take xSPIMutex (100 ms timeout) defined in safety_comm.h.
 * fault_log_task() processes xFaultQueue asynchronously so the traffic
 * engine can post faults without blocking on FRAM I/O.
 */

#include "fault_codes.h"
#include "main.h"
#include "safety_comm.h"    /* for xSPIMutex */

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * FRAM geometry
 * ============================================================================ */

/** Total FRAM capacity in bytes: 4 Mbit = 512 KB */
#define FRAM_SIZE                   524288UL

/** Base address of the fault log in FRAM */
#define FRAM_BASE                   0x000000UL

/** MB85RS4MT SPI opcodes */
#define FRAM_OP_WREN                0x06U   /**< Write Enable Latch           */
#define FRAM_OP_WRDI                0x04U   /**< Write Disable Latch          */
#define FRAM_OP_RDSR                0x05U   /**< Read Status Register         */
#define FRAM_OP_WRSR                0x01U   /**< Write Status Register        */
#define FRAM_OP_READ                0x03U   /**< Read Memory                  */
#define FRAM_OP_WRITE               0x02U   /**< Write Memory                 */
#define FRAM_OP_RDID                0x9FU   /**< Read Device ID               */

/** SPI timeout for each transaction (ms) */
#define FRAM_SPI_TIMEOUT_MS         50U

/** Header magic — "LUMI" in ASCII, little-endian as seen in memory */
#define FAULT_LOG_MAGIC             0x4C554D49UL

/** Fault log format version */
#define FAULT_LOG_VERSION           1U

/* ============================================================================
 * Data structures
 * ============================================================================ */

/**
 * @brief  Fault log header — stored at FRAM address 0x000000.
 *
 * Total size must remain <= 32 bytes so entries start at a
 * naturally-aligned offset.  Current size: 20 bytes + 12 bytes pad = 32.
 */
typedef struct __attribute__((packed)) {
    uint32_t magic;             /**< FAULT_LOG_MAGIC = 0x4C554D49             */
    uint8_t  version;           /**< Format version = FAULT_LOG_VERSION       */
    uint8_t  reserved[3];       /**< Alignment padding                        */
    uint32_t write_index;       /**< Next slot to write (modulo MAX_LOG_ENTRIES) */
    uint32_t total_entries;     /**< Monotonic count of all entries ever written */
    uint32_t crc32;             /**< CRC-32 of all preceding header bytes     */
    uint8_t  pad[8];            /**< Pad to 32 bytes                          */
} fault_log_header_t;

/**
 * @brief  A single fault log entry stored in FRAM.
 *
 * Mirrors fault_event_t from fault_codes.h plus a sequence number.
 * Size: 28 bytes.
 */
typedef struct __attribute__((packed)) {
    uint32_t         sequence;      /**< Unique monotonic sequence number     */
    uint32_t         timestamp_ms;  /**< HAL_GetTick() at fault occurrence    */
    uint16_t         fault_code;    /**< fault_code_t value                   */
    uint8_t          severity;      /**< fault_severity_t value               */
    uint8_t          source;        /**< fault_source_t value                 */
    uint32_t         context[2];    /**< Source-specific diagnostic data      */
    uint32_t         entry_crc32;   /**< CRC-32 of preceding entry bytes      */
} fault_log_entry_t;

/** Maximum number of entries that fit in FRAM after the header */
#define MAX_LOG_ENTRIES  \
    ((uint32_t)((FRAM_SIZE - sizeof(fault_log_header_t)) / sizeof(fault_log_entry_t)))

/** Byte address of first entry slot */
#define FAULT_ENTRY_BASE  (FRAM_BASE + sizeof(fault_log_header_t))

/* ============================================================================
 * CRC-32 (IEEE 802.3 polynomial 0xEDB88320, reflected)
 * ============================================================================ */

static const uint32_t k_crc32_table[256] = {
    0x00000000UL, 0x77073096UL, 0xEE0E612CUL, 0x990951BAUL,
    0x076DC419UL, 0x706AF48FUL, 0xE963A535UL, 0x9E6495A3UL,
    0x0EDB8832UL, 0x79DCB8A4UL, 0xE0D5E91BUL, 0x97D2D988UL,
    0x09B64C2BUL, 0x7EB17CBDUL, 0xE7B82D08UL, 0x90BF1CBEUL,
    0x1DB71064UL, 0x6AB020F2UL, 0xF3B97148UL, 0x84BE41DEUL,
    0x1ADAD47DUL, 0x6DDDE4EBUL, 0xF4D4B551UL, 0x83D385C7UL,
    0x136C9856UL, 0x646BA8C0UL, 0xFD62F97AUL, 0x8A65C9ECUL,
    0x14015C4FUL, 0x63066CD9UL, 0xFA0F3D63UL, 0x8D080DF5UL,
    0x3B6E20C8UL, 0x4C69105EUL, 0xD56041E4UL, 0xA2677172UL,
    0x3C03E4D1UL, 0x4B04D447UL, 0xD20D85FDUL, 0xA50AB56BUL,
    0x35B5A8FAUL, 0x42B2986CUL, 0xDBBBC9D6UL, 0xACBCF940UL,
    0x32D86CE3UL, 0x45DF5C75UL, 0xDCD60DCFUL, 0xABD13D59UL,
    0x26D930ACUL, 0x51DE003AUL, 0xC8D75180UL, 0xBFD06116UL,
    0x21B4F927UL, 0x56B3C9B1UL, 0xCFBA9200UL, 0xB8BDA246UL,
    0x2802B89EUL, 0x5F058808UL, 0xC60CD9B2UL, 0xB10BE924UL,
    0x2F6F7C87UL, 0x58684C11UL, 0xC1611DABUL, 0xB6662D3DUL,
    0x76DC4190UL, 0x01DB7106UL, 0x98D220BCUL, 0xEFD5102AUL,
    0x71B18589UL, 0x06B6B51FUL, 0x9FBFE4A5UL, 0xE8B8D433UL,
    0x7807C9A2UL, 0x0F00F934UL, 0x9609A88EUL, 0xE10E9818UL,
    0x7F6AD9BBUL, 0x086D3D2DUL, 0x91646C97UL, 0xE6635C01UL,
    0x6B6B51F4UL, 0x1C6C6162UL, 0x856530D8UL, 0xF262004EUL,
    0x6C0695EDUL, 0x1B01A57BUL, 0x8208F4C1UL, 0xF50FC457UL,
    0x65B0D9C6UL, 0x12B7E950UL, 0x8BBEB8EAUL, 0xFCB9887CUL,
    0x62DD1DDFUL, 0x15DA2D49UL, 0x8CD37CF3UL, 0xFBD44C65UL,
    0x4DB26158UL, 0x3AB551CEUL, 0xA3BC0074UL, 0xD4BB30E2UL,
    0x4ADFA541UL, 0x3DD895D7UL, 0xA4D1C46DUL, 0xD3D6F4FBUL,
    0x4369E96AUL, 0x346ED9FCUL, 0xAD678846UL, 0xDA60B8D0UL,
    0x44042D73UL, 0x33031DE5UL, 0xAA0A4C5FUL, 0xDD0D7CC9UL,
    0x5005713CUL, 0x270241AAUL, 0xBE0B1010UL, 0xC90C2086UL,
    0x5768B525UL, 0x206F85B3UL, 0xB966D409UL, 0xCE61E49FUL,
    0x5EDEF90EUL, 0x29D9C998UL, 0xB0D09822UL, 0xC7D7A8B4UL,
    0x59B33D17UL, 0x2EB40D81UL, 0xB7BD5C3BUL, 0xC0BA6CADUL,
    0xEDB88320UL, 0x9ABFB3B6UL, 0x03B6E20CUL, 0x74B1D29AUL,
    0xEAD54739UL, 0x9DD277AFUL, 0x04DB2615UL, 0x73DC1683UL,
    0xE3630B12UL, 0x94643B84UL, 0x0D6D6A3EUL, 0x7A6A5AA8UL,
    0xE40ECF0BUL, 0x9309FF9DUL, 0x0A00AE27UL, 0x7D079EB1UL,
    0xF00F9344UL, 0x8708A3D2UL, 0x1E01F268UL, 0x6906C2FEUL,
    0xF762575DUL, 0x806567CBUL, 0x196C3671UL, 0x6E6B06E7UL,
    0xFED41B76UL, 0x89D32BE0UL, 0x10DA7A5AUL, 0x67DD4ACCUL,
    0xF9B9DF6FUL, 0x8EBEEFF9UL, 0x17B7BE43UL, 0x60B08ED5UL,
    0xD6D6A3E8UL, 0xA1D1937EUL, 0x38D8C2C4UL, 0x4FDFF252UL,
    0xD1BB67F1UL, 0xA6BC5767UL, 0x3FB506DDUL, 0x48B2364BUL,
    0xD80D2BDAUL, 0xAF0A1B4CUL, 0x36034AF6UL, 0x41047A60UL,
    0xDF60EFC3UL, 0xA8670955UL, 0x316658EFUL, 0x46616879UL,
    0xCB61B38CUL, 0xBC66831AUL, 0x256FD2A0UL, 0x5268E236UL,
    0xCC0C7795UL, 0xBB0B4703UL, 0x220216B9UL, 0x5505262FUL,
    0xC5BA3BBEUL, 0xB2BD0B28UL, 0x2BB45A92UL, 0x5CB36A04UL,
    0xC2D7FFA7UL, 0xB5D0CF31UL, 0x2CD99E8BUL, 0x5BDEAE1DUL,
    0x9B64C2B0UL, 0xEC63F226UL, 0x756AA39CUL, 0x026D930AUL,
    0x9C0906A9UL, 0xEB0E363FUL, 0x72076785UL, 0x05005713UL,
    0x95BF4A82UL, 0xE2B87A14UL, 0x7BB12BAEUL, 0x0CB61B38UL,
    0x92D28E9BUL, 0xE5D5BE0DUL, 0x7CDCEFB7UL, 0x0BDBDF21UL,
    0x86D3D2D4UL, 0xF1D4E242UL, 0x68DDB3F8UL, 0x1FDA836EUL,
    0x81BE16CDUL, 0xF6B9265BUL, 0x6FB077E1UL, 0x18B74777UL,
    0x88085AE6UL, 0xFF0F6A70UL, 0x66063BCAUL, 0x11010B5CUL,
    0x8F659EFFUL, 0xF862AE69UL, 0x616BFFD3UL, 0x166CCF45UL,
    0xA00AE278UL, 0xD70DD2EEUL, 0x4E048354UL, 0x3903B3C2UL,
    0xA7672661UL, 0xD06016F7UL, 0x4969474DUL, 0x3E6E77DBUL,
    0xAED16A4AUL, 0xD9D65ADCUL, 0x40DF0B66UL, 0x37D83BF0UL,
    0xA9BCAE53UL, 0xDEBB9EC5UL, 0x47B2CF7FUL, 0x30B5FFE9UL,
    0xBDBDF21CUL, 0xCABAC28AUL, 0x53B39330UL, 0x24B4A3A6UL,
    0xBAD03605UL, 0xCDD70693UL, 0x54DE5729UL, 0x23D967BFUL,
    0xB3667A2EUL, 0xC4614AB8UL, 0x5D681B02UL, 0x2A6F2B94UL,
    0xB40BBE37UL, 0xC30C8EA1UL, 0x5A05DF1BUL, 0x2D02EF8DUL,
};

static uint32_t crc32_compute(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFUL;
    for (size_t i = 0; i < len; i++) {
        uint8_t idx = (uint8_t)((crc ^ data[i]) & 0xFFU);
        crc = (crc >> 8) ^ k_crc32_table[idx];
    }
    return crc ^ 0xFFFFFFFFUL;
}

/* ============================================================================
 * Module state
 * ============================================================================ */

/** RAM shadow of the FRAM header — kept in sync after every write */
static fault_log_header_t s_header;

/** True once fault_log_init() has succeeded */
static bool s_initialised = false;

/* ============================================================================
 * Private helpers
 * ============================================================================ */

/**
 * @brief  Assert FRAM SPI chip-select (active low).
 */
static inline void fram_cs_assert(void)
{
    HAL_GPIO_WritePin(FRAM_CS_GPIO_Port, FRAM_CS_Pin, GPIO_PIN_RESET);
}

/**
 * @brief  De-assert FRAM SPI chip-select.
 */
static inline void fram_cs_deassert(void)
{
    HAL_GPIO_WritePin(FRAM_CS_GPIO_Port, FRAM_CS_Pin, GPIO_PIN_SET);
}

/**
 * @brief  Send WREN (Write Enable Latch) opcode.
 *         Must be called immediately before every WRITE sequence.
 */
static void fram_send_wren(void)
{
    uint8_t opcode = FRAM_OP_WREN;
    fram_cs_assert();
    HAL_SPI_Transmit(&hspi1, &opcode, 1U, FRAM_SPI_TIMEOUT_MS);
    fram_cs_deassert();
}

/**
 * @brief  Read len bytes from FRAM address addr into buf.
 *
 * @param addr   24-bit FRAM byte address.
 * @param buf    Destination buffer.
 * @param len    Number of bytes to read.
 * @return HAL_OK on success.
 */
static HAL_StatusTypeDef fram_read(uint32_t addr, uint8_t *buf, size_t len)
{
    uint8_t cmd[4];
    cmd[0] = FRAM_OP_READ;
    cmd[1] = (uint8_t)((addr >> 16) & 0xFFU);
    cmd[2] = (uint8_t)((addr >>  8) & 0xFFU);
    cmd[3] = (uint8_t)((addr      ) & 0xFFU);

    fram_cs_assert();
    HAL_StatusTypeDef status = HAL_SPI_Transmit(&hspi1, cmd, 4U,
                                                 FRAM_SPI_TIMEOUT_MS);
    if (status == HAL_OK) {
        status = HAL_SPI_Receive(&hspi1, buf, (uint16_t)len,
                                 FRAM_SPI_TIMEOUT_MS);
    }
    fram_cs_deassert();
    return status;
}

/**
 * @brief  Write len bytes from buf to FRAM address addr.
 *
 *         Issues a WREN cycle then the WRITE command.
 *
 * @param addr   24-bit FRAM byte address.
 * @param buf    Source buffer.
 * @param len    Number of bytes to write.
 * @return HAL_OK on success.
 */
static HAL_StatusTypeDef fram_write(uint32_t addr, const uint8_t *buf,
                                     size_t len)
{
    uint8_t cmd[4];
    cmd[0] = FRAM_OP_WRITE;
    cmd[1] = (uint8_t)((addr >> 16) & 0xFFU);
    cmd[2] = (uint8_t)((addr >>  8) & 0xFFU);
    cmd[3] = (uint8_t)((addr      ) & 0xFFU);

    /* WREN must immediately precede the WRITE CS assertion */
    fram_send_wren();

    fram_cs_assert();
    HAL_StatusTypeDef status = HAL_SPI_Transmit(&hspi1, cmd, 4U,
                                                 FRAM_SPI_TIMEOUT_MS);
    if (status == HAL_OK) {
        status = HAL_SPI_Transmit(&hspi1, (uint8_t *)buf, (uint16_t)len,
                                  FRAM_SPI_TIMEOUT_MS);
    }
    fram_cs_deassert();
    return status;
}

/**
 * @brief  Write the in-RAM header shadow back to FRAM address FRAM_BASE.
 *         Recomputes header CRC before writing.
 *
 * @return true on success.
 */
static bool write_header(void)
{
    /* Recompute header CRC over all fields except crc32 itself */
    size_t crc_len = offsetof(fault_log_header_t, crc32);
    s_header.crc32 = crc32_compute((const uint8_t *)&s_header, crc_len);

    HAL_StatusTypeDef status = fram_write(FRAM_BASE,
                                          (const uint8_t *)&s_header,
                                          sizeof(s_header));
    return (status == HAL_OK);
}

/**
 * @brief  Format the FRAM fault log: write a clean header and zero all entries.
 *
 *         Called when an invalid or corrupt header is detected.
 *
 * @return true on success.
 */
static bool format_fram(void)
{
    memset(&s_header, 0, sizeof(s_header));
    s_header.magic         = FAULT_LOG_MAGIC;
    s_header.version       = FAULT_LOG_VERSION;
    s_header.write_index   = 0U;
    s_header.total_entries = 0U;

    if (!write_header()) {
        return false;
    }

    /* Zero the entry area in 64-byte chunks to avoid a large stack buffer */
    uint8_t zero_chunk[64];
    memset(zero_chunk, 0, sizeof(zero_chunk));

    uint32_t addr     = FAULT_ENTRY_BASE;
    uint32_t end_addr = FRAM_BASE + FRAM_SIZE;

    while (addr < end_addr) {
        size_t chunk = sizeof(zero_chunk);
        if ((addr + chunk) > end_addr) {
            chunk = end_addr - addr;
        }
        HAL_StatusTypeDef s = fram_write(addr, zero_chunk, chunk);
        if (s != HAL_OK) {
            return false;
        }
        addr += (uint32_t)chunk;
    }

    return true;
}

/* ============================================================================
 * fault_log_init
 * ============================================================================ */
uint32_t fault_log_init(void)
{
    /* De-assert CS to ensure FRAM is not selected at power-on */
    fram_cs_deassert();

    /* Read header */
    HAL_StatusTypeDef status = fram_read(FRAM_BASE,
                                          (uint8_t *)&s_header,
                                          sizeof(s_header));
    if (status != HAL_OK) {
        /* SPI read failed — format and return 0 entries */
        format_fram();
        s_initialised = true;
        return 0U;
    }

    /* Validate magic */
    if (s_header.magic != FAULT_LOG_MAGIC) {
        format_fram();
        s_initialised = true;
        return 0U;
    }

    /* Validate header CRC-32 */
    size_t crc_len   = offsetof(fault_log_header_t, crc32);
    uint32_t computed = crc32_compute((const uint8_t *)&s_header, crc_len);
    if (computed != s_header.crc32) {
        /* Header corrupted — format FRAM and start fresh */
        format_fram();
        s_initialised = true;
        return 0U;
    }

    /* Sanity-check write_index range */
    if (s_header.write_index >= MAX_LOG_ENTRIES) {
        s_header.write_index = 0U;
    }

    s_initialised = true;
    return s_header.total_entries;
}

/* ============================================================================
 * fault_log_write_entry
 * ============================================================================ */
bool fault_log_write_entry(const fault_event_t *evt)
{
    if (!s_initialised || evt == NULL) {
        return false;
    }

    /* Take SPI mutex — 100 ms timeout to avoid stalling safety comms */
    if (xSPIMutex == NULL ||
        xSemaphoreTake(xSPIMutex, pdMS_TO_TICKS(100U)) != pdTRUE) {
        return false;
    }

    bool success = false;

    /* Compute write address in the ring buffer */
    uint32_t slot    = s_header.write_index % MAX_LOG_ENTRIES;
    uint32_t addr    = FAULT_ENTRY_BASE +
                       (slot * (uint32_t)sizeof(fault_log_entry_t));

    /* Build the entry */
    fault_log_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    entry.sequence     = s_header.total_entries + 1U;
    entry.timestamp_ms = evt->timestamp_ms;
    entry.fault_code   = (uint16_t)evt->code;
    entry.severity     = (uint8_t)evt->severity;
    entry.source       = (uint8_t)evt->source;
    entry.context[0]   = evt->context[0];
    entry.context[1]   = evt->context[1];

    /* Entry CRC over all fields except entry_crc32 */
    size_t entry_crc_len = offsetof(fault_log_entry_t, entry_crc32);
    entry.entry_crc32 = crc32_compute((const uint8_t *)&entry, entry_crc_len);

    /* Write entry to FRAM */
    HAL_StatusTypeDef s = fram_write(addr, (const uint8_t *)&entry,
                                     sizeof(entry));
    if (s == HAL_OK) {
        /* Advance ring buffer head */
        s_header.write_index   = (s_header.write_index + 1U) % MAX_LOG_ENTRIES;
        s_header.total_entries = s_header.total_entries + 1U;

        /* Persist updated header */
        success = write_header();
    }

    xSemaphoreGive(xSPIMutex);
    return success;
}

/* ============================================================================
 * fault_log_read_entry
 * ============================================================================ */
bool fault_log_read_entry(uint32_t index, fault_event_t *evt_out)
{
    if (!s_initialised || evt_out == NULL) {
        return false;
    }

    /* index is a ring-relative slot number (0 = oldest entry still present) */
    uint32_t total  = s_header.total_entries;
    uint32_t stored = (total < MAX_LOG_ENTRIES) ? total : MAX_LOG_ENTRIES;

    if (index >= stored) {
        return false;
    }

    /* Compute absolute slot: oldest entry sits at (write_index - stored) mod MAX */
    uint32_t oldest_slot = (s_header.write_index + MAX_LOG_ENTRIES - stored)
                           % MAX_LOG_ENTRIES;
    uint32_t target_slot = (oldest_slot + index) % MAX_LOG_ENTRIES;
    uint32_t addr        = FAULT_ENTRY_BASE +
                           (target_slot * (uint32_t)sizeof(fault_log_entry_t));

    fault_log_entry_t entry;

    if (xSPIMutex == NULL ||
        xSemaphoreTake(xSPIMutex, pdMS_TO_TICKS(100U)) != pdTRUE) {
        return false;
    }

    HAL_StatusTypeDef s = fram_read(addr, (uint8_t *)&entry, sizeof(entry));
    xSemaphoreGive(xSPIMutex);

    if (s != HAL_OK) {
        return false;
    }

    /* Validate entry CRC */
    size_t entry_crc_len = offsetof(fault_log_entry_t, entry_crc32);
    uint32_t computed    = crc32_compute((const uint8_t *)&entry, entry_crc_len);
    if (computed != entry.entry_crc32) {
        return false;
    }

    /* Populate caller's fault_event_t */
    evt_out->code         = (fault_code_t)entry.fault_code;
    evt_out->severity     = (fault_severity_t)entry.severity;
    evt_out->source       = (fault_source_t)entry.source;
    evt_out->timestamp_ms = entry.timestamp_ms;
    evt_out->context[0]   = entry.context[0];
    evt_out->context[1]   = entry.context[1];

    return true;
}

/* ============================================================================
 * fault_log_dump_uart
 *
 * Iterates all stored fault log entries and prints each one as human-readable
 * text to USART3 (the service port / modem UART, shared via xSPIMutex).
 *
 * Format per entry:
 *   [%08X] %s Fault 0x%04X: %s (Board: %d, Phase: %d)\r\n
 * ============================================================================ */

/** Human-readable names for fault severity */
static const char *const k_sev_names[] = {
    "INFO", "WARN", "CRIT", "FATAL"
};

/** Human-readable names for fault source subsystems */
static const char *const k_src_names[] = {
    "SYSTEM", "TRAFFIC", "SAFETY", "CAN", "POWER",
    "GNSS", "TELEMETRY", "FRAM", "LSO"
};

void fault_log_dump_uart(void)
{
    if (!s_initialised) {
        return;
    }

    uint32_t total  = s_header.total_entries;
    uint32_t stored = (total < MAX_LOG_ENTRIES) ? total : MAX_LOG_ENTRIES;

    char line_buf[128];
    int  len;

    len = snprintf(line_buf, sizeof(line_buf),
                   "\r\n--- LUMINA LCU-100 FAULT LOG ---\r\n"
                   "Total logged: %lu  Stored: %lu  Log wrap: %s\r\n\r\n",
                   (unsigned long)total,
                   (unsigned long)stored,
                   (total > MAX_LOG_ENTRIES) ? "YES" : "no");
    if (len > 0) {
        HAL_UART_Transmit(&huart3, (uint8_t *)line_buf, (uint16_t)len, 1000U);
    }

    for (uint32_t i = 0U; i < stored; i++) {
        fault_event_t evt;
        if (!fault_log_read_entry(i, &evt)) {
            continue;
        }

        const char *sev_str = (evt.severity <= FAULT_SEV_FATAL)
                              ? k_sev_names[evt.severity] : "???";
        const char *src_str = (evt.source <= FAULT_SRC_LSO)
                              ? k_src_names[evt.source] : "???";

        len = snprintf(line_buf, sizeof(line_buf),
                       "[%08lX] %s Fault 0x%04X: %s (Board: %ld, Phase: %ld)\r\n",
                       (unsigned long)evt.timestamp_ms,
                       sev_str,
                       (unsigned)evt.code,
                       src_str,
                       (long)evt.context[0],
                       (long)evt.context[1]);

        if (len > 0) {
            HAL_UART_Transmit(&huart3, (uint8_t *)line_buf,
                              (uint16_t)len, 1000U);
        }
    }

    len = snprintf(line_buf, sizeof(line_buf),
                   "\r\n--- END OF LOG ---\r\n");
    if (len > 0) {
        HAL_UART_Transmit(&huart3, (uint8_t *)line_buf, (uint16_t)len, 1000U);
    }
}

/* ============================================================================
 * fault_log_task — FreeRTOS task
 *
 * Waits on xFaultQueue, writes arriving fault_event_t entries to FRAM.
 * This task never blocks the traffic engine; it simply drains the queue
 * whenever events are posted.
 *
 * Stack size: 512 words    Priority: osPriorityBelowNormal
 * ============================================================================ */
void fault_log_task(void *pvParameters)
{
    (void)pvParameters;

    /* Initialise the log — this may take a few hundred milliseconds if
     * FRAM formatting is required on first boot. */
    fault_log_init();

    /* Assert fault LED off at startup */
    HAL_GPIO_WritePin(LED_FAULT_GPIO_Port, LED_FAULT_Pin, GPIO_PIN_RESET);

    for (;;) {
        fault_event_t evt;

        /* Block indefinitely on the fault queue */
        if (xQueueReceive(xFaultQueue, &evt, portMAX_DELAY) == pdTRUE) {
            /* Write to FRAM */
            fault_log_write_entry(&evt);

            /* Drive fault LED based on whether an active fault remains */
            EventBits_t bits = xEventGroupGetBits(xSystemEventGroup);
            if (bits & SYSEVT_FAULT_ACTIVE) {
                HAL_GPIO_WritePin(LED_FAULT_GPIO_Port, LED_FAULT_Pin,
                                  GPIO_PIN_SET);
            } else {
                HAL_GPIO_WritePin(LED_FAULT_GPIO_Port, LED_FAULT_Pin,
                                  GPIO_PIN_RESET);
            }
        }
    }
}
