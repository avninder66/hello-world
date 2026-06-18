/**
 * @file    phase_config.c
 * @brief   LCU-100 Phase Plan Configuration Loader — implementation
 *
 * Reads and writes phase_plan_t instances to/from the MB85RS4MT FRAM at
 * PHASE_CONFIG_FRAM_ADDRESS (0x010000), wrapped in a validation envelope
 * containing a magic word, format version, and CRC-32.
 *
 * Block layout in FRAM at 0x010000
 * ---------------------------------
 *   Offset  Size  Field
 *   0       4     magic          (PHASE_CONFIG_MAGIC = 0x504C414E)
 *   4       1     version_major  (PHASE_CONFIG_VERSION_MAJOR)
 *   5       1     version_minor  (PHASE_CONFIG_VERSION_MINOR)
 *   6       2     reserved       (0x0000)
 *   8       N     phase_plan_t   (sizeof(phase_plan_t) bytes)
 *   8+N     4     crc32          (CRC-32/IEEE of bytes [0 .. 8+N-1])
 *
 * Total block size: sizeof(phase_config_block_t)
 *
 * FRAM SPI protocol (MB85RS4MT)
 * -----------------------------
 *   READ  (0x03): CS-low, 0x03, addr[23:16], addr[15:8], addr[7:0], data..., CS-high
 *   WREN  (0x06): CS-low, 0x06, CS-high  (must precede every WRITE CS sequence)
 *   WRITE (0x02): CS-low, 0x02, addr[23:16], addr[15:8], addr[7:0], data..., CS-high
 *
 * The MB85RS4MT has no internal write cycle; data is available immediately
 * after CS de-assertion with no poll required.
 */

#include "phase_config.h"
#include "fault_codes.h"
#include "traffic_engine.h"
#include "safety_comm.h"      /* for xSPIMutex */
#include "main.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * FRAM SPI opcodes (duplicated locally to avoid coupling to fault_log.c)
 * ============================================================================ */
#define CFG_FRAM_OP_WREN    0x06U
#define CFG_FRAM_OP_READ    0x03U
#define CFG_FRAM_OP_WRITE   0x02U
#define CFG_FRAM_SPI_TIMEOUT_MS  50U

/* ============================================================================
 * Configuration block envelope
 * ============================================================================ */

/**
 * @brief  The full FRAM block that wraps a phase_plan_t.
 *
 * The crc32 field covers every byte in the struct from magic through
 * (and including) plan[], i.e. offsetof(phase_config_block_t, crc32) bytes.
 */
typedef struct __attribute__((packed)) {
    uint32_t     magic;             /**< PHASE_CONFIG_MAGIC                   */
    uint8_t      version_major;     /**< PHASE_CONFIG_VERSION_MAJOR           */
    uint8_t      version_minor;     /**< PHASE_CONFIG_VERSION_MINOR           */
    uint8_t      reserved[2];       /**< Must be 0x00                         */
    phase_plan_t plan;              /**< The embedded phase plan              */
    uint32_t     crc32;             /**< CRC-32 of all preceding bytes        */
} phase_config_block_t;

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
 * Static plan buffer — loaded at init, pointed to by traffic engine
 * ============================================================================ */

/**
 * RAM buffer for the active phase plan.
 * Populated by phase_config_init() and kept for the engine's lifetime.
 * Declared static so it outlives the call stack.
 */
static phase_plan_t s_active_plan;

/* ============================================================================
 * Private: FRAM low-level helpers
 * (Local copies so phase_config.c has no link dependency on fault_log.c)
 * ============================================================================ */

static inline void cfg_fram_cs_assert(void)
{
    HAL_GPIO_WritePin(FRAM_CS_GPIO_Port, FRAM_CS_Pin, GPIO_PIN_RESET);
}

static inline void cfg_fram_cs_deassert(void)
{
    HAL_GPIO_WritePin(FRAM_CS_GPIO_Port, FRAM_CS_Pin, GPIO_PIN_SET);
}

static void cfg_fram_send_wren(void)
{
    uint8_t opcode = CFG_FRAM_OP_WREN;
    cfg_fram_cs_assert();
    HAL_SPI_Transmit(&hspi1, &opcode, 1U, CFG_FRAM_SPI_TIMEOUT_MS);
    cfg_fram_cs_deassert();
}

