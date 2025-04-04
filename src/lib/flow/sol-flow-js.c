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

#include <float.h>
#include <stdio.h>

#include "duktape.h"
#include "sol-arena.h"
#include "sol-flow-internal.h"
#include "sol-flow-js.h"
#include "sol-str-table.h"

/* Contains information specific to a type based on JS. */
struct flow_js_type {
    struct sol_flow_node_type base;

    struct sol_vector ports_in;
    struct sol_vector ports_out;

    struct sol_arena *str_arena;

    char *js_content_buf;
    size_t js_content_buf_len;
};

struct flow_js_port_in {
    struct sol_flow_port_type_in type;
    char *name;
    char *type_name;
};

struct flow_js_port_out {
    struct sol_flow_port_type_out type;
    char *name;
    char *type_name;
};

/* Contains information specific to a node of a JS node type. */
struct flow_js_data {
    /* Each node keeps its own JavaScript context and global object. */
    struct duk_context *duk_ctx;
};

enum {
    PORTS_IN_CONNECT_INDEX,
    PORTS_IN_DISCONNECT_INDEX,
    PORTS_IN_PROCESS_INDEX,
    PORTS_IN_METHODS_LENGTH,
};

enum {
    PORTS_OUT_CONNECT_INDEX,
    PORTS_OUT_DISCONNECT_INDEX,
    PORTS_OUT_METHODS_LENGTH,
};

static const char *
get_in_port_name(const struct flow_js_type *type, uint16_t port)
{
    const struct flow_js_port_in *p;

    p = sol_vector_get(&type->ports_in, port);
    if (!p) {
        SOL_WRN("Couldn't get input port %d name.", port);
        return "";
    }

    return p->name;
}

static const char *
get_out_port_name(const struct flow_js_type *type, uint16_t port)
{
    const struct flow_js_port_out *p;

    p = sol_vector_get(&type->ports_out, port);
    if (!p) {
        SOL_WRN("Couldn't get output port %d name.", port);
        return "";
    }

    return p->name;
}

static duk_ret_t
send_boolean_packet(struct sol_flow_node *node, uint16_t port, duk_context *ctx)
{
    bool value;
    int r;

    value = duk_require_boolean(ctx, 1);

    r = sol_flow_send_boolean_packet(node, port, value);
    if (r < 0) {
        duk_error(ctx, DUK_ERR_ERROR, "Couldn't send boolean packet on '%s' port.",
            get_out_port_name((struct flow_js_type *)node->type, port));
    }

    return r;
}

static duk_ret_t
send_byte_packet(struct sol_flow_node *node, uint16_t port, duk_context *ctx)
{
    unsigned char value;
    int r;

    value = duk_require_int(ctx, 1);

    r = sol_flow_send_byte_packet(node, port, value);
    if (r < 0) {
        duk_error(ctx, DUK_ERR_ERROR, "Couldn't send byte packet on '%s' port.",
            get_out_port_name((struct flow_js_type *)node->type, port));
    }

    return r;
}

static duk_ret_t
send_float_packet(struct sol_flow_node *node, uint16_t port, duk_context *ctx)
{
    struct sol_drange value;
    int r;

    if (duk_is_number(ctx, 1)) {
        value.val = duk_require_number(ctx, 1);

        value.min = -DBL_MAX;
        value.max = DBL_MAX;
        value.step = DBL_MIN;
    } else {
        duk_require_object_coercible(ctx, 1);

        duk_get_prop_string(ctx, -1, "val");
        duk_get_prop_string(ctx, -2, "min");
        duk_get_prop_string(ctx, -3, "max");
        duk_get_prop_string(ctx, -4, "step");

        value.val = duk_require_number(ctx, -4);
        value.min = duk_require_number(ctx, -3);
        value.max = duk_require_number(ctx, -2);
        value.step = duk_require_number(ctx, -1);

        duk_pop_n(ctx, 4); /* step, max, min, val values */
    }

    r = sol_flow_send_drange_packet(node, port, &value);
    if (r < 0) {
        duk_error(ctx, DUK_ERR_ERROR, "Couldn't send float packet on '%s' port.",
            get_out_port_name((struct flow_js_type *)node->type, port));
    }

    return r;
}

static duk_ret_t
send_int_packet(struct sol_flow_node *node, uint16_t port, duk_context *ctx)
{
    struct sol_irange value;
    int r;

    if (duk_is_number(ctx, 1)) {
        value.val = duk_require_int(ctx, 1);

        value.min = INT32_MIN;
        value.max = INT32_MAX;
        value.step = 1;
    } else {
        duk_require_object_coercible(ctx, 1);

        duk_get_prop_string(ctx, -1, "val");
        duk_get_prop_string(ctx, -2, "min");
        duk_get_prop_string(ctx, -3, "max");
        duk_get_prop_string(ctx, -4, "step");

        value.val = duk_require_int(ctx, -4);
        value.min = duk_require_int(ctx, -3);
        value.max = duk_require_int(ctx, -2);
        value.step = duk_require_int(ctx, -1);

        duk_pop_n(ctx, 4); /* step, max, min, val values */
    }

    r = sol_flow_send_irange_packet(node, port, &value);
    if (r < 0) {
        duk_error(ctx, DUK_ERR_ERROR, "Couldn't send int packet on '%s' port.",
            get_out_port_name((struct flow_js_type *)node->type, port));
    }

    return r;
}

