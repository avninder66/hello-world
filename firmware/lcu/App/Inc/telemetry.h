/**
 * @file    telemetry.h
 * @brief   LCU-100 Cloud and Local Telemetry Module
 *
 * Manages LTE connectivity via Quectel EC21 modem (USART3) and publishes
 * JSON telemetry packets to the Lumina MQTT broker using AT+QMTPUBEX commands.
 * GNSS position is sourced from the u-blox SAM-M10Q receiver on UART4.
 *
 * Connectivity model
 * ------------------
 * The module implements a state machine that drives the modem through
 * power-on, SIM registration, PDP context activation, MQTT broker connection,
 * and periodic publish cycles.  On consecutive failures an exponential backoff
 * is applied (30 s → 60 s → 120 s → 300 s cap).  After five consecutive modem
 * failures FAULT_COMMS_MODEM_OFFLINE is raised and telemetry is suspended
 * while the traffic engine continues to operate normally.
 *
 * MQTT topics (runtime asset_id substituted)
 * -------------------------------------------
 *   lumina/v1/{asset_id}/telemetry   — 30 s normal publish cadence
 *   lumina/v1/{asset_id}/faults      — 5 s cadence while fault active
 *   lumina/v1/{asset_id}/commands    — subscribed (inbound TMC commands)
 *
 * Thread safety
 * -------------
 * telemetry_publish_now() and telemetry_set_asset_id() are safe to call from
 * any task.  They operate on atomically written shared state or post to the
 * internal xTelPublishQueue rather than accessing modem hardware directly.
 */

#ifndef __TELEMETRY_H
#define __TELEMETRY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/* ============================================================================
 * Timing configuration
 * ============================================================================ */

/** Normal telemetry publish interval — 30 seconds */
#define TELEMETRY_PUBLISH_INTERVAL_MS       30000U

/** Publish interval when a fault is active — 5 seconds */
#define TELEMETRY_FAULT_INTERVAL_MS         5000U

/** GNSS position update interval — 1 hour (position rarely changes) */
#define TELEMETRY_GNSS_INTERVAL_MS          3600000U

/** AT command response timeout in milliseconds */
#define TELEMETRY_AT_TIMEOUT_MS             5000U

/** MQTT connect/open timeout */
#define TELEMETRY_MQTT_TIMEOUT_MS           15000U

/** Maximum consecutive modem failures before declaring offline */
#define TELEMETRY_MAX_MODEM_FAILURES        5U

/** Exponential backoff ceiling in milliseconds */
#define TELEMETRY_BACKOFF_MAX_MS            300000U

/* ============================================================================
 * MQTT topic templates
 *
 * The {asset_id} token is replaced at runtime with the configured asset ID.
 * Maximum rendered topic length is TELEMETRY_TOPIC_LEN.
 * ============================================================================ */

#define LUMINA_MQTT_BROKER_HOST             "mqtt.lumina.cloud"
#define LUMINA_MQTT_BROKER_PORT             1883U

/** Telemetry data topic (outbound) */
#define LUMINA_MQTT_TOPIC_TELEMETRY         "lumina/v1/{asset_id}/telemetry"

/** Active fault notification topic (outbound) */
#define LUMINA_MQTT_TOPIC_FAULTS            "lumina/v1/{asset_id}/faults"

/** TMC command topic (inbound subscription) */
#define LUMINA_MQTT_TOPIC_COMMANDS          "lumina/v1/{asset_id}/commands"

/** Maximum length of a rendered MQTT topic string including NUL */
#define TELEMETRY_TOPIC_LEN                 64U

/* ============================================================================
 * LTE / PDP configuration
 * ============================================================================ */

/** PDP context APN for Vodafone "everywhere" M2M SIM */
#define TELEMETRY_LTE_APN                   "everywhere"

/** EC21 context ID used for all AT+QICSGP / AT+QIOPEN operations */
#define TELEMETRY_PDP_CTX_ID                1U

/** EC21 MQTT socket ID */
#define TELEMETRY_MQTT_SOCKET_ID            0U

/* ============================================================================
 * AT command response codes
 * ============================================================================ */

typedef enum {
    AT_OK           = 0,    /**< Modem responded "OK"                          */
    AT_ERROR        = 1,    /**< Modem responded "ERROR" or "+CME ERROR"       */
    AT_TIMEOUT      = 2,    /**< No response within TELEMETRY_AT_TIMEOUT_MS    */
    AT_NO_CARRIER   = 3,    /**< Modem responded "NO CARRIER" (link dropped)   */
} at_response_t;

/* ============================================================================
 * Telemetry state machine
 * ============================================================================ */

