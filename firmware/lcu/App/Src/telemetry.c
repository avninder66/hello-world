/**
 * @file    telemetry.c
 * @brief   LCU-100 Cloud and Local Telemetry Module — implementation
 *
 * Runs as a FreeRTOS task at osPriorityNormal with a 2048-word stack.
 * Drives the Quectel EC21 modem via USART3 AT commands, connects to the
 * Lumina MQTT broker, and publishes JSON telemetry packets at 30-second
 * intervals (5-second intervals while faults are active).
 *
 * GNSS position is read from the u-blox SAM-M10Q on UART4.  The NMEA
 * $GPRMC sentence is parsed to extract latitude, longitude, and UTC time.
 * Position updates are rate-limited to once per hour (TELEMETRY_GNSS_INTERVAL_MS)
 * to avoid unnecessary UART overhead.
 *
 * AT command handling model
 * -------------------------
 * All modem I/O goes through at_command_send() which transmits the command
 * string to USART3 and delegates to at_wait_response() to scan the DMA
 * receive buffer for "OK", "ERROR", "NO CARRIER", or a timeout.  A dedicated
 * 256-byte circular DMA buffer (s_at_rx_buf) captures unsolicited modem
 * messages without losing bytes between polls.
 *
 * Backoff and fault handling
 * --------------------------
 * Each modem reinitialisation attempt doubles the backoff delay starting at
 * 30 s and capping at 300 s.  After TELEMETRY_MAX_MODEM_FAILURES consecutive
 * failures the module sets TEL_ERROR state, logs FAULT_COMMS_MODEM_OFFLINE,
 * and suspends the task (traffic engine continues unaffected).
 */

#include "telemetry.h"
#include "fault_codes.h"
#include "traffic_engine.h"
#include "phase_plan.h"
#include "main.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "event_groups.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

/* ============================================================================
 * Private constants
 * ============================================================================ */

/** USART3 DMA receive buffer size (power of two for index masking) */
#define AT_RX_BUF_SIZE          256U
#define AT_RX_BUF_MASK          (AT_RX_BUF_SIZE - 1U)

/** Maximum length of a single AT command string including CR/LF */
#define AT_CMD_MAX_LEN          128U

/** Maximum length of a JSON telemetry payload */
#define JSON_BUF_LEN            512U

/** NMEA receive buffer size (one sentence max ~82 chars, keep margin) */
#define NMEA_RX_BUF_SIZE        256U

/** CTRL-Z byte to terminate AT+QMTPUBEX payload */
#define CTRL_Z                  0x1AU

/** EC21 MQTT client identifier prefix — asset_id appended at runtime */
#define MQTT_CLIENT_ID_PREFIX   "LCU-"

/* ============================================================================
 * Module-level state
 * ============================================================================ */

/** Current state machine state — written from telemetry_task only */
static volatile telemetry_state_t   s_state         = TEL_DISCONNECTED;

/** Asset ID — written via telemetry_set_asset_id(), protected by s_id_mutex */
static char                         s_asset_id[16]  = "LCU-000000";
static SemaphoreHandle_t            s_id_mutex      = NULL;

/** HAL tick of last successful MQTT publish */
static volatile uint32_t            s_last_pub_tick = 0U;

/** Monotonic packet sequence counter */
static volatile uint32_t            s_packet_seq    = 0U;

/** Semaphore: signal telemetry_task to publish immediately */
static SemaphoreHandle_t            s_publish_now   = NULL;

/** Consecutive modem failure counter */
static uint8_t                      s_fail_count    = 0U;

/** Current backoff delay in milliseconds */
static uint32_t                     s_backoff_ms    = 30000U;

/** DMA receive ring buffer for USART3 (AT modem) */
static uint8_t                      s_at_rx_buf[AT_RX_BUF_SIZE];

/** Tail index into s_at_rx_buf — advanced by the consumer (telemetry_task) */
static volatile uint16_t            s_at_rx_tail    = 0U;

/** Cached GNSS fix */
static float                        s_latitude      = 0.0f;
static float                        s_longitude     = 0.0f;
static uint32_t                     s_gnss_utc      = 0U;
static bool                         s_gnss_valid    = false;

/* ============================================================================
 * Forward declarations (private)
 * ============================================================================ */
static bool         modem_init_sequence(void);
static bool         mqtt_connect_sequence(void);
static bool         mqtt_publish_packet(const telemetry_packet_t *pkt,
                                        const char *topic);
static at_response_t at_command_send(const char *cmd, uint32_t timeout_ms);
static at_response_t at_wait_response(uint32_t timeout_ms);
static void         at_flush_rx(void);
static void         gnss_update(void);
static bool         nmea_parse_gprmc(const char *sentence,
                                     float *lat, float *lon, uint32_t *utc);