static duk_ret_t
send_rgb_packet(struct sol_flow_node *node, uint16_t port, duk_context *ctx)
{
    struct sol_rgb value;
    int r;

    duk_require_object_coercible(ctx, 1);

    duk_get_prop_string(ctx, -1, "red");
    duk_get_prop_string(ctx, -2, "green");
    duk_get_prop_string(ctx, -3, "blue");
    duk_get_prop_string(ctx, -4, "red_max");
    duk_get_prop_string(ctx, -5, "green_max");
    duk_get_prop_string(ctx, -6, "blue_max");

    value.red = duk_require_int(ctx, -6);
    value.green = duk_require_int(ctx, -5);
    value.blue = duk_require_int(ctx, -4);
    value.red_max = duk_require_int(ctx, -3);
    value.green_max = duk_require_int(ctx, -2);
    value.blue_max = duk_require_int(ctx, -1);

    duk_pop_n(ctx, 6); /* blue_max, green_max, red_max, blue, green, red values */

    r = sol_flow_send_rgb_packet(node, port, &value);
    if (r < 0) {
        duk_error(ctx, DUK_ERR_ERROR, "Couldn't send rgb packet on '%s' port.",
            get_out_port_name((struct flow_js_type *)node->type, port));
    }

    return r;
}

static duk_ret_t
send_string_packet(struct sol_flow_node *node, uint16_t port, duk_context *ctx)
{
    const char *value;
    int r;

    value = duk_require_string(ctx, 1);

    r = sol_flow_send_string_packet(node, port, value);
    if (r < 0) {
        duk_error(ctx, DUK_ERR_ERROR, "Couldn't send string packet on '%s' port.",
            get_out_port_name((struct flow_js_type *)node->type, port));
    }

    return r;
}

static struct sol_flow_node *
get_node_from_duk_ctx(duk_context *ctx)
{
    struct sol_flow_node *n;

    duk_push_global_object(ctx);

    duk_get_prop_string(ctx, -1, "\xFF" "Soletta_node_pointer");
    n = duk_require_pointer(ctx, -1);

    duk_pop_2(ctx); /* Soletta_node_pointer, global object values */

    return n;
}

static int
get_output_port_number(const struct flow_js_type *type, const char *port_name)
{
    struct flow_js_port_out *p;
    uint16_t i;

    SOL_VECTOR_FOREACH_IDX (&type->ports_out, p, i) {
        if (streq(p->name, port_name))
            return i;
    }

    return -EINVAL;
}

/* sendPacket() on Javascript may throw exceptions. */
static duk_ret_t
send_packet(duk_context *ctx)
{
    const struct flow_js_port_out *port;
    const struct flow_js_type *type;
    const char *port_name;
    struct sol_flow_node *node;
    int port_number;

    port_name = duk_require_string(ctx, 0);

    node = get_node_from_duk_ctx(ctx);
    if (!node) {
        duk_error(ctx, DUK_ERR_ERROR, "Couldn't send packet to '%s' port.", port_name);
        return 0;
    }

    type = (struct flow_js_type *)node->type;
    if (!type) {
        duk_error(ctx, DUK_ERR_ERROR, "Couldn't send packet to '%s' port.", port_name);
        return 0;
    }

    /* TODO: Check a cheaper way to do this, if we had hashes we could
     * use them here. */

    port_number = get_output_port_number(type, port_name);
    if (port_number < 0) {
        duk_error(ctx, DUK_ERR_ERROR, "'%s' invalid port name.", port_name);
        return 0;
    }

    port = sol_vector_get(&type->ports_out, port_number);
    if (!port) {
        duk_error(ctx, DUK_ERR_ERROR, "'%s' invalid port name.", port_name);
        return 0;
    }

    if (port->type.packet_type == SOL_FLOW_PACKET_TYPE_BOOLEAN)
        return send_boolean_packet(node, port_number, ctx);
    if (port->type.packet_type == SOL_FLOW_PACKET_TYPE_BYTE)
        return send_byte_packet(node, port_number, ctx);
    if (port->type.packet_type == SOL_FLOW_PACKET_TYPE_DRANGE)
        return send_float_packet(node, port_number, ctx);
    if (port->type.packet_type == SOL_FLOW_PACKET_TYPE_IRANGE)
        return send_int_packet(node, port_number, ctx);
    if (port->type.packet_type == SOL_FLOW_PACKET_TYPE_RGB)
        return send_rgb_packet(node, port_number, ctx);
    if (port->type.packet_type == SOL_FLOW_PACKET_TYPE_STRING)
        return send_string_packet(node, port_number, ctx);

    /* TODO: Create a way to let the user define custom packets. Maybe we could
     * use the same techniques we do for option parsing, and provide an object
     * with an array of fields, offsets and values in basic C types. */

    duk_error(ctx, DUK_ERR_ERROR, "Couldn't handle unknown port type %s.", port->type_name);

    return 0;
}

/* sendErrorPacket() on Javascript may throw exceptions. */
static duk_ret_t
send_error_packet(duk_context *ctx)
{
    const char *value_msg = NULL;
    struct sol_flow_node *node;
    int value_code, r;

    value_code = duk_require_int(ctx, 0);

    if (duk_is_string(ctx, 1))
        value_msg = duk_require_string(ctx, 1);

    node = get_node_from_duk_ctx(ctx);
    if (!node) {
        duk_error(ctx, DUK_ERR_ERROR, "Couldn't send error packet.");
        return 0;
    }

    r = sol_flow_send_error_packet(node, value_code, value_msg);
    if (r < 0)
        duk_error(ctx, DUK_ERR_ERROR, "Couldn't send error packet.");

    return r;
}