static HAL_StatusTypeDef cfg_fram_read(uint32_t addr,
                                        uint8_t *buf, size_t len)
{
    uint8_t cmd[4];
    cmd[0] = CFG_FRAM_OP_READ;
    cmd[1] = (uint8_t)((addr >> 16) & 0xFFU);
    cmd[2] = (uint8_t)((addr >>  8) & 0xFFU);
    cmd[3] = (uint8_t)((addr      ) & 0xFFU);

    cfg_fram_cs_assert();
    HAL_StatusTypeDef s = HAL_SPI_Transmit(&hspi1, cmd, 4U,
                                            CFG_FRAM_SPI_TIMEOUT_MS);
    if (s == HAL_OK) {
        s = HAL_SPI_Receive(&hspi1, buf, (uint16_t)len,
                            CFG_FRAM_SPI_TIMEOUT_MS);
    }
    cfg_fram_cs_deassert();
    return s;
}

static HAL_StatusTypeDef cfg_fram_write(uint32_t addr,
                                         const uint8_t *buf, size_t len)
{
    uint8_t cmd[4];
    cmd[0] = CFG_FRAM_OP_WRITE;
    cmd[1] = (uint8_t)((addr >> 16) & 0xFFU);
    cmd[2] = (uint8_t)((addr >>  8) & 0xFFU);
    cmd[3] = (uint8_t)((addr      ) & 0xFFU);

    cfg_fram_send_wren();   /* WREN must immediately precede WRITE CS cycle */

    cfg_fram_cs_assert();
    HAL_StatusTypeDef s = HAL_SPI_Transmit(&hspi1, cmd, 4U,
                                            CFG_FRAM_SPI_TIMEOUT_MS);
    if (s == HAL_OK) {
        s = HAL_SPI_Transmit(&hspi1, (uint8_t *)buf, (uint16_t)len,
                             CFG_FRAM_SPI_TIMEOUT_MS);
    }
    cfg_fram_cs_deassert();
    return s;
}

/* ============================================================================
 * Private: post_config_fault
 * ============================================================================ */
static void post_config_fault(fault_code_t code, fault_severity_t sev,
                               uint32_t ctx0, uint32_t ctx1)
{
    fault_event_t evt = {
        .code         = code,
        .severity     = sev,
        .source       = FAULT_SRC_FRAM,
        .timestamp_ms = HAL_GetTick(),
        .context      = { ctx0, ctx1 }
    };
    if (xFaultQueue != NULL) {
        xQueueSend(xFaultQueue, &evt, 0);
    }
}

/* ============================================================================
 * phase_config_load_default
 * ============================================================================ */
void phase_config_load_default(phase_plan_t *plan)
{
    if (plan == NULL) {
        return;
    }
    /* Copy the compile-time constant into the caller's buffer */
    memcpy(plan, &phase_plan_shuttle_2way, sizeof(phase_plan_t));
}

/* ============================================================================
 * phase_config_load
 * ============================================================================ */
config_load_result_t phase_config_load(phase_plan_t *plan)
{
    if (plan == NULL) {
        return CONFIG_SPI_ERROR;
    }

    /* Take SPI mutex for the duration of the FRAM transaction */
    if (xSPIMutex == NULL ||
        xSemaphoreTake(xSPIMutex, pdMS_TO_TICKS(100U)) != pdTRUE) {
        return CONFIG_SPI_ERROR;
    }

    phase_config_block_t block;
    HAL_StatusTypeDef status = cfg_fram_read(PHASE_CONFIG_FRAM_ADDRESS,
                                              (uint8_t *)&block,
                                              sizeof(block));
    xSemaphoreGive(xSPIMutex);

    if (status != HAL_OK) {
        return CONFIG_SPI_ERROR;
    }

    /* Check for uninitialised FRAM (erased = 0xFF pattern) */
    {
        const uint8_t *p   = (const uint8_t *)&block;
        bool all_ff        = true;
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            if (p[i] != 0xFFU) {
                all_ff = false;
                break;
            }
        }
        if (all_ff) {
            return CONFIG_EMPTY;
        }
    }

    /* Validate magic */
    if (block.magic != PHASE_CONFIG_MAGIC) {
        return CONFIG_MAGIC_FAIL;
    }

    /* Validate version — major version must match exactly */
    if (block.version_major != PHASE_CONFIG_VERSION_MAJOR) {
        return CONFIG_VERSION_MISMATCH;
    }

    /* Validate CRC-32 — covers all bytes before the crc32 field */
    size_t   crc_len  = offsetof(phase_config_block_t, crc32);
    uint32_t computed = crc32_compute((const uint8_t *)&block, crc_len);
    if (computed != block.crc32) {
        return CONFIG_CRC_FAIL;
    }

    /* All checks passed — copy plan out */
    memcpy(plan, &block.plan, sizeof(phase_plan_t));

    return CONFIG_OK;
}