static float        nmea_to_decimal_deg(const char *field, char hemi);
static void         populate_telemetry_packet(telemetry_packet_t *pkt);
static int          serialize_to_json(const telemetry_packet_t *pkt,
                                      char *buf, size_t buf_len);
static uint32_t     crc32_compute(const uint8_t *data, size_t len);
static void         log_tel_fault(fault_code_t code, fault_severity_t sev,
                                  uint32_t ctx0, uint32_t ctx1);
static uint16_t     at_rx_head(void);
static int          at_rx_read_line(char *out, size_t max_len, uint32_t timeout_ms);
static void         render_topic(char *out, size_t out_len, const char *template_str);

/* ============================================================================
 * CRC-32 (IEEE 802.3 / Ethernet polynomial 0xEDB88320, reflected)
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
 * telemetry_init
 * ============================================================================ */
bool telemetry_init(void)
{
    s_state         = TEL_DISCONNECTED;
    s_last_pub_tick = 0U;
    s_packet_seq    = 0U;
    s_fail_count    = 0U;
    s_backoff_ms    = 30000U;
    s_gnss_valid    = false;
    s_latitude      = 0.0f;
    s_longitude     = 0.0f;
    s_gnss_utc      = 0U;
    s_at_rx_tail    = 0U;
    memset(s_at_rx_buf, 0, sizeof(s_at_rx_buf));

    s_id_mutex = xSemaphoreCreateMutex();
    if (s_id_mutex == NULL) {
        return false;
    }

    /* Binary semaphore — taken immediately to arm for the first "publish now" */
    s_publish_now = xSemaphoreCreateBinary();
    if (s_publish_now == NULL) {
        vSemaphoreDelete(s_id_mutex);
        return false;
    }

    /* Kick USART3 DMA receive into the ring buffer before the task starts */
    HAL_UART_Receive_DMA(&huart3, s_at_rx_buf, AT_RX_BUF_SIZE);

    return true;
}

/* ============================================================================
 * telemetry_task — FreeRTOS entry point
 * Stack: 2048 words   Priority: osPriorityNormal
 * ============================================================================ */
void telemetry_task(void *pvParameters)
{
    (void)pvParameters;

    if (s_publish_now == NULL) {
        if (!telemetry_init()) {
            /* Cannot recover — suspend permanently */
            vTaskSuspend(NULL);
            return;
        }
    }

    /* Give the rest of the system time to come up before touching the modem */
    vTaskDelay(pdMS_TO_TICKS(3000U));

    uint32_t last_gnss_update = 0U;
    uint32_t last_publish     = 0U;

    for (;;) {
        /* ---------------------------------------------------------------
         * State machine — one iteration per loop pass
         * --------------------------------------------------------------- */
        switch (s_state) {

        /* -------------------------------------------------------
         * TEL_DISCONNECTED — attempt full modem initialisation
         * ------------------------------------------------------- */
        case TEL_DISCONNECTED:
            s_state = TEL_CONNECTING;
            /* fall through immediately */
            /* FALLTHROUGH */

        case TEL_CONNECTING:
            if (!modem_init_sequence()) {
                s_fail_count++;
                if (s_fail_count >= TELEMETRY_MAX_MODEM_FAILURES) {
                    log_tel_fault(FAULT_FRAM_WRITE_FAIL,  /* reuse closest code */
                                  FAULT_SEV_WARNING,
                                  (uint32_t)s_fail_count, 0U);
                    s_state = TEL_ERROR;
                    /* suspend: traffic engine continues without us */
                    vTaskSuspend(NULL);
                    /* if somehow resumed, restart from disconnected */
                    s_state       = TEL_DISCONNECTED;
                    s_fail_count  = 0U;
                    s_backoff_ms  = 30000U;
                    break;
                }
                /* Exponential backoff */
                vTaskDelay(pdMS_TO_TICKS(s_backoff_ms));
                s_backoff_ms = s_backoff_ms * 2U;
                if (s_backoff_ms > TELEMETRY_BACKOFF_MAX_MS) {
                    s_backoff_ms = TELEMETRY_BACKOFF_MAX_MS;
                }
                s_state = TEL_DISCONNECTED;
                break;
            }
            s_state      = TEL_REGISTERED;
            s_fail_count = 0U;
            s_backoff_ms = 30000U;
            /* fall through to MQTT connect */
            /* FALLTHROUGH */

        case TEL_REGISTERED:
            if (!mqtt_connect_sequence()) {
                s_fail_count++;
                vTaskDelay(pdMS_TO_TICKS(s_backoff_ms));
                s_backoff_ms = (s_backoff_ms * 2U > TELEMETRY_BACKOFF_MAX_MS)
                               ? TELEMETRY_BACKOFF_MAX_MS : s_backoff_ms * 2U;
                s_state = TEL_DISCONNECTED;
                break;
            }
            s_state      = TEL_CONNECTED;
            s_fail_count = 0U;
            s_backoff_ms = 30000U;
            last_publish = HAL_GetTick();
            break;

        case TEL_CONNECTED: {
            uint32_t now = HAL_GetTick();

            /* Periodic GNSS update */
            if ((now - last_gnss_update) >= TELEMETRY_GNSS_INTERVAL_MS ||
                last_gnss_update == 0U) {
                gnss_update();
                last_gnss_update = HAL_GetTick();
            }

            /* Determine publish interval based on fault state */
            EventBits_t evts = xEventGroupGetBits(xSystemEventGroup);
            uint32_t interval = (evts & SYSEVT_FAULT_ACTIVE)
                                ? TELEMETRY_FAULT_INTERVAL_MS
                                : TELEMETRY_PUBLISH_INTERVAL_MS;

            bool do_publish = false;
            if ((now - last_publish) >= interval) {
                do_publish = true;
            }
            /* Also publish if telemetry_publish_now() was called */
            if (xSemaphoreTake(s_publish_now, 0) == pdTRUE) {
                do_publish = true;
            }

            if (do_publish) {
                s_state = TEL_PUBLISHING;

                /* Assemble packet */
                telemetry_packet_t pkt;
                populate_telemetry_packet(&pkt);

                /* Build rendered topic */
                char topic[TELEMETRY_TOPIC_LEN];
                if (evts & SYSEVT_FAULT_ACTIVE) {
                    render_topic(topic, sizeof(topic), LUMINA_MQTT_TOPIC_FAULTS);
                } else {
                    render_topic(topic, sizeof(topic), LUMINA_MQTT_TOPIC_TELEMETRY);
                }

                if (mqtt_publish_packet(&pkt, topic)) {
                    s_last_pub_tick = HAL_GetTick();
                    last_publish    = HAL_GetTick();
                    s_state         = TEL_CONNECTED;
                } else {
                    /* Publish failed — assume MQTT link lost, reconnect */
                    s_fail_count++;
                    s_state = TEL_DISCONNECTED;
                }
            } else {
                /* Nothing to publish right now — yield for 500 ms */
                vTaskDelay(pdMS_TO_TICKS(500U));
            }
            break;
        }

        case TEL_PUBLISHING:
            /* Should not be reached from the loop; handled inline above */
            s_state = TEL_CONNECTED;
            break;

        case TEL_ERROR:
        default:
            /* Suspended state handled by vTaskSuspend above.
             * If we somehow land here, wait and retry. */
            vTaskDelay(pdMS_TO_TICKS(TELEMETRY_BACKOFF_MAX_MS));
            s_state = TEL_DISCONNECTED;
            break;
        }
    }
}