static bool
setup_ports_in_methods(struct duk_context *duk_ctx, uint16_t ports_in_len, uint16_t base)
{
    uint16_t i;

    if (ports_in_len == 0)
        return true;

    duk_get_prop_string(duk_ctx, -1, "in");

    if (!duk_is_array(duk_ctx, -1)) {
        SOL_ERR("'in' property of object 'node' should be an array.");
        return false;
    }

    duk_push_global_stash(duk_ctx);

    for (i = 0; i < ports_in_len; i++) {
        if (!duk_get_prop_index(duk_ctx, -2, i)) {
            SOL_ERR("Couldn't get input port information from 'ports.in[%d]'.", i);
            return false;
        }

        /* This is in order to get port methods references in one call.
         *
         * We have 3 methods for each input port. We put all in the stash,
         * even with 'undefined' values, if the method is not implemented on JS.
         *
         * We calculate the index by the following:
         *
         * base + input_port_index * ports_in_methods_length + method_index
         *
         * base - where should it start, for input ports it should be 0.
         * input_port_index - the index of the JS 'in' array entry.
         * method_index - the index of the method for input ports.
         */

        duk_get_prop_string(duk_ctx, -1, "connect");
        duk_put_prop_index(duk_ctx, -3, base + i * PORTS_IN_METHODS_LENGTH + PORTS_IN_CONNECT_INDEX);

        duk_get_prop_string(duk_ctx, -1, "disconnect");
        duk_put_prop_index(duk_ctx, -3, base + i * PORTS_IN_METHODS_LENGTH + PORTS_IN_DISCONNECT_INDEX);

        duk_get_prop_string(duk_ctx, -1, "process");
        duk_put_prop_index(duk_ctx, -3, base + i * PORTS_IN_METHODS_LENGTH + PORTS_IN_PROCESS_INDEX);

        duk_pop(duk_ctx); /* array entry */
    }

    duk_pop_2(duk_ctx); /* in array and global_stash value */

    return true;
}

static bool
setup_ports_out_methods(struct duk_context *duk_ctx, uint16_t ports_out_len, uint16_t base)
{
    uint16_t i;

    if (ports_out_len == 0)
        return true;

    duk_get_prop_string(duk_ctx, -1, "out");

    if (!duk_is_array(duk_ctx, -1)) {
        SOL_ERR("'out' property of object 'node' should be an array.");
        return false;
    }

    duk_push_global_stash(duk_ctx);

    for (i = 0; i < ports_out_len; i++) {
        if (!duk_get_prop_index(duk_ctx, -2, i)) {
            SOL_ERR("Couldn't get output port information from 'ports.out[%d]'.", i);
            return false;
        }

        /* This is in order to get port methods references in one call.
         *
         * We have 2 methods for each output port. We put all in the stash,
         * even with 'undefined' values, if the method is not implemented on JS.
         *
         * We calculate the index by the following:
         *
         * base + output_port_index * ports_out_methods_length + method_index
         *
         * base - where should it start, for output ports it should be the size of input ports * number of input methods.
         * input_port_index - the index of the JS 'in' array entry.
         * method_index - the index of the method for output ports.
         */

        duk_get_prop_string(duk_ctx, -1, "connect");
        duk_put_prop_index(duk_ctx, -3, base + i * PORTS_OUT_METHODS_LENGTH + PORTS_OUT_CONNECT_INDEX);

        duk_get_prop_string(duk_ctx, -1, "disconnect");
        duk_put_prop_index(duk_ctx, -3, base + i * PORTS_OUT_METHODS_LENGTH + PORTS_OUT_DISCONNECT_INDEX);

        duk_pop(duk_ctx); /* array entry */
    }

    duk_pop_2(duk_ctx); /* out array and global_stash value */

    return true;
}

static bool
setup_ports_methods(duk_context *duk_ctx, uint16_t ports_in_len, uint16_t ports_out_len)
{
    /* We're using duktape global stash to keep reference to some JS
     * port methods: connect(), disconnect() and process() in order
     * to call it directly when receive a port number.
     */

    if (!setup_ports_in_methods(duk_ctx, ports_in_len, 0))
        return false;

    if (!setup_ports_out_methods(duk_ctx, ports_out_len, ports_in_len * PORTS_IN_METHODS_LENGTH))
        return false;

    return true;
}

/* open() method on JS may throw exceptions. */
static int
flow_js_open(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
    const struct flow_js_type *type = (struct flow_js_type *)node->type;
    struct flow_js_data *mdata = data;

    mdata->duk_ctx = duk_create_heap_default();
    if (!mdata->duk_ctx) {
        SOL_ERR("Failed to create a Duktape heap");
        return -1;
    }

    /* TODO: Check if there's a "already parsed" representation that we can use. */
    if (duk_peval_lstring(mdata->duk_ctx, type->js_content_buf, type->js_content_buf_len) != 0) {
        SOL_ERR("Failed to read from javascript content buffer: %s", duk_safe_to_string(mdata->duk_ctx, -1));
        duk_destroy_heap(mdata->duk_ctx);
        return -1;
    }
    duk_pop(mdata->duk_ctx); /* duk_peval_lstring() result */

    duk_push_global_object(mdata->duk_ctx);

    /* "Soletta_node_pointer" is a hidden property. \xFF is used to give one extra level of hiding */
    duk_push_string(mdata->duk_ctx, "\xFF" "Soletta_node_pointer");
    duk_push_pointer(mdata->duk_ctx, node);
    duk_def_prop(mdata->duk_ctx, -3,
        DUK_DEFPROP_HAVE_VALUE |
        DUK_DEFPROP_HAVE_WRITABLE |
        DUK_DEFPROP_HAVE_ENUMERABLE |
        DUK_DEFPROP_HAVE_CONFIGURABLE);

    duk_push_c_function(mdata->duk_ctx, send_packet, 2);
    duk_put_prop_string(mdata->duk_ctx, -2, "sendPacket");

    duk_push_c_function(mdata->duk_ctx, send_error_packet, 2);
    duk_put_prop_string(mdata->duk_ctx, -2, "sendErrorPacket");

    /* From this point node JS object is always in the top of the stack. */
    duk_get_prop_string(mdata->duk_ctx, -1, "node");

    if (!setup_ports_methods(mdata->duk_ctx, type->ports_in.len, type->ports_out.len)) {
        SOL_ERR("Failed to handle ports methods: %s", duk_safe_to_string(mdata->duk_ctx, -1));
        duk_destroy_heap(mdata->duk_ctx);
        return -1;
    }

    if (!duk_has_prop_string(mdata->duk_ctx, -1, "open"))
        return 0;

    duk_push_string(mdata->duk_ctx, "open");
    if (duk_pcall_prop(mdata->duk_ctx, -2, 0) != DUK_EXEC_SUCCESS) {
        duk_error(mdata->duk_ctx, DUK_ERR_ERROR, "Javascript open() function error: %s\n",
            duk_safe_to_string(mdata->duk_ctx, -1));
    }

    duk_pop(mdata->duk_ctx); /* open() result */

    return 0;
}