/* ============================================================================
 * phase_config_save
 * ============================================================================ */
config_load_result_t phase_config_save(const phase_plan_t *plan)
{
    if (plan == NULL) {
        return CONFIG_SPI_ERROR;
    }

    phase_config_block_t block;
    memset(&block, 0, sizeof(block));

    block.magic         = PHASE_CONFIG_MAGIC;
    block.version_major = PHASE_CONFIG_VERSION_MAJOR;
    block.version_minor = PHASE_CONFIG_VERSION_MINOR;
    block.reserved[0]   = 0U;
    block.reserved[1]   = 0U;

    memcpy(&block.plan, plan, sizeof(phase_plan_t));

    /* Compute CRC over magic + version + reserved + plan */
    size_t crc_len = offsetof(phase_config_block_t, crc32);
    block.crc32    = crc32_compute((const uint8_t *)&block, crc_len);

    /* Write to FRAM under SPI mutex */
    if (xSPIMutex == NULL ||
        xSemaphoreTake(xSPIMutex, pdMS_TO_TICKS(200U)) != pdTRUE) {
        return CONFIG_SPI_ERROR;
    }

    HAL_StatusTypeDef status = cfg_fram_write(PHASE_CONFIG_FRAM_ADDRESS,
                                               (const uint8_t *)&block,
                                               sizeof(block));
    xSemaphoreGive(xSPIMutex);

    return (status == HAL_OK) ? CONFIG_OK : CONFIG_SPI_ERROR;
}

/* ============================================================================
 * phase_config_init
 * ============================================================================ */
config_load_result_t phase_config_init(void)
{
    config_load_result_t result = phase_config_load(&s_active_plan);

    if (result != CONFIG_OK) {
        /* Log fault describing what went wrong */
        switch (result) {
        case CONFIG_CRC_FAIL:
            post_config_fault(FAULT_FRAM_CRC_MISMATCH, FAULT_SEV_WARNING,
                              (uint32_t)result, PHASE_CONFIG_FRAM_ADDRESS);
            break;

        case CONFIG_MAGIC_FAIL:
        case CONFIG_VERSION_MISMATCH:
            post_config_fault(FAULT_PHASE_PLAN_INVALID, FAULT_SEV_WARNING,
                              (uint32_t)result, PHASE_CONFIG_FRAM_ADDRESS);
            break;

        case CONFIG_EMPTY:
            /* First boot — not a fault, just load default and save */
            post_config_fault(FAULT_NO_PHASE_PLAN, FAULT_SEV_INFO,
                              0U, 0U);
            break;

        case CONFIG_SPI_ERROR:
            post_config_fault(FAULT_FRAM_READ_FAIL, FAULT_SEV_CRITICAL,
                              (uint32_t)result, PHASE_CONFIG_FRAM_ADDRESS);
            break;

        default:
            break;
        }

        /* Fall back to the compile-time default shuttle plan */
        phase_config_load_default(&s_active_plan);

        /* Attempt to persist the default so future boots load it directly.
         * Failure here is non-fatal — we already have the plan in RAM. */
        (void)phase_config_save(&s_active_plan);
    }

    /* Hand the plan pointer to the traffic engine.
     * s_active_plan is static so the pointer remains valid indefinitely. */
    if (!traffic_engine_load_plan(&s_active_plan)) {
        /* plan CRC check failed — this should not happen for the default plan,
         * but handle defensively. */
        post_config_fault(FAULT_PHASE_PLAN_INVALID, FAULT_SEV_CRITICAL,
                          0U, 0U);
    }

    return result;
}
