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

#include "magnetometer-gen.h"
#include "sol-flow-internal.h"

#include <sol-i2c.h>
#include <sol-util.h>

#include <errno.h>

#define LSM303_MAG_BYTES_NUMBER 6
#define LSM303_MAG_DEFAULT_MODE 0x00
#define LSM303_MAG_REG_CRB_REG_M 0x01
#define LSM303_MAG_REG_MR_REG_M 0x02
#define LSM303_ACCEL_REG_OUT_X_H_M 0x03

struct magnetometer_lsm303_data {
    struct sol_i2c *i2c;
    double reading[3];
    uint16_t gain_xy;
    uint16_t gain_z;
    uint8_t slave;
    uint8_t scale;
};

static bool
_get_range_bits_and_gain(double range, uint8_t *range_bit, uint16_t *gain_xy, uint16_t *gain_z)
{
    if (sol_drange_val_equal(range, 1.3)) {
        *range_bit = 0x01;
        *gain_xy = 1100;
        *gain_z = 980;
    } else if (sol_drange_val_equal(range, 1.9)) {
        *range_bit = 0x02;
        *gain_xy = 855;
        *gain_z = 760;
    } else if (sol_drange_val_equal(range, 2.5)) {
        *range_bit = 0x03;
        *gain_xy = 670;
        *gain_z = 600;
    } else if (sol_drange_val_equal(range, 4.0)) {
        *range_bit = 0x04;
        *gain_xy = 450;
        *gain_z = 400;
    } else if (sol_drange_val_equal(range, 4.5)) {
        *range_bit = 0x05;
        *gain_xy = 400;
        *gain_z = 355;
    } else if (sol_drange_val_equal(range, 5.6)) {
        *range_bit = 0x06;
        *gain_xy = 330;
        *gain_z = 295;
    } else if (sol_drange_val_equal(range, 8.1)) {
        *range_bit = 0x07;
        *gain_xy = 230;
        *gain_z = 205;
    } else
        return false;

    *range_bit = *range_bit << 5;

    return true;
}

static int
magnetometer_lsm303_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    struct magnetometer_lsm303_data *mdata = data;
    const struct sol_flow_node_type_magnetometer_lsm303_options *opts;
    static const uint8_t mode = LSM303_MAG_DEFAULT_MODE;
    uint8_t range_bit;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, SOL_FLOW_NODE_TYPE_MAGNETOMETER_LSM303_OPTIONS_API_VERSION,
        -EINVAL);
    opts = (const struct sol_flow_node_type_magnetometer_lsm303_options *)options;

    if (!_get_range_bits_and_gain(opts->scale.val, &range_bit, &mdata->gain_xy, &mdata->gain_z)) {
        SOL_WRN("Invalid gain. Expected one of 1.3, 1.9, 2.5, 4.0, 4.5, 5.6 or 8.1");
        return -EINVAL;
    }

    mdata->i2c = sol_i2c_open(opts->i2c_bus.val, SOL_I2C_SPEED_10KBIT);
    if (!mdata->i2c) {
        SOL_WRN("Failed to open i2c bus");
        return -EINVAL;
    }

    if (!sol_i2c_set_slave_address(mdata->i2c, opts->i2c_slave.val)) {
        SOL_WRN("Failed to set slave at address 0x%02x\n",
            opts->i2c_slave.val);
        goto fail;
    }
    mdata->slave = opts->i2c_slave.val;

    if (!sol_i2c_write_register(mdata->i2c, LSM303_MAG_REG_MR_REG_M, &mode, 1)) {
        SOL_WRN("Could not enable LSM303 magnetometer");
        goto fail;
    }

    if (!sol_i2c_write_register(mdata->i2c, LSM303_MAG_REG_CRB_REG_M, &range_bit, 1)) {
        SOL_WRN("Could not set LSM303 magnetometer range");
        goto fail;
    }

    return 0;

fail:
    sol_i2c_close(mdata->i2c);
    return -EINVAL;
}

static void
magnetometer_lsm303_close(struct sol_flow_node *node, void *data)
{
    struct magnetometer_lsm303_data *mdata = data;

    if (mdata->i2c)
        sol_i2c_close(mdata->i2c);
}

static void
_lsm303_send_output_packets(struct sol_flow_node *node, struct magnetometer_lsm303_data *mdata)
{
    struct sol_direction_vector val =
    {
        .min = -mdata->scale,
        .max = -mdata->scale,
        .x = mdata->reading[0],
        .y = mdata->reading[1],
        .z = mdata->reading[2]
    };

    sol_flow_send_direction_vector_packet(node,
        SOL_FLOW_NODE_TYPE_MAGNETOMETER_LSM303__OUT__OUT, &val);
}

static int
magnetometer_lsm303_tick(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
    struct magnetometer_lsm303_data *mdata = data;
    int8_t buffer[LSM303_MAG_BYTES_NUMBER];
    int r;

    if (!sol_i2c_set_slave_address(mdata->i2c, mdata->slave)) {
        const char errmsg[] = "Failed to set slave at address 0x%02x";
        SOL_WRN(errmsg, mdata->slave);
        sol_flow_send_error_packet(node, EIO, errmsg, mdata->slave);
        return -EIO;
    }

    r = sol_i2c_read_register(mdata->i2c, LSM303_ACCEL_REG_OUT_X_H_M,
        (uint8_t *)buffer, sizeof(buffer));
    if (r <= 0) {
        const char errmsg[] = "Failed to read LSM303 magnetometer samples";
        SOL_WRN(errmsg);
        sol_flow_send_error_packet(node, EIO, errmsg);
        return -EIO;
    }

    /* Get X, Z and Y. That's why reading[] is indexed 0, 2 and 1. */
    mdata->reading[0] = ((buffer[0] << 8) | buffer[1]) / mdata->gain_xy;
    mdata->reading[2] = ((buffer[2] << 8) | buffer[3]) / mdata->gain_z;
    mdata->reading[1] = ((buffer[4] << 8) | buffer[5]) / mdata->gain_xy;

    _lsm303_send_output_packets(node, mdata);

    return 0;
}

#include "magnetometer-gen.c"