/* close() method on JS may throw exceptions. */
static void
flow_js_close(struct sol_flow_node *node, void *data)
{
    const struct flow_js_data *mdata = (struct flow_js_data *)data;

    if (duk_has_prop_string(mdata->duk_ctx, -1, "close")) {
        duk_push_string(mdata->duk_ctx, "close");

        if (duk_pcall_prop(mdata->duk_ctx, -2, 0) != DUK_EXEC_SUCCESS) {
            duk_error(mdata->duk_ctx, DUK_ERR_ERROR, "Javascript close() function error: %s\n",
                duk_safe_to_string(mdata->duk_ctx, -1));
        }

        duk_pop(mdata->duk_ctx); /* close() result */
    }

    duk_destroy_heap(mdata->duk_ctx);
}

static int
process_boilerplate_pre(duk_context *ctx, struct sol_flow_node *node, uint16_t port)
{
    duk_push_global_stash(ctx);

    if (!duk_get_prop_index(ctx, -1, port * PORTS_IN_METHODS_LENGTH + PORTS_IN_PROCESS_INDEX)) {
        SOL_ERR("Couldn't handle '%s' process().", get_in_port_name((struct flow_js_type *)node->type, port));
        duk_pop_2(ctx); /* get_prop() value and global_stash */
        return -1;
    }

    if (duk_is_null_or_undefined(ctx, -1)) {
        SOL_WRN("'%s' process() callback not implemented in javascript, ignoring incoming packets for this port",
            get_in_port_name((struct flow_js_type *)node->type, port));
        duk_pop_2(ctx); /* get_prop() value and global_stash */
        return 0;
    }

    /* In order to use 'node' object as 'this' binding. */
    duk_dup(ctx, -3);

    return 1;
}

static int
process_boilerplate_post(duk_context *ctx, struct sol_flow_node *node, uint16_t port, uint16_t js_method_nargs)
{
    if (duk_pcall_method(ctx, js_method_nargs) != DUK_EXEC_SUCCESS) {
        duk_error(ctx, DUK_ERR_ERROR, "Javascript %s process() function error: %s\n",
            get_in_port_name((struct flow_js_type *)node->type, port), duk_safe_to_string(ctx, -1));
        duk_pop_2(ctx); /* process() result and global_stash */
        return -1;
    }

    duk_pop_2(ctx); /* process() result and global_stash */

    return 0;
}

static int
boolean_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    const struct flow_js_data *mdata = (struct flow_js_data *)data;
    bool value;
    int r;

    r = sol_flow_packet_get_boolean(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    r = process_boilerplate_pre(mdata->duk_ctx, node, port);
    SOL_INT_CHECK(r, <= 0, r);

    duk_push_boolean(mdata->duk_ctx, value);

    return process_boilerplate_post(mdata->duk_ctx, node, port, 1);
}

static int
byte_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    const struct flow_js_data *mdata = (struct flow_js_data *)data;
    unsigned char value;
    int r;

    r = sol_flow_packet_get_byte(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    r = process_boilerplate_pre(mdata->duk_ctx, node, port);
    SOL_INT_CHECK(r, <= 0, r);

    duk_push_int(mdata->duk_ctx, value);

    return process_boilerplate_post(mdata->duk_ctx, node, port, 1);
}

static int
error_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    const struct flow_js_data *mdata = (struct flow_js_data *)data;
    const char *value_msg;
    int r, value_code;

    r = sol_flow_packet_get_error(packet, &value_code, &value_msg);
    SOL_INT_CHECK(r, < 0, r);

    r = process_boilerplate_pre(mdata->duk_ctx, node, port);
    SOL_INT_CHECK(r, <= 0, r);

    duk_push_int(mdata->duk_ctx, value_code);
    duk_push_string(mdata->duk_ctx, value_msg);

    return process_boilerplate_post(mdata->duk_ctx, node, port, 2);
}

static int
float_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    const struct flow_js_data *mdata = (struct flow_js_data *)data;
    struct sol_drange value;
    duk_idx_t obj_idx;
    int r;

    r = sol_flow_packet_get_drange(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    r = process_boilerplate_pre(mdata->duk_ctx, node, port);
    SOL_INT_CHECK(r, <= 0, r);

    obj_idx = duk_push_object(mdata->duk_ctx);
    duk_push_number(mdata->duk_ctx, value.val);
    duk_put_prop_string(mdata->duk_ctx, obj_idx, "val");
    duk_push_number(mdata->duk_ctx, value.min);
    duk_put_prop_string(mdata->duk_ctx, obj_idx, "min");
    duk_push_number(mdata->duk_ctx, value.max);
    duk_put_prop_string(mdata->duk_ctx, obj_idx, "max");
    duk_push_number(mdata->duk_ctx, value.step);
    duk_put_prop_string(mdata->duk_ctx, obj_idx, "step");

    return process_boilerplate_post(mdata->duk_ctx, node, port, 1);
}