typedef enum {
    TEL_DISCONNECTED    = 0,    /**< Modem not yet initialised or link lost      */
    TEL_CONNECTING      = 1,    /**< PDP context activation / SIM registration   */
    TEL_REGISTERED      = 2,    /**< Registered on network, activating data      */
    TEL_CONNECTED       = 3,    /**< PDP active, MQTT broker connected           */
    TEL_PUBLISHING      = 4,    /**< AT+QMTPUBEX in progress                    */
    TEL_ERROR           = 5,    /**< Unrecoverable error, in backoff             */
} telemetry_state_t;

/* ============================================================================
 * Telemetry packet
 *
 * Packed to avoid padding bytes so sizeof() matches the wire representation.
 * All multi-byte fields are little-endian (native Cortex-M7).
 *
 * signal_states[16]: each byte encodes two channel states in 4 bits each:
 *   bits [7:4] = channel (2*i+1) state   bits [3:0] = channel (2*i) state
 *   State nibble values: 0=off, 1=red, 2=amber, 3=green
 * ============================================================================ */

#pragma pack(push, 1)
typedef struct {
    /* --- Identity --------------------------------------------------------- */
    char        asset_id[16];           /**< Null-terminated asset ID string   */
    char        firmware_version[8];    /**< "M.m.p\0\0\0" packed version      */

    /* --- Timing ----------------------------------------------------------- */
    uint32_t    timestamp_utc;          /**< Unix time from GNSS or RTC        */
    uint32_t    uptime_seconds;         /**< Seconds since last reset          */
    uint32_t    packet_sequence;        /**< Monotonically increasing counter  */

    /* --- GNSS ------------------------------------------------------------- */
    float       latitude_deg;           /**< Decimal degrees, WGS-84           */
    float       longitude_deg;          /**< Decimal degrees, WGS-84           */

    /* --- Power / battery -------------------------------------------------- */
    uint16_t    battery_voltage_mv;     /**< Battery terminal voltage (mV)     */
    int16_t     battery_current_ma;     /**< Charge (+) or discharge (-) in mA */
    uint8_t     battery_soc_pct;        /**< State of charge 0–100 %           */
    int8_t      battery_temp_c;         /**< Battery temperature in °C         */

    /* --- Traffic engine --------------------------------------------------- */
    uint8_t     current_phase_id;       /**< Active phase (0 = none/all-red)   */
    uint8_t     operating_mode;         /**< operating_mode_t value            */
    uint32_t    fault_bitmap;           /**< Active fault code bitmask         */

    /* --- Signal outputs --------------------------------------------------- */
    uint8_t     signal_states[16];      /**< 4 bits per channel, 32 channels   */

    /* --- Security / physical ---------------------------------------------- */
    bool        door_open;              /**< Cabinet door open sense           */
    bool        tamper_detected;        /**< Tamper detect input               */

    /* --- Integrity -------------------------------------------------------- */
    uint32_t    crc32;                  /**< CRC-32 of all preceding bytes     */
} telemetry_packet_t;
#pragma pack(pop)

/* ============================================================================
 * Public API
 * ============================================================================ */

/**
 * @brief  Initialise the telemetry module.
 *
 *         Creates internal FreeRTOS objects (publish-trigger semaphore).
 *         Must be called before vTaskStartScheduler().
 *
 * @return true on success, false if resource allocation failed.
 */
bool telemetry_init(void);

/**
 * @brief  FreeRTOS task entry point.
 *
 *         Stack: 2048 words.  Priority: osPriorityNormal.
 *         Registered in main.c as "Telemetry".
 *
 * @param pvParameters  Unused; pass NULL.
 */
void telemetry_task(void *pvParameters);

/**
 * @brief  Request an immediate out-of-cycle telemetry publish.
 *
 *         Thread-safe.  Posts a trigger to the internal semaphore; the next
 *         telemetry_task iteration will publish before the normal interval
 *         expires.  No-op if a publish is already in progress.
 */
void telemetry_publish_now(void);

/**
 * @brief  Return the current telemetry state machine state.
 *
 *         Thread-safe atomic read.
 *
 * @return Current telemetry_state_t.
 */
telemetry_state_t telemetry_get_state(void);

/**
 * @brief  Set the asset identifier string used in MQTT topics and packets.
 *
 *         Thread-safe.  The string is copied to an internal buffer.
 *         Maximum length: 15 characters (+ NUL).
 *
 * @param[in] id  Null-terminated ASCII string.
 */
void telemetry_set_asset_id(const char *id);

/**
 * @brief  Return the HAL tick count of the last successful MQTT publish.
 *
 *         Returns 0 if no publish has yet completed.
 *
 * @return HAL_GetTick() value at last successful publish.
 */
uint32_t telemetry_get_last_publish_tick(void);

#ifdef __cplusplus
}
#endif

#endif /* __TELEMETRY_H */
