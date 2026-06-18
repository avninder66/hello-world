/**
 * @file    commissioning.h
 * @brief   Commissioning and service UART interface for the Lumina LCU-100.
 *
 * Provides a line-oriented command shell over USART3 (115200 8N1) or USB-CDC
 * virtual COM port, intended for use by installation engineers and field
 * service technicians.
 *
 * Service port hardware:
 *   - Physical port : USART3 (PA9/USART3_TX, PA10/USART3_RX), 115200 8N1
 *   - Virtual port  : USB CDC on OTG_FS (PA11/PA12), same line protocol
 *   - Echo          : character echo on by default (toggle with "echo off")
 *
 * Command format:
 *   <command> [arg0] [arg1] ... <CR>
 *
 * Response format:
 *   Every response line is prefixed with "[LUMINA] " for unambiguous machine
 *   parsing.  Responses are terminated with CRLF.
 *
 * PIN authentication:
 *   Commands marked requires_auth == true require a prior successful
 *   "auth <PIN>" command.  The session is valid for AUTH_SESSION_TIMEOUT_S
 *   seconds (300 s default) after which it automatically expires.  The
 *   factory default PIN is "1234" and is configurable via "set auth_pin".
 *
 * Thread safety:
 *   commissioning_send_response() acquires g_usart_mutex before writing to
 *   the UART so it is safe to call from any task.
 *
 * Copyright (c) Lumina Traffic Systems Ltd.  All rights reserved.
 * SPDX-License-Identifier: Proprietary
 */

#ifndef COMMISSIONING_H
#define COMMISSIONING_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Protocol constants
 * ========================================================================= */

/** USART3 baud rate. */
#define COMMISSIONING_BAUD_RATE         115200U

/** Maximum length of a single input line including the NUL terminator. */
#define MAX_CMD_LEN                     128U

/** Maximum number of argument tokens per command (including the command name). */
#define MAX_ARGS                        8U

/** PIN authentication session validity in seconds. */
#define AUTH_SESSION_TIMEOUT_S          300U

/** Length of the PIN string including the NUL terminator (4-digit → 5 bytes). */
#define AUTH_PIN_MAX_LEN                8U

/** Factory default PIN. */
#define AUTH_PIN_DEFAULT                "1234"

/** Maximum number of commands that can be registered in the command table. */
#define COMMISSIONING_MAX_COMMANDS      24U

/** Maximum length of a command name including the NUL terminator. */
#define COMMISSIONING_CMD_NAME_LEN      16U

/** Maximum length of a help string including the NUL terminator. */
#define COMMISSIONING_HELP_STR_LEN      64U

/** USART3 Rx DMA ring buffer size in bytes (must be power of two). */
#define COMMISSIONING_RX_BUF_SIZE       256U

/* =========================================================================
 * Command enumeration
 *
 * Used internally to identify built-in command handlers.  Application code
 * that registers custom handlers via commissioning_register_handler() does
 * not need to use this enum.
 * ========================================================================= */

typedef enum
{
    CMD_HELP            = 0,  /**< List all registered commands with help strings   */
    CMD_STATUS          = 1,  /**< Print firmware, phase, battery, fault summary    */
    CMD_SET_PHASE       = 2,  /**< Request a specific phase (auth required)         */
    CMD_SET_MODE        = 3,  /**< Set operating mode (auth required)               */
    CMD_LOAD_PLAN       = 4,  /**< Load a new phase plan from XMODEM stream         */
    CMD_DUMP_FAULTS     = 5,  /**< Print the FRAM fault log                         */
    CMD_CLEAR_FAULTS    = 6,  /**< Clear all fault log entries (auth required)      */
    CMD_SET_ASSET_ID    = 7,  /**< Set the asset identifier string (auth required)  */
    CMD_SET_CONFIG      = 8,  /**< Set a named configuration parameter (auth req.)  */
    CMD_FIRMWARE_INFO   = 9,  /**< Print git hash, build date, board serial         */
    CMD_REBOOT          = 10, /**< Trigger a controlled system reboot (auth req.)   */
    CMD_DFU_MODE        = 11, /**< Jump to STM32 DFU bootloader (auth required)     */
    CMD_SELF_TEST       = 12, /**< Trigger safety supervisor self-test              */
} commissioning_cmd_t;

/* =========================================================================
 * Command handler typedef
 *
 * argc is the number of tokens in argv[], including argv[0] (the command
 * name itself).  The handler returns 0 on success or a negative error code.
 * Output should be written via commissioning_send_response().
 * ========================================================================= */

/**
 * @brief Command handler function pointer type.
 *
 * @param argc  Number of argument strings (including the command name at [0]).
 * @param argv  NULL-terminated array of argument string pointers.
 * @return      0 on success; negative value on error (definition is per handler).
 */
typedef int (*cmd_handler_t)(int argc, char *argv[]);

/* =========================================================================
 * Command table entry
 * ========================================================================= */

/**
 * @brief Descriptor for one registered command.
 *
 * Populated by commissioning_register_handler() and stored in the internal
 * command table.
 */
typedef struct
{
    /** Command name string (e.g. "status").  Null-terminated. */
    char            name[COMMISSIONING_CMD_NAME_LEN];

    /** Handler function pointer. */
    cmd_handler_t   handler;

    /** One-line description shown in the "help" listing.  Null-terminated. */
    char            help_string[COMMISSIONING_HELP_STR_LEN];

    /**
     * If true the "auth <PIN>" command must have been issued within the
     * current AUTH_SESSION_TIMEOUT_S window before this command is accepted.
     */
    bool            requires_auth;
} commissioning_command_entry_t;

/* =========================================================================
 * Public API
 * ========================================================================= */

/**
 * @brief Initialise the commissioning module.
 *
 * Configures USART3 Rx DMA, registers built-in command handlers, and creates
 * the UART transmit mutex.  Must be called before vTaskStartScheduler().
 *
 * @return true on success; false if any resource allocation failed.
 */
bool commissioning_init(void);

/**
 * @brief FreeRTOS task entry point for the commissioning shell.
 *
 * Polls the USART3 DMA ring buffer for complete lines (terminated by CR or
 * LF), tokenises them, performs authentication checks, and dispatches to the
 * registered handler.  Characters are echoed as they are received if echo is
 * enabled.
 *
 * Stack requirement: ~1024 words.  Priority: osPriorityLow.
 *
 * @param pvParameters  Unused; pass NULL.
 */
void commissioning_task(void *pvParameters);

/**
 * @brief Register a command handler in the command dispatch table.
 *
 * Up to COMMISSIONING_MAX_COMMANDS entries may be registered.  Handlers are
 * matched by exact strcmp against the received command name.  If a handler
 * for the same name already exists it is replaced.
 *
 * @param name         Command name string (e.g. "status").  Copied internally.
 * @param handler      Function pointer to invoke.
 * @param help_string  Short description for "help" listing.  Copied internally.
 * @param requires_auth  true if the "auth" command must precede this command.
 * @return true if the handler was registered; false if the table is full.
 */
bool commissioning_register_handler(const char    *name,
                                    cmd_handler_t  handler,
                                    const char    *help_string,
                                    bool           requires_auth);

/**
 * @brief Send a formatted response string to the commissioning port.
 *
 * Prepends "[LUMINA] " to the message and appends CRLF before transmitting.
 * Thread-safe: acquires the internal UART mutex before writing.
 *
 * @param message  Null-terminated response string (without the prefix or CRLF).
 */
void commissioning_send_response(const char *message);

#ifdef __cplusplus
}
#endif

#endif /* COMMISSIONING_H */