static int
int_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    const struct flow_js_data *mdata = (struct flow_js_data *)data;
    struct sol_irange value;
    duk_idx_t obj_idx;
    int r;

    r = sol_flow_packet_get_irange(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    r = process_boilerplate_pre(mdata->duk_ctx, node, port);
    SOL_INT_CHECK(r, <= 0, r);

    obj_idx = duk_push_object(mdata->duk_ctx);
    duk_push_int(mdata->duk_ctx, value.val);
    duk_put_prop_string(mdata->duk_ctx, obj_idx, "val");
    duk_push_int(mdata->duk_ctx, value.min);
    duk_put_prop_string(mdata->duk_ctx, obj_idx, "min");
    duk_push_int(mdata->duk_ctx, value.max);
    duk_put_prop_string(mdata->duk_ctx, obj_idx, "max");
    duk_push_int(mdata->duk_ctx, value.step);
    duk_put_prop_string(mdata->duk_ctx, obj_idx, "step");

    return process_boilerplate_post(mdata->duk_ctx, node, port, 1);
}

static int
rgb_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    const struct flow_js_data *mdata = (struct flow_js_data *)data;
    struct sol_rgb value;
    duk_idx_t obj_idx;
    int r;

    r = sol_flow_packet_get_rgb(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    r = process_boilerplate_pre(mdata->duk_ctx, node, port);
    SOL_INT_CHECK(r, <= 0, r);

    obj_idx = duk_push_object(mdata->duk_ctx);
    duk_push_int(mdata->duk_ctx, value.red);
    duk_put_prop_string(mdata->duk_ctx, obj_idx, "red");
    duk_push_int(mdata->duk_ctx, value.green);
    duk_put_prop_string(mdata->duk_ctx, obj_idx, "green");
    duk_push_int(mdata->duk_ctx, value.blue);
    duk_put_prop_string(mdata->duk_ctx, obj_idx, "blue");
    duk_push_int(mdata->duk_ctx, value.red_max);
    duk_put_prop_string(mdata->duk_ctx, obj_idx, "red_max");
    duk_push_int(mdata->duk_ctx, value.green_max);
    duk_put_prop_string(mdata->duk_ctx, obj_idx, "green_max");
    duk_push_int(mdata->duk_ctx, value.blue_max);
    duk_put_prop_string(mdata->duk_ctx, obj_idx, "blue_max");

    return process_boilerplate_post(mdata->duk_ctx, node, port, 1);
}

static int
string_process(struct sol_flow_node *node, void *data, uint16_t port,
    uint16_t conn_id, const struct sol_flow_packet *packet)
{
    const struct flow_js_data *mdata = (struct flow_js_data *)data;
    const char *value;
    int r;

    r = sol_flow_packet_get_string(packet, &value);
    SOL_INT_CHECK(r, < 0, r);

    r = process_boilerplate_pre(mdata->duk_ctx, node, port);
    SOL_INT_CHECK(r, <= 0, r);

    duk_push_string(mdata->duk_ctx, value);

    return process_boilerplate_post(mdata->duk_ctx, node, port, 1);
}

/* process() methods on JS may throw exceptions. */
static int
flow_js_port_process(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id,
    const struct sol_flow_packet *packet)
{
    const struct sol_flow_packet_type *packet_type;

    packet_type = sol_flow_packet_get_type(packet);
    if (packet_type == SOL_FLOW_PACKET_TYPE_BOOLEAN)
        return boolean_process(node, data, port, conn_id, packet);
    if (packet_type == SOL_FLOW_PACKET_TYPE_BYTE)
        return byte_process(node, data, port, conn_id, packet);
    if (packet_type == SOL_FLOW_PACKET_TYPE_ERROR)
        return error_process(node, data, port, conn_id, packet);
    if (packet_type == SOL_FLOW_PACKET_TYPE_DRANGE)
        return float_process(node, data, port, conn_id, packet);
    if (packet_type == SOL_FLOW_PACKET_TYPE_IRANGE)
        return int_process(node, data, port, conn_id, packet);
    if (packet_type == SOL_FLOW_PACKET_TYPE_RGB)
        return rgb_process(node, data, port, conn_id, packet);
    if (packet_type == SOL_FLOW_PACKET_TYPE_STRING)
        return string_process(node, data, port, conn_id, packet);

    return 0;
}

/* connect() and disconnect() port methods on JS may throw exceptions. */
static int
handle_js_port_activity(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id,
    uint16_t base, uint16_t methods_length, uint16_t method_index)
{
    const struct flow_js_data *mdata = (struct flow_js_data *)data;

    duk_push_global_stash(mdata->duk_ctx);

    if (!duk_get_prop_index(mdata->duk_ctx, -1, base + port * methods_length + method_index)) {
        duk_error(mdata->duk_ctx, DUK_ERR_ERROR, "Couldn't handle '%s' %s().",
            get_in_port_name((struct flow_js_type *)node->type, port),
            method_index == PORTS_IN_CONNECT_INDEX ? "connect" : "disconnect");
        duk_pop_2(mdata->duk_ctx); /* get_prop() value and global_stash */
        return -1;
    }

    if (duk_is_null_or_undefined(mdata->duk_ctx, -1)) {
        duk_pop_2(mdata->duk_ctx); /* get_prop() value and global_stash */
        return 0;
    }

    if (duk_pcall(mdata->duk_ctx, 0) != DUK_EXEC_SUCCESS) {
        duk_error(mdata->duk_ctx, DUK_ERR_ERROR, "Javascript function error: %s\n",
            duk_safe_to_string(mdata->duk_ctx, -1));
        duk_pop_2(mdata->duk_ctx); /* method() result and global_stash */
        return -1;
    }

    duk_pop_2(mdata->duk_ctx); /* method() result and global_stash */

    return 0;
}

