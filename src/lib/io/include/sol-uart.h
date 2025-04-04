/*
 * This file is part of the Soletta Project
 *
 * Copyright (C) 2015 Intel Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief These routines are used for UART access under Solleta.
 */

/**
 * @ingroup IO
 *
 * @{
 *
 */

struct sol_uart;

enum sol_uart_baud_rate {
    SOL_UART_BAUD_RATE_9600 = 0,
    SOL_UART_BAUD_RATE_19200,
    SOL_UART_BAUD_RATE_38400,
    SOL_UART_BAUD_RATE_57600,
    SOL_UART_BAUD_RATE_115200
};

enum sol_uart_data_bits {
    SOL_UART_DATA_BITS_8 = 0,
    SOL_UART_DATA_BITS_7,
    SOL_UART_DATA_BITS_6,
    SOL_UART_DATA_BITS_5
};

enum sol_uart_parity {
    SOL_UART_PARITY_NONE = 0,
    SOL_UART_PARITY_EVEN,
    SOL_UART_PARITY_ODD
};

enum sol_uart_stop_bits {
    SOL_UART_STOP_BITS_ONE = 0,
    SOL_UART_STOP_BITS_TWO
};

struct sol_uart_config {
#define SOL_UART_CONFIG_API_VERSION (1)
    uint16_t api_version;
    enum sol_uart_baud_rate baud_rate;
    enum sol_uart_data_bits data_bits;
    enum sol_uart_parity parity;
    enum sol_uart_stop_bits stop_bits;
    void (*rx_cb)(void *user_data, struct sol_uart *uart, unsigned char byte_read); /** Set a callback to be called every time a character is received on UART */
    const void *rx_cb_user_data;
    bool flow_control; /** Enables software flow control(XOFF and XON) */
};

/**
 * Open an UART bus.
 *
 * @param port_name the name of UART port, on Linux it should be ttyUSB0 or
 * ttyACM0 in small OSes it should be a id number.
 * @param config The UART bus configuration
 * @return A new UART bus handle
 */
struct sol_uart *sol_uart_open(const char *port_name, const struct sol_uart_config *config);

/**
 * Close an UART bus.
 *
 * @param uart The UART bus handle
 */
void sol_uart_close(struct sol_uart *uart);

/**
 * Perform a UART asynchronous transmission.
 *
 * @param uart The UART bus handle
 * @param tx The output buffer to be sent
 * @param length number of bytes to be transfer
 * @param tx_cb callback to be called when transmission finish, in case of
 * success the status parameter on tx_cb should be equal to length otherwise
 * an error happen during the transmission
 * @param data the first parameter of tx_cb
 * @return true if transfer was started
 *
 * @note Caller should guarantee that tx buffer will not be freed until
 * callback is called.
 */
bool sol_uart_write(struct sol_uart *uart, const unsigned char *tx, unsigned int length, void (*tx_cb)(void *data, struct sol_uart *uart, unsigned char *tx, int status), const void *data);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
