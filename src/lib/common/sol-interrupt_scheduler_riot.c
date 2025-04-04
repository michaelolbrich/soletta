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

#include "sol-interrupt_scheduler_riot.h"

#include <stdbool.h>
#include <stdlib.h>
#include <thread.h>

static kernel_pid_t pid;

enum interrupt_type {
    GPIO,
#ifdef USE_UART
    UART_RX,
    UART_TX
#endif
};

struct interrupt_data_base {
    uint16_t deleted : 1, refcount : 15;
};

struct interrupt_data {
    struct interrupt_data_base base;
    void *cb;
    void *data;
};

#ifdef USE_UART
struct uart_interrupt_data {
    struct interrupt_data_base base;
    uart_t uart_id;
    void *rx_cb;
    void *tx_cb;
    void *data;
};

struct uart_rx_interrupt_data {
    char char_read;
    struct uart_interrupt_data *uart_int;
};
#endif

void
sol_interrupt_scheduler_set_pid(kernel_pid_t p)
{
    pid = p;
}

/* Run in interrupt context */
static void
gpio_cb(void *data)
{
    msg_t m;
    struct interrupt_data *int_data = data;

    int_data->base.refcount++;
    m.type = GPIO;
    m.content.ptr = data;
    msg_send_int(&m, pid);
}

int
sol_interrupt_scheduler_gpio_init_int(gpio_t dev, gpio_pp_t pullup, gpio_flank_t flank, gpio_cb_t cb, void *arg, void **handler)
{
    int retval = -1;
    struct interrupt_data *int_data = malloc(sizeof(struct interrupt_data));

    if (!int_data)
        return retval;

    int_data->cb = cb;
    int_data->data = arg;
    int_data->base.deleted = false;
    int_data->base.refcount = 1;
    retval = gpio_init_int(dev, pullup, flank, gpio_cb, int_data);
    if (retval != 0) {
        free(int_data);
        int_data = NULL;
    }

    if (handler != NULL && int_data != NULL)
        *handler = int_data;
    return retval;
}

static void
interrupt_data_base_unref(struct interrupt_data_base *base)
{
    if (!--base->refcount)
        free(base);
}

static void
interrupt_scheduler_handler_free(void *handler)
{
    struct interrupt_data_base *base = handler;

    if (base == NULL)
        return;
    base->deleted = true;
    interrupt_data_base_unref(base);
}

void
sol_interrupt_scheduler_gpio_stop(gpio_t dev, void *handler)
{
    gpio_init_in(dev, GPIO_NOPULL);
    interrupt_scheduler_handler_free(handler);
}

/* Run in interrupt context */
#ifdef USE_UART
static void
uart_rx_cb(void *data, char char_read)
{
    msg_t m;
    struct uart_interrupt_data *int_data = data;
    struct uart_rx_interrupt_data *rx_int_data = malloc(sizeof(struct uart_rx_interrupt_data));

    rx_int_data = malloc(sizeof(struct uart_rx_interrupt_data));
    if (!rx_int_data)
        return;

    rx_int_data->uart_int = int_data;
    rx_int_data->char_read = char_read;

    int_data->base.refcount++;
    m.type = UART_RX;
    m.content.ptr = (char *)rx_int_data;
    msg_send_int(&m, pid);
}

/* Run in interrupt context */
static int
uart_tx_cb(void *data)
{
    msg_t m;
    struct uart_interrupt_data *int_data = data;

    int_data->base.refcount++;
    m.type = UART_TX;
    m.content.ptr = data;
    msg_send_int(&m, pid);
    return 0;
}

int
sol_interrupt_scheduler_uart_init_int(uart_t uart, uint32_t baudrate, uart_rx_cb_t rx_cb, uart_tx_cb_t tx_cb, void *arg, void **handler)
{
    int retval = -1;
    struct uart_interrupt_data *int_data = malloc(sizeof(struct uart_interrupt_data));

    if (!int_data)
        return retval;

    int_data->uart_id = uart;
    int_data->rx_cb = rx_cb;
    int_data->tx_cb = tx_cb;
    int_data->data = arg;
    int_data->base.deleted = false;
    int_data->base.refcount = 1;

    retval = uart_init(uart, baudrate, uart_rx_cb, uart_tx_cb, int_data);
    if (retval != 0) {
        free(int_data);
        int_data = NULL;
    }

    if (handler != NULL && int_data != NULL)
        *handler = int_data;
    return retval;
}

void
sol_interrupt_scheduler_uart_stop(uart_t uart, void *handler)
{
    uart_init_blocking(uart, 9600);
    interrupt_scheduler_handler_free(handler);
}
#endif

void
sol_interrupt_scheduler_process(msg_t *msg)
{
    switch (msg->type) {
    case GPIO: {
        struct interrupt_data *int_data = (void *)msg->content.ptr;
        gpio_cb_t cb = int_data->cb;
        if (!int_data->base.deleted)
            cb(int_data->data);
        interrupt_data_base_unref(&int_data->base);
        break;
    }
#ifdef USE_UART
    case UART_RX: {
        struct uart_rx_interrupt_data *rx_data = (void *)msg->content.ptr;
        uart_rx_cb_t cb = rx_data->uart_int->rx_cb;
        if (!rx_data->uart_int->base.deleted)
            cb(rx_data->uart_int->data, rx_data->char_read);
        interrupt_data_base_unref(&rx_data->uart_int->base);
        free(rx_data);
        break;
    }
    case UART_TX: {
        struct uart_interrupt_data *int_data = (void *)msg->content.ptr;
        uart_tx_cb_t cb = int_data->tx_cb;
        if (!int_data->base.deleted) {
            if (cb(int_data->data))
                uart_tx_begin(int_data->uart_id);
        }
        interrupt_data_base_unref(&int_data->base);
        break;
    }
#endif
    }
}