static int
flow_js_port_in_connect(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id)
{
    return handle_js_port_activity(node, data, port, conn_id, 0, PORTS_IN_METHODS_LENGTH, PORTS_IN_CONNECT_INDEX);
}

static int
flow_js_port_in_disconnect(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id)
{
    return handle_js_port_activity(node, data, port, conn_id, 0, PORTS_IN_METHODS_LENGTH, PORTS_IN_DISCONNECT_INDEX);
}

static int
flow_js_port_out_connect(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id)
{
    const struct flow_js_type *type = (struct flow_js_type *)node->type;

    return handle_js_port_activity(node, data, port, conn_id,
        type->ports_in.len * PORTS_IN_METHODS_LENGTH, PORTS_OUT_METHODS_LENGTH, PORTS_OUT_CONNECT_INDEX);
}

static int
flow_js_port_out_disconnect(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id)
{
    const struct flow_js_type *type = (struct flow_js_type *)node->type;

    return handle_js_port_activity(node, data, port, conn_id,
        type->ports_in.len * PORTS_IN_METHODS_LENGTH, PORTS_OUT_METHODS_LENGTH, PORTS_OUT_DISCONNECT_INDEX);
}

static void
flow_js_get_ports_counts(const struct sol_flow_node_type *type, uint16_t *ports_in_count, uint16_t *ports_out_count)
{
    const struct flow_js_type *js_type = (struct flow_js_type *)type;

    if (ports_in_count)
        *ports_in_count = js_type->ports_in.len;
    if (ports_out_count)
        *ports_out_count = js_type->ports_out.len;
}

static const struct sol_flow_port_type_in *
flow_js_get_port_in(const struct sol_flow_node_type *type, uint16_t port)
{
    const struct flow_js_type *js_type = (struct flow_js_type *)type;
    const struct flow_js_port_in *p = sol_vector_get(&js_type->ports_in, port);

    return p ? &p->type : NULL;
}

static const struct sol_flow_port_type_out *
flow_js_get_port_out(const struct sol_flow_node_type *type, uint16_t port)
{
    const struct flow_js_type *js_type = (struct flow_js_type *)type;
    const struct flow_js_port_out *p = sol_vector_get(&js_type->ports_out, port);

    return p ? &p->type : NULL;
}

#ifdef SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED
static const struct sol_flow_node_type_description sol_flow_node_type_js_description = {
    .api_version = SOL_FLOW_NODE_TYPE_DESCRIPTION_API_VERSION,
    .name = "js",
    .category = "js",
    .symbol = "SOL_FLOW_NODE_TYPE_JS",
    .options_symbol = "sol_flow_node_type_js_options",
    .version = NULL,
    /* TODO: Add a way for the user specify description, author, url and license. */
};

static int
setup_description(struct flow_js_type *type)
{
    struct sol_flow_node_type_description *desc;
    struct sol_flow_port_description **p;
    struct flow_js_port_in *port_type_in;
    struct flow_js_port_out *port_type_out;
    int i, j;

    type->base.description = calloc(1, sizeof(struct sol_flow_node_type_description));
    SOL_NULL_CHECK(type->base.description, -ENOMEM);

    type->base.description = memcpy((struct sol_flow_node_type_description *)type->base.description,
        &sol_flow_node_type_js_description, sizeof(struct sol_flow_node_type_description));

    desc = (struct sol_flow_node_type_description *)type->base.description;

    /* Extra slot for NULL sentinel at the end. */
    desc->ports_in = calloc(type->ports_in.len + 1, sizeof(struct sol_flow_port_description *));
    SOL_NULL_CHECK_GOTO(desc->ports_in, fail_ports_in);

    p = (struct sol_flow_port_description **)desc->ports_in;
    for (i = 0; i < type->ports_in.len; i++) {
        p[i] = calloc(1, sizeof(struct sol_flow_port_description));
        SOL_NULL_CHECK_GOTO(p[i], fail_ports_in_desc);

        port_type_in = sol_vector_get(&type->ports_in, i);

        p[i]->name = port_type_in->name;
        p[i]->description = "Input port";
        p[i]->data_type = port_type_in->type_name;
        p[i]->array_size = 0;
        p[i]->base_port_idx = i;
        p[i]->required = false;
    }

    /* Extra slot for NULL sentinel at the end. */
    desc->ports_out = calloc(type->ports_out.len + 1, sizeof(struct sol_flow_port_description *));
    SOL_NULL_CHECK_GOTO(desc->ports_out, fail_ports_in_desc);

    p = (struct sol_flow_port_description **)desc->ports_out;
    for (j = 0; j < type->ports_out.len; j++) {
        p[j] = calloc(1, sizeof(struct sol_flow_port_description));
        SOL_NULL_CHECK_GOTO(p[j], fail_ports_out_desc);

        port_type_out = sol_vector_get(&type->ports_out, j);

        p[j]->name = port_type_out->name;
        p[j]->description = "Output port";
        p[j]->data_type = port_type_out->type_name;
        p[j]->array_size = 0;
        p[j]->base_port_idx = j;
        p[j]->required = false;
    }

    return 0;

fail_ports_out_desc:
    if (j > 0) {
        for (; j >= 0; j--) {
            free((struct sol_flow_port_description *)desc->ports_out[j]);
        }
    }
    free((struct sol_flow_port_description **)desc->ports_out);
fail_ports_in_desc:
    if (i > 0) {
        for (; i >= 0; i--) {
            free((struct sol_flow_port_description *)desc->ports_in[i]);
        }
    }
    free((struct sol_flow_port_description **)desc->ports_in);
fail_ports_in:
    free(desc);
    return -ENOMEM;
}