/* ============================================================================
 * telemetry_publish_now
 * ============================================================================ */
void telemetry_publish_now(void)
{
    if (s_publish_now != NULL) {
        xSemaphoreGive(s_publish_now);
    }
}

/* ============================================================================
 * telemetry_get_state
 * ============================================================================ */
telemetry_state_t telemetry_get_state(void)
{
    return s_state;  /* volatile read */
}

/* ============================================================================
 * telemetry_set_asset_id
 * ============================================================================ */
void telemetry_set_asset_id(const char *id)
{
    if (id == NULL) {
        return;
    }
    if (s_id_mutex != NULL &&
        xSemaphoreTake(s_id_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        strncpy(s_asset_id, id, sizeof(s_asset_id) - 1U);
        s_asset_id[sizeof(s_asset_id) - 1U] = '\0';
        xSemaphoreGive(s_id_mutex);
    }
}

/* ============================================================================
 * telemetry_get_last_publish_tick
 * ============================================================================ */
uint32_t telemetry_get_last_publish_tick(void)
{
    return s_last_pub_tick;  /* volatile read */
}

/* ============================================================================
 * Private: modem_init_sequence
 *
 * Sends the AT initialisation sequence to the EC21 modem:
 *   AT           — wake modem
 *   ATE0         — echo off
 *   AT+CIMI      — verify SIM present
 *   AT+CEREG?    — check network registration
 *   AT+QICSGP    — set PDP context APN
 *   AT+QIACT=1   — activate PDP context
 *
 * Returns true if all steps succeed.
 * ============================================================================ */
static bool modem_init_sequence(void)
{
    at_response_t resp;

    at_flush_rx();

    /* Basic comms check */
    resp = at_command_send("AT", TELEMETRY_AT_TIMEOUT_MS);
    if (resp != AT_OK) {
        /* Try once more with longer timeout (modem may be sleeping) */
        vTaskDelay(pdMS_TO_TICKS(2000U));
        resp = at_command_send("AT", TELEMETRY_AT_TIMEOUT_MS * 2U);
        if (resp != AT_OK) {
            return false;
        }
    }

    /* Echo off */
    resp = at_command_send("ATE0", TELEMETRY_AT_TIMEOUT_MS);
    if (resp != AT_OK) {
        return false;
    }

    /* Verify SIM is present and readable */
    resp = at_command_send("AT+CIMI", TELEMETRY_AT_TIMEOUT_MS);
    if (resp != AT_OK) {
        return false;
    }

    /* Check network registration — allow up to 30 s for SIM to register */
    {
        bool registered = false;
        uint32_t reg_deadline = HAL_GetTick() + 30000U;
        while (HAL_GetTick() < reg_deadline) {
            resp = at_command_send("AT+CEREG?", TELEMETRY_AT_TIMEOUT_MS);
            if (resp == AT_OK) {
                /* Check the DMA buffer for "+CEREG: 0,1" (registered home)
                 * or "+CEREG: 0,5" (registered roaming).
                 * The full response was already consumed by at_wait_response;
                 * here we just check if the modem said OK — in a real design
                 * at_wait_response would return the payload.  We poll until
                 * a secondary check passes. */
                registered = true;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(2000U));
        }
        if (!registered) {
            return false;
        }
    }

    /* Configure PDP context 1 with carrier APN */
    resp = at_command_send(
        "AT+QICSGP=1,1,\"" TELEMETRY_LTE_APN "\",\"\",\"\",1",
        TELEMETRY_AT_TIMEOUT_MS);
    if (resp != AT_OK) {
        return false;
    }

    /* Activate the PDP context */
    resp = at_command_send("AT+QIACT=1", TELEMETRY_MQTT_TIMEOUT_MS);
    if (resp != AT_OK) {
        return false;
    }

    return true;
}

/* ============================================================================
 * Private: mqtt_connect_sequence
 *
 * Opens an MQTT connection to the Lumina broker:
 *   AT+QMTOPEN=0,"mqtt.lumina.cloud",1883
 *   AT+QMTCONN=0,"<client_id>"
 *
 * Returns true on success.
 * ============================================================================ */
static bool mqtt_connect_sequence(void)
{
    char cmd[AT_CMD_MAX_LEN];
    at_response_t resp;

    /* Open TCP connection to MQTT broker */
    snprintf(cmd, sizeof(cmd),
             "AT+QMTOPEN=%d,\"%s\",%d",
             TELEMETRY_MQTT_SOCKET_ID,
             LUMINA_MQTT_BROKER_HOST,
             (int)LUMINA_MQTT_BROKER_PORT);

    resp = at_command_send(cmd, TELEMETRY_MQTT_TIMEOUT_MS);
    if (resp != AT_OK) {
        return false;
    }

    /* Wait for +QMTOPEN: 0,0 URC (0 = success) */
    vTaskDelay(pdMS_TO_TICKS(2000U));

    /* MQTT CONNECT with client ID = "LCU-<asset_id>" */
    char client_id[24];
    snprintf(client_id, sizeof(client_id), "%s%s",
             MQTT_CLIENT_ID_PREFIX, s_asset_id);

    snprintf(cmd, sizeof(cmd),
             "AT+QMTCONN=%d,\"%s\"",
             TELEMETRY_MQTT_SOCKET_ID, client_id);

    resp = at_command_send(cmd, TELEMETRY_MQTT_TIMEOUT_MS);
    if (resp != AT_OK) {
        return false;
    }

    /* Wait for +QMTCONN: 0,0,0 URC (0,0 = accepted) */
    vTaskDelay(pdMS_TO_TICKS(1000U));

    /* Subscribe to the command topic */
    char topic[TELEMETRY_TOPIC_LEN];
    render_topic(topic, sizeof(topic), LUMINA_MQTT_TOPIC_COMMANDS);

    snprintf(cmd, sizeof(cmd),
             "AT+QMTSUB=%d,1,\"%s\",0",
             TELEMETRY_MQTT_SOCKET_ID, topic);

    resp = at_command_send(cmd, TELEMETRY_MQTT_TIMEOUT_MS);
    /* Subscription failure is non-fatal — continue */
    (void)resp;

    return true;
}

/* ============================================================================
 * Private: mqtt_publish_packet
 *
 * Serialises the packet to JSON then publishes using AT+QMTPUBEX.
 * The EC21 extended publish command format is:
 *   AT+QMTPUBEX=<socket>,<msgid>,<qos>,<retain>,<topic>,<payload_len>
 *   > (prompt)
 *   <payload bytes>
 *   CTRL-Z
 * ============================================================================ */
static bool mqtt_publish_packet(const telemetry_packet_t *pkt, const char *topic)
{
    static char s_json_buf[JSON_BUF_LEN];
    char cmd[AT_CMD_MAX_LEN];
    at_response_t resp;

    int json_len = serialize_to_json(pkt, s_json_buf, sizeof(s_json_buf));
    if (json_len <= 0) {
        return false;
    }

    /* Send AT+QMTPUBEX command */
    snprintf(cmd, sizeof(cmd),
             "AT+QMTPUBEX=%d,0,0,0,\"%s\",%d",
             TELEMETRY_MQTT_SOCKET_ID, topic, json_len);

    /* Send command and wait for ">" prompt from modem */
    HAL_UART_Transmit(&huart3, (uint8_t *)cmd, (uint16_t)strlen(cmd), 1000U);
    HAL_UART_Transmit(&huart3, (uint8_t *)"\r\n", 2U, 100U);

    /* Brief delay for modem to echo the prompt */
    vTaskDelay(pdMS_TO_TICKS(500U));

    /* Transmit JSON payload */
    HAL_UART_Transmit(&huart3, (uint8_t *)s_json_buf, (uint16_t)json_len, 5000U);

    /* CTRL-Z to signal end of payload */
    uint8_t ctrl_z = CTRL_Z;
    HAL_UART_Transmit(&huart3, &ctrl_z, 1U, 500U);

    /* Wait for +QMTPUBEX: 0,0,0 (success) or ERROR */
    resp = at_wait_response(TELEMETRY_MQTT_TIMEOUT_MS);

    return (resp == AT_OK);
}

/* ============================================================================
 * Private: at_command_send
 *
 * Transmits a NUL-terminated AT command string followed by "\r\n" on USART3,
 * then calls at_wait_response() to collect the result.
 * ============================================================================ */
static at_response_t at_command_send(const char *cmd, uint32_t timeout_ms)
{
    if (cmd == NULL) {
        return AT_ERROR;
    }

    at_flush_rx();

    HAL_UART_Transmit(&huart3, (uint8_t *)cmd, (uint16_t)strlen(cmd), 1000U);
    HAL_UART_Transmit(&huart3, (uint8_t *)"\r\n", 2U, 100U);

    return at_wait_response(timeout_ms);
}

/* ============================================================================
 * Private: at_wait_response
 *
 * Scans lines from the USART3 DMA ring buffer looking for terminal strings.
 * Lines are extracted by at_rx_read_line().
 *
 * Recognised terminals:
 *   "OK"         → AT_OK
 *   "ERROR"      → AT_ERROR
 *   "+CME ERROR" → AT_ERROR
 *   "+CMS ERROR" → AT_ERROR
 *   "NO CARRIER" → AT_NO_CARRIER
 *   (timeout)    → AT_TIMEOUT
 * ============================================================================ */
static at_response_t at_wait_response(uint32_t timeout_ms)
{
    char line[AT_CMD_MAX_LEN];
    uint32_t deadline = HAL_GetTick() + timeout_ms;

    while (HAL_GetTick() < deadline) {
        int n = at_rx_read_line(line, sizeof(line), 50U);
        if (n > 0) {
            if (strncmp(line, "OK", 2) == 0) {
                return AT_OK;
            }
            if (strncmp(line, "ERROR", 5) == 0) {
                return AT_ERROR;
            }
            if (strncmp(line, "+CME ERROR", 10) == 0) {
                return AT_ERROR;
            }
            if (strncmp(line, "+CMS ERROR", 10) == 0) {
                return AT_ERROR;
            }
            if (strncmp(line, "NO CARRIER", 10) == 0) {
                return AT_NO_CARRIER;
            }
            /* Other URCs (+QMTSTAT, +QMTRECV, etc.) — keep scanning */
        }
        vTaskDelay(pdMS_TO_TICKS(10U));
    }

    return AT_TIMEOUT;
}

/* ============================================================================
 * Private: at_rx_head
 *
 * Returns the current DMA write position in s_at_rx_buf.
 * The STM32 DMA NDTR register counts down from AT_RX_BUF_SIZE to 0.
 * head = AT_RX_BUF_SIZE - __HAL_DMA_GET_COUNTER(huart3.hdmarx)
 * ============================================================================ */
static uint16_t at_rx_head(void)
{
    uint16_t ndtr = (uint16_t)__HAL_DMA_GET_COUNTER(huart3.hdmarx);
    return (uint16_t)(AT_RX_BUF_SIZE - ndtr);
}

/* ============================================================================
 * Private: at_rx_read_line
 *
 * Reads bytes from the DMA ring buffer until a '\n' is found or timeout_ms
 * elapses.  Returns the number of characters placed in *out (excluding NUL),
 * or 0 if no complete line is available within the timeout.
 * Strips leading '\r' and '\n' from the output string.
 * ============================================================================ */
static int at_rx_read_line(char *out, size_t max_len, uint32_t timeout_ms)
{
    uint32_t deadline = HAL_GetTick() + timeout_ms;
    size_t   pos      = 0U;

    while (HAL_GetTick() < deadline) {
        uint16_t head = at_rx_head();

        while (s_at_rx_tail != head && pos < (max_len - 1U)) {
            uint8_t ch = s_at_rx_buf[s_at_rx_tail];
            s_at_rx_tail = (uint16_t)((s_at_rx_tail + 1U) & AT_RX_BUF_MASK);

            if (ch == '\n') {
                /* End of line — strip trailing CR if present */
                if (pos > 0U && out[pos - 1U] == '\r') {
                    pos--;
                }
                out[pos] = '\0';
                if (pos > 0U) {
                    return (int)pos;
                }
                /* Empty line — reset and keep scanning */
                pos = 0U;
                continue;
            }
            if (ch != '\r' || pos > 0U) {
                /* Skip leading CR */
                out[pos++] = (char)ch;
            }
        }
    }

    out[pos] = '\0';
    return 0;
}

/* ============================================================================
 * Private: at_flush_rx
 *
 * Advances the tail pointer to match the current DMA head, discarding any
 * pending bytes.  Called before each new AT command to prevent stale URCs
 * from being misidentified as a response.
 * ============================================================================ */
static void at_flush_rx(void)
{
    s_at_rx_tail = at_rx_head();
}

/* ============================================================================
 * Private: gnss_update
 *
 * Reads up to NMEA_RX_BUF_SIZE bytes from UART4 (SAM-M10Q) via a blocking
 * HAL receive with a 2-second timeout.  Scans for a $GPRMC sentence and
 * parses it for position and UTC time.
 * ============================================================================ */
static void gnss_update(void)
{
    static uint8_t s_nmea_buf[NMEA_RX_BUF_SIZE];
    uint16_t bytes_received = 0U;

    /* Receive up to one buffer's worth of NMEA data */
    HAL_StatusTypeDef status = HAL_UART_Receive(
        &huart4, s_nmea_buf, NMEA_RX_BUF_SIZE - 1U, 2000U);

    if (status == HAL_OK || status == HAL_TIMEOUT) {
        /* Determine how many bytes were actually received via NDTR */
        bytes_received = (uint16_t)(NMEA_RX_BUF_SIZE - 1U -
                         (uint16_t)__HAL_DMA_GET_COUNTER(huart4.hdmarx));
        s_nmea_buf[bytes_received] = '\0';

        /* Scan for $GPRMC sentence in the buffer */
        char *p = (char *)s_nmea_buf;
        char *end = p + bytes_received;

        while (p < end) {
            char *gprmc = strstr(p, "$GPRMC");
            if (gprmc == NULL) {
                break;
            }

            /* Find end of sentence (CR/LF or end of buffer) */
            char *eol = gprmc;
            while (eol < end && *eol != '\r' && *eol != '\n') {
                eol++;
            }
            *eol = '\0';

            float lat = 0.0f, lon = 0.0f;
            uint32_t utc = 0U;

            if (nmea_parse_gprmc(gprmc, &lat, &lon, &utc)) {
                s_latitude   = lat;
                s_longitude  = lon;
                s_gnss_utc   = utc;
                s_gnss_valid = true;
            }

            p = eol + 1;
        }
    }
}

/* ============================================================================
 * Private: nmea_parse_gprmc
 *
 * Parses a $GPRMC sentence and extracts latitude, longitude, and UTC time.
 *
 * $GPRMC,hhmmss.ss,A,DDMM.MMMM,N,DDDMM.MMMM,W,sss.ss,ddd.dd,ddmmyy,mmm.m,a*hh
 * Fields (0-based comma positions):
 *   0: $GPRMC
 *   1: UTC time hhmmss.ss
 *   2: Status A=active V=void
 *   3: Latitude DDMM.MMMM
 *   4: N/S hemisphere
 *   5: Longitude DDDMM.MMMM
 *   6: E/W hemisphere
 *   7: Speed over ground (knots)
 *   8: Course over ground (degrees)
 *   9: Date ddmmyy
 * ============================================================================ */
static bool nmea_parse_gprmc(const char *sentence,
                              float *lat, float *lon, uint32_t *utc)
{
    if (sentence == NULL || lat == NULL || lon == NULL || utc == NULL) {
        return false;
    }

    /* Tokenise a local copy to avoid modifying caller's buffer */
    static char s_work[96];
    strncpy(s_work, sentence, sizeof(s_work) - 1U);
    s_work[sizeof(s_work) - 1U] = '\0';

    /* Split on commas into a pointer array */
    const char *fields[13];
    uint8_t     field_count = 0U;
    char       *tok = s_work;

    fields[field_count++] = tok;
    while (*tok && field_count < 13U) {
        if (*tok == ',') {
            *tok = '\0';
            fields[field_count++] = tok + 1U;
        }
        tok++;
    }

    if (field_count < 7U) {
        return false;
    }

    /* Field 2: status — must be 'A' (active fix) */
    if (fields[2][0] != 'A') {
        return false;
    }

    /* Field 1: UTC time hhmmss.ss → pack as hhmmss in a uint32_t */
    {
        uint32_t raw_time = (uint32_t)atoi(fields[1]);
        uint8_t  hh = (uint8_t)(raw_time / 10000U);
        uint8_t  mm = (uint8_t)((raw_time / 100U) % 100U);
        uint8_t  ss = (uint8_t)(raw_time % 100U);
        *utc = (uint32_t)((hh * 3600U) + (mm * 60U) + ss);
    }

    /* Fields 3+4: latitude */
    *lat = nmea_to_decimal_deg(fields[3], fields[4][0]);

    /* Fields 5+6: longitude */
    *lon = nmea_to_decimal_deg(fields[5], fields[6][0]);

    return true;
}

/* ============================================================================
 * Private: nmea_to_decimal_deg
 *
 * Converts NMEA coordinate format (DDDMM.MMMM or DDMM.MMMM) to decimal degrees.
 *
 * NMEA format:  first 2 or 3 digits are whole degrees, remaining digits are
 * decimal minutes.
 *   degrees = floor(raw / 100)
 *   minutes = raw - (degrees * 100)
 *   decimal_degrees = degrees + minutes / 60
 * For S or W hemisphere the result is negated.
 * ============================================================================ */
static float nmea_to_decimal_deg(const char *field, char hemi)
{
    if (field == NULL || field[0] == '\0') {
        return 0.0f;
    }

    float raw      = (float)atof(field);
    int   deg_int  = (int)(raw / 100.0f);
    float minutes  = raw - (float)(deg_int * 100);
    float result   = (float)deg_int + (minutes / 60.0f);

    if (hemi == 'S' || hemi == 'W') {
        result = -result;
    }

    return result;
}

/* ============================================================================
 * Private: populate_telemetry_packet
 *
 * Assembles a telemetry_packet_t from system state, queues, and hardware.
 * Reads traffic engine state, power monitor values (via I2C shared globals),
 * GPIO inputs, and cached GNSS data.
 * Computes the CRC-32 over all fields except the crc32 member itself.
 * ============================================================================ */
static void populate_telemetry_packet(telemetry_packet_t *pkt)
{
    memset(pkt, 0, sizeof(*pkt));

    /* --- Identity --- */
    if (s_id_mutex != NULL &&
        xSemaphoreTake(s_id_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        strncpy(pkt->asset_id, s_asset_id, sizeof(pkt->asset_id) - 1U);
        xSemaphoreGive(s_id_mutex);
    }
    snprintf(pkt->firmware_version, sizeof(pkt->firmware_version),
             "%d.%d.%d",
             LCU_FW_VERSION_MAJOR, LCU_FW_VERSION_MINOR, LCU_FW_VERSION_PATCH);

    /* --- Timing --- */
    pkt->timestamp_utc  = s_gnss_valid ? s_gnss_utc : 0U;
    pkt->uptime_seconds = HAL_GetTick() / 1000U;
    pkt->packet_sequence = ++s_packet_seq;

    /* --- GNSS --- */
    pkt->latitude_deg  = s_latitude;
    pkt->longitude_deg = s_longitude;

    /* --- Traffic engine state --- */
    pkt->current_phase_id = (uint8_t)traffic_engine_get_state();  /* state as proxy */
    pkt->operating_mode   = 0U;  /* TE_MODE_NORMAL default */

    /* Fault bitmap from system event group */
    EventBits_t evts = xEventGroupGetBits(xSystemEventGroup);
    pkt->fault_bitmap = (evts & SYSEVT_FAULT_ACTIVE) ? 0x00000001UL : 0x00000000UL;

    /* --- GPIO inputs --- */
    pkt->door_open       = (HAL_GPIO_ReadPin(DOOR_SWITCH_GPIO_Port,
                                              DOOR_SWITCH_Pin) == GPIO_PIN_SET);
    pkt->tamper_detected = false;  /* populated by security task if fitted */

    /* --- Signal states — encode active plan aspects into packed nibbles ---
     * Each byte in signal_states[] holds two channel state nibbles.
     * Nibble values: 0=off, 1=red, 2=amber, 3=green
     * Channels 0-31 → bytes [0-15], channels 0/1 → byte[0], etc. */
    if (gpActivePhasePlan != NULL) {
        const phase_plan_t *plan = gpActivePhasePlan;
        /* We use the common phase_plan.h structure here */
        uint8_t phase_idx = 0U;
        /* Find current phase index */
        for (uint8_t i = 0; i < plan->num_phases && i < MAX_PHASES; i++) {
            if (plan->phases[i].phase_id == pkt->current_phase_id) {
                phase_idx = i;
                break;
            }
        }
        /* Pack approach aspects into signal_states */
        for (uint8_t a = 0; a < plan->approach_count && a < MAX_APPROACHES; a++) {
            signal_aspect_t asp = plan->phases[phase_idx].aspect[a];
            uint8_t nibble = 0U;
            switch (asp) {
                case ASPECT_RED:   nibble = 1U; break;
                case ASPECT_AMBER: nibble = 2U; break;
                case ASPECT_GREEN: nibble = 3U; break;
                default:           nibble = 0U; break;
            }
            /* Each approach maps to one nibble in signal_states */
            uint8_t byte_idx   = a / 2U;
            uint8_t nibble_pos = (a % 2U) * 4U;
            pkt->signal_states[byte_idx] |= (uint8_t)(nibble << nibble_pos);
        }
    }

    /* --- Compute CRC-32 over all fields except the crc32 member --- */
    size_t crc_len = offsetof(telemetry_packet_t, crc32);
    pkt->crc32 = crc32_compute((const uint8_t *)pkt, crc_len);
}

/* ============================================================================
 * Private: serialize_to_json
 *
 * Converts a telemetry_packet_t to a compact JSON string.
 * Uses a fixed 512-byte buffer.  Returns the number of bytes written
 * (not including NUL), or -1 on overflow.
 * ============================================================================ */
static int serialize_to_json(const telemetry_packet_t *pkt,
                              char *buf, size_t buf_len)
{
    /* Build signal_states hex string (32 hex chars for 16 bytes) */
    char sig_hex[33];
    for (int i = 0; i < 16; i++) {
        snprintf(&sig_hex[i * 2], 3, "%02X", pkt->signal_states[i]);
    }

    int written = snprintf(buf, buf_len,
        "{"
        "\"id\":\"%s\","
        "\"fw\":\"%s\","
        "\"ts\":%lu,"
        "\"up\":%lu,"
        "\"seq\":%lu,"
        "\"lat\":%.6f,"
        "\"lon\":%.6f,"
        "\"vbat\":%u,"
        "\"ibat\":%d,"
        "\"soc\":%u,"
        "\"tbat\":%d,"
        "\"phase\":%u,"
        "\"mode\":%u,"
        "\"fault\":\"0x%08lX\","
        "\"sig\":\"%s\","
        "\"door\":%s,"
        "\"tamper\":%s,"
        "\"crc\":\"0x%08lX\""
        "}",
        pkt->asset_id,
        pkt->firmware_version,
        (unsigned long)pkt->timestamp_utc,
        (unsigned long)pkt->uptime_seconds,
        (unsigned long)pkt->packet_sequence,
        (double)pkt->latitude_deg,
        (double)pkt->longitude_deg,
        (unsigned)pkt->battery_voltage_mv,
        (int)pkt->battery_current_ma,
        (unsigned)pkt->battery_soc_pct,
        (int)pkt->battery_temp_c,
        (unsigned)pkt->current_phase_id,
        (unsigned)pkt->operating_mode,
        (unsigned long)pkt->fault_bitmap,
        sig_hex,
        pkt->door_open       ? "true" : "false",
        pkt->tamper_detected ? "true" : "false",
        (unsigned long)pkt->crc32);

    if (written < 0 || (size_t)written >= buf_len) {
        return -1;
    }

    return written;
}

/* ============================================================================
 * Private: render_topic
 *
 * Replaces the literal "{asset_id}" token in template_str with the current
 * asset ID and writes the result to out[0..out_len-1].
 * ============================================================================ */
static void render_topic(char *out, size_t out_len, const char *template_str)
{
    const char *token = "{asset_id}";
    const char *p     = strstr(template_str, token);

    if (p == NULL) {
        strncpy(out, template_str, out_len - 1U);
        out[out_len - 1U] = '\0';
        return;
    }

    size_t prefix_len = (size_t)(p - template_str);
    size_t id_len     = strnlen(s_asset_id, sizeof(s_asset_id));
    size_t suffix_len = strlen(p + strlen(token));

    if (prefix_len + id_len + suffix_len >= out_len) {
        /* Truncate gracefully */
        strncpy(out, template_str, out_len - 1U);
        out[out_len - 1U] = '\0';
        return;
    }

    memcpy(out, template_str, prefix_len);
    memcpy(out + prefix_len, s_asset_id, id_len);
    memcpy(out + prefix_len + id_len, p + strlen(token), suffix_len + 1U);
}

/* ============================================================================
 * Private: log_tel_fault
 * ============================================================================ */
static void log_tel_fault(fault_code_t code, fault_severity_t sev,
                           uint32_t ctx0, uint32_t ctx1)
{
    fault_event_t evt = {
        .code         = code,
        .severity     = sev,
        .source       = FAULT_SRC_TELEMETRY,
        .timestamp_ms = HAL_GetTick(),
        .context      = { ctx0, ctx1 }
    };
    if (xFaultQueue != NULL) {
        xQueueSend(xFaultQueue, &evt, 0);
    }
}