static void
free_description(struct flow_js_type *type)
{
    struct sol_flow_node_type_description *desc;
    uint16_t i;

    desc = (struct sol_flow_node_type_description *)type->base.description;

    for (i = 0; i < type->ports_in.len; i++)
        free((struct sol_flow_port_description *)desc->ports_in[i]);
    free((struct sol_flow_port_description **)desc->ports_in);

    for (i = 0; i < type->ports_out.len; i++)
        free((struct sol_flow_port_description *)desc->ports_out[i]);
    free((struct sol_flow_port_description **)desc->ports_out);

    free(desc);
}
#endif

static const struct sol_flow_packet_type *
get_packet_type(const char *type)
{
    /* We're using 'if statements' instead of 'sol_str_table_ptr' because we couldn't create the table
     * as 'const static' since the packet types are declared in another file (found only in linkage time),
     * and creating the table all the time would give us a bigger overhead than 'if statements' */

    if (!strcasecmp(type, "boolean"))
        return SOL_FLOW_PACKET_TYPE_BOOLEAN;
    if (!strcasecmp(type, "byte"))
        return SOL_FLOW_PACKET_TYPE_BYTE;
    if (!strcasecmp(type, "drange") || !strcasecmp(type, "float"))
        return SOL_FLOW_PACKET_TYPE_DRANGE;
    if (!strcasecmp(type, "error"))
        return SOL_FLOW_PACKET_TYPE_ERROR;
    if (!strcasecmp(type, "irange") || !strcasecmp(type, "int"))
        return SOL_FLOW_PACKET_TYPE_IRANGE;
    if (!strcasecmp(type, "rgb"))
        return SOL_FLOW_PACKET_TYPE_RGB;
    if (!strcasecmp(type, "string"))
        return SOL_FLOW_PACKET_TYPE_STRING;

    return NULL;
}

static bool
setup_ports_in(struct duk_context *duk_ctx, struct sol_arena *str_arena, struct sol_vector *ports_in)
{
    const char *name, *type_name;
    const struct sol_flow_packet_type *packet_type;
    struct flow_js_port_in *port_type;
    uint16_t array_len, i;

    if (!duk_has_prop_string(duk_ctx, -1, "in"))
        return true;

    duk_get_prop_string(duk_ctx, -1, "in");

    if (!duk_is_array(duk_ctx, -1)) {
        SOL_ERR("'in' property of variable 'ports' should be an array.");
        return false;
    }

    if (!duk_get_prop_string(duk_ctx, -1, "length")) {
        SOL_ERR("Couldn't get 'in' length from 'ports' variable.");
        return false;
    }

    array_len = duk_require_int(duk_ctx, -1);
    duk_pop(duk_ctx); /* length value */

    if (array_len == 0)
        return true;

    sol_vector_init(ports_in, sizeof(struct flow_js_port_in));

    for (i = 0; i < array_len; i++) {
        if (!duk_get_prop_index(duk_ctx, -1, i)) {
            SOL_WRN("Couldn't get input port information from 'ports.in[%d]', ignoring this port creation...", i);
            duk_pop(duk_ctx);
            continue;
        }

        if (!duk_get_prop_string(duk_ctx, -1, "name")) {
            SOL_WRN("Input port 'name' property is missing on 'ports.in[%d]', ignoring this port creation... "
                "e.g. '{ name:'IN', type:'boolean' }'", i);
            duk_pop_2(duk_ctx);
            continue;
        }

        if (!duk_get_prop_string(duk_ctx, -2, "type")) {
            SOL_WRN("Input port 'type' property is missing on 'ports.in[%d]', ignoring this port creation... "
                "e.g. '{ name:'IN', type:'boolean' }'", i);
            duk_pop_3(duk_ctx);
            continue;
        }

        name = duk_require_string(duk_ctx, -2);
        type_name = duk_require_string(duk_ctx, -1);

        packet_type = get_packet_type(type_name);
        if (!packet_type) {
            SOL_WRN("Input port type '%s' is an invalid packet type on 'ports.in[%d]', ignoring this port creation...", type_name, i);
            duk_pop_3(duk_ctx);
            continue;
        }

        port_type = sol_vector_append(ports_in);
        SOL_NULL_CHECK(port_type, false);

        port_type->type.api_version = SOL_FLOW_PORT_TYPE_IN_API_VERSION;
        port_type->type.packet_type = packet_type;
        port_type->type.process = flow_js_port_process;
        port_type->type.connect = flow_js_port_in_connect;
        port_type->type.disconnect = flow_js_port_in_disconnect;

        port_type->name = sol_arena_strdup(str_arena, name);
        SOL_NULL_CHECK(port_type->name, false);

        port_type->type_name = sol_arena_strdup(str_arena, type_name);
        SOL_NULL_CHECK(port_type->type_name, false);

        duk_pop_3(duk_ctx);
    }

    duk_pop(duk_ctx); /* in value */

    return true;
}

static bool
setup_ports_out(struct duk_context *duk_ctx, struct sol_arena *str_arena, struct sol_vector *ports_out)
{
    const char *name, *type_name;
    const struct sol_flow_packet_type *packet_type;
    struct flow_js_port_out *port_type;
    uint16_t array_len, i;

    if (!duk_has_prop_string(duk_ctx, -1, "out"))
        return true;

    duk_get_prop_string(duk_ctx, -1, "out");

    if (!duk_is_array(duk_ctx, -1)) {
        SOL_ERR("'out' property of variable 'ports' should be an array.");
        return false;
    }

    if (!duk_get_prop_string(duk_ctx, -1, "length")) {
        SOL_ERR("Couldn't get 'out' length from 'ports' variable.");
        return false;
    }

    array_len = duk_require_int(duk_ctx, -1);
    duk_pop(duk_ctx); /* length value */

    if (array_len == 0)
        return true;

    sol_vector_init(ports_out, sizeof(struct flow_js_port_out));

    for (i = 0; i < array_len; i++) {
        if (!duk_get_prop_index(duk_ctx, -1, i)) {
            SOL_WRN("Couldn't get output port information from 'ports.out[%d]', ignoring this port creation...", i);
            duk_pop(duk_ctx);
            continue;
        }

        if (!duk_get_prop_string(duk_ctx, -1, "name")) {
            SOL_WRN("Output port 'name' property is missing on 'ports.out[%d]', ignoring this port creation... "
                "e.g. '{ name:'OUT', type:'boolean' }'", i);
            duk_pop_2(duk_ctx);
            continue;
        }

        if (!duk_get_prop_string(duk_ctx, -2, "type")) {
            SOL_WRN("Output port 'type' property is missing on 'ports.out[%d]', ignoring this port creation... "
                "e.g. '{ name:'OUT', type:'boolean' }'", i);
            duk_pop_3(duk_ctx);
            continue;
        }

        name = duk_require_string(duk_ctx, -2);
        type_name = duk_require_string(duk_ctx, -1);

        packet_type = get_packet_type(type_name);
        if (!packet_type) {
            SOL_WRN("Output port type '%s' is an invalid packet type on 'ports.out[%d]', ignoring this port creation...", type_name, i);
            duk_pop_3(duk_ctx);
            continue;
        }

        port_type = sol_vector_append(ports_out);
        SOL_NULL_CHECK(port_type, false);

        port_type->type.api_version = SOL_FLOW_PORT_TYPE_OUT_API_VERSION;
        port_type->type.packet_type = packet_type;
        port_type->type.connect = flow_js_port_out_connect;
        port_type->type.disconnect = flow_js_port_out_disconnect;

        port_type->name = sol_arena_strdup(str_arena, name);
        SOL_NULL_CHECK(port_type->name, false);

        port_type->type_name = sol_arena_strdup(str_arena, type_name);
        SOL_NULL_CHECK(port_type->type_name, false);

        duk_pop_3(duk_ctx);
    }

    duk_pop(duk_ctx); /* out value */

    return true;
}

static bool
setup_ports(struct flow_js_type *type, const char *buf, size_t len)
{
    struct duk_context *duk_ctx;

    duk_ctx = duk_create_heap_default();
    if (!duk_ctx) {
        SOL_ERR("Failed to create a Duktape heap");
        return false;
    }

    if (duk_peval_lstring(duk_ctx, buf, len) != 0) {
        SOL_ERR("Failed to parse javascript content: %s", duk_safe_to_string(duk_ctx, -1));
        duk_destroy_heap(duk_ctx);
        return false;
    }
    duk_pop(duk_ctx); /* duk_peval_lstring() result */

    duk_push_global_object(duk_ctx);

    if (!duk_get_prop_string(duk_ctx, -1, "node")) {
        SOL_ERR("'node' variable not found in javascript file.");
        duk_destroy_heap(duk_ctx);
        return false;
    }

    type->str_arena = sol_arena_new();
    if (!type->str_arena) {
        SOL_ERR("Couldn't create sol_arena.");
        duk_destroy_heap(duk_ctx);
        return false;
    }

    if (!setup_ports_in(duk_ctx, type->str_arena, &type->ports_in)) {
        duk_destroy_heap(duk_ctx);
        return false;
    }

    if (!setup_ports_out(duk_ctx, type->str_arena, &type->ports_out)) {
        duk_destroy_heap(duk_ctx);
        return false;
    }

    duk_destroy_heap(duk_ctx);
    return true;
}

static void
flow_js_type_fini(struct flow_js_type *type)
{
#ifdef SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED
    if (type->base.description)
        free_description(type);
#endif

    if (type->str_arena)
        sol_arena_del(type->str_arena);

    sol_vector_clear(&type->ports_in);
    sol_vector_clear(&type->ports_out);

    free(type->js_content_buf);
}

static void
flow_dispose_type(struct sol_flow_node_type *type)
{
    struct flow_js_type *js_type;

    SOL_NULL_CHECK(type);

    js_type = (struct flow_js_type *)type;
    flow_js_type_fini(js_type);
    free(js_type);
}

static bool
flow_js_type_init(struct flow_js_type *type, const char *buf, size_t len)
{
    char *js_content_buf;

    *type = (const struct flow_js_type) {
        .base = {
            .api_version = SOL_FLOW_NODE_TYPE_API_VERSION,
            .data_size = sizeof(struct flow_js_data),
            .open = flow_js_open,
            .close = flow_js_close,
            .get_ports_counts = flow_js_get_ports_counts,
            .get_port_in = flow_js_get_port_in,
            .get_port_out = flow_js_get_port_out,
            .dispose_type = flow_dispose_type,
        },
    };

    if (!setup_ports(type, buf, len))
        return false;

    js_content_buf = strndup(buf, len);
    SOL_NULL_CHECK(js_content_buf, false);

    type->js_content_buf = js_content_buf;
    type->js_content_buf_len = len;

#ifdef SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED
    if (setup_description(type) < 0)
        SOL_WRN("Failed to setup description");
#endif

    return true;
}

SOL_API struct sol_flow_node_type *
sol_flow_js_new_type(const char *buf, size_t len)
{
    struct flow_js_type *type;

    type = calloc(1, sizeof(struct flow_js_type));
    SOL_NULL_CHECK(type, NULL);

    if (!flow_js_type_init(type, buf, len)) {
        flow_js_type_fini(type);
        free(type);
        return NULL;
    }

    return &type->base;
}
