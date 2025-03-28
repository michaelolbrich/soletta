#!/usr/bin/env python3

# This file is part of the Soletta Project
#
# Copyright (C) 2015 Intel Corporation. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
#   * Redistributions of source code must retain the above copyright
#     notice, this list of conditions and the following disclaimer.
#   * Redistributions in binary form must reproduce the above copyright
#     notice, this list of conditions and the following disclaimer in
#     the documentation and/or other materials provided with the
#     distribution.
#   * Neither the name of Intel Corporation nor the names of its
#     contributors may be used to endorse or promote products derived
#     from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import json
import os
import re


class StubError(Exception):
    pass


def c_clean(string):
    return re.sub('[^A-Za-z0-9_]', '_', string)

def is_custom(typename):
    return typename[:7] == "custom:"

def custom_get_name(typename):
    return c_clean(typename[7:].lower())

data_type_to_c_map = {
    "boolean": "bool ",
    "blob": "struct sol_blob *",
    "byte": "unsigned char ",
    "int": "struct sol_irange ",
    "float": "struct sol_drange ",
    "rgb": "struct sol_rgb ",
    "direction-vector": "struct sol_direction_vector ",
    "string": "const char *",
    "error": "int code_value; const char *",
    }
def data_type_to_c(typename):
    if (is_custom(typename)):
        cname = custom_get_name(typename)
        return "struct " + cname + "_packet_data "
    return data_type_to_c_map[typename]

def load_json(infile, prefix):
    data = json.load(infile)
    # TODO: validate data

    prefix_c = ""
    if prefix:
        prefix_c = c_clean(prefix)

    name_c = c_clean(data["name"].lower())
    if prefix_c:
        name_c = prefix_c + "_" + name_c

    data["name_c"] = name_c
    data["NAME_C"] = name_c.upper()
    data["prefix_c"] = prefix_c
    return data


def find_methods(port_methods, data_type, method_type, methods):
    method_name = port_methods.get(method_type)
    if method_name:
        method = methods.get(method_name, {})
        if data_type:
            method[data_type] = True
        else:
            method["any"] = True
        methods[method_name] = method


def get_single_packet_type(packets):
    if "any" in packets or "empty" in packets:
        return None;
    if len(packets) > 1:
        return None;
    return list(packets.keys())[0]

def print_data_struct(outfile, struct):
    if struct:
        outfile.write("""\
    struct %s *mdata = data;
""" % struct)

data_type_to_packet_getter_map = {
    "boolean": "boolean(packet, &in_value)",
    "blob": "blob(packet, &in_value)",
    "int": "irange(packet, &in_value)",
    "float": "drange(packet, &in_value)",
    "string": "string(packet, &in_value)",
    "byte": "byte(packet, &in_value)",
    "error": "error(packet, &code_value, &in_value)",
    "rgb": "rgb(packet, &in_value)",
    "direction-vector": "direction_vector(packet, &in_value)",
    }
def data_type_to_packet_getter(typename):
    if (is_custom(typename)):
        return "packet_get_" + custom_get_name(typename) + \
               "(packet /* TODO: add args */)"
    return "sol_flow_packet_get_" + data_type_to_packet_getter_map[typename]


def get_base_name(base_name):
    if base_name[-5:] != '.json':
        raise StubError("Description files not using .json extension '%s'." %
                        base_name)
    base_name = base_name[:-5]
    return os.path.basename(base_name)


def include_source(outfile, base_name):
    outfile.write("""\
#include "%s"
""" % (base_name + '-gen.c'))


def include_header(outfile, base_name):
    outfile.write("""\
#include "%s"
""" % (base_name + '-gen.h'))


def add_empty_line(outfile):
    outfile.write("""\

""")

def license_header(outfile):
    outfile.write("""\
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

""")


def include_common_headers(outfile):
    outfile.write("""\
#include "sol-flow-internal.h"

#include <sol-util.h>
#include <errno.h>

""")


def declare_packet(outfile, port, packets):
    data_type = port.get("data_type");
    if (is_custom(data_type)):
        packet_name = custom_get_name(data_type)
        if packet_name not in packets:
            packets.append(packet_name)
            outfile.write("""\
struct %(name_data)s {
    /* TODO: add packet struct fields */
};

static void
%(name)s_packet_dispose(const struct sol_flow_packet_type *packet_type,
                        void *mem)
{
    struct %(name_data)s *%(name)s = mem;
    /* TODO: free fields alloced memory */
}

static int
%(name)s_packet_init(const struct sol_flow_packet_type *packet_type,
                     void *mem, const void *input)
{
    const struct %(name_data)s *in = input;
    struct %(name_data)s *%(name)s = mem;

    /* TODO: initialize fields with input content */

    return 0;
}

#define %(NAME)s_PACKET_TYPE_API_VERSION (1)

static const struct sol_flow_packet_type _%(NAME)s = {
    .api_version = %(NAME)s_PACKET_TYPE_API_VERSION,
    .name = "%(NAME)s",
    .data_size = sizeof(struct %(name_data)s),
    .init = %(name)s_packet_init,
    .dispose = %(name)s_packet_dispose,
};
static const struct sol_flow_packet_type *%(NAME)s =
    &_%(NAME)s;

#undef %(NAME)s_PACKET_TYPE_API_VERSION

static struct sol_flow_packet *
packet_new_%(name)s(/* TODO: args to fill fields */)
{
    struct %(name_data)s %(name)s;

    /* TODO: check for args validity and fill fields */

    return sol_flow_packet_new(%(NAME)s, &%(name)s);
}

static int
packet_get_%(name)s(const struct sol_flow_packet *packet
                    /* TODO: args to get fields values */)
{
    struct %(name_data)s %(name)s;
    int ret;

    SOL_NULL_CHECK(packet, -EINVAL);
    if (sol_flow_packet_get_type(packet) != %(NAME)s)
        return -EINVAL;

    ret = sol_flow_packet_get(packet, &%(name)s);
    SOL_INT_CHECK(ret, != 0, ret);

    /* TODO: set args with fields values */

    return ret;
}

static int
send_%(name)s_packet(struct sol_flow_node *src, uint16_t src_port
                     /* TODO: args to create a new packet */)
{
    struct sol_flow_packet *packet;
    int ret;

    packet = packet_new_%(name)s(/* TODO: args */);
    SOL_NULL_CHECK(packet, -ENOMEM);

    ret = sol_flow_send_packet(src, src_port, packet);
    if (ret != 0)
        sol_flow_packet_del(packet);

    return ret;
}

""" % {
    "name": packet_name,
    "NAME": packet_name.upper(),
    "name_data": packet_name + "_packet_data"
    })


def declare_packets(outfile, data, packets):
    if "in_ports" in data:
        for port in data["in_ports"]:
            declare_packet(outfile, port, packets)
    if "out_ports" in data:
        for port in data["out_ports"]:
            declare_packet(outfile, port, packets)


def declare_structs(outfile, data, structs):
    # declare empty struct
    struct = data.get("private_data_type", None)
    if struct and struct not in structs:
        outfile.write("""\
struct %s {
    /* TODO: add struct fields */
};

""" % struct)
        structs.append(struct)


def declare_methods(outfile, data, methods):
    struct = data.get("private_data_type", None)
    method_open = None
    method_close = None
    if "methods" in data:
        method_open = data["methods"].get("open")
        method_close = data["methods"].get("close")

    # declare open method
    if method_open and method_open not in methods:
        outfile.write("""\
static int
%s(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options)
{
""" % method_open)
        print_data_struct(outfile, struct)
        if "options" in data:
            outfile.write("""\
    const struct %(name_c)s_options *opts;

    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, %(NAME_C)s_OPTIONS_API_VERSION,
                                       -EINVAL);
    opts = (const struct %(name_c)s_options *)options;

""" % {
    "name_c": data["name_c"],
    "NAME_C": data["NAME_C"],
    })

        outfile.write("""\
    /* TODO: implement open method */

    return 0;
}

""")
        methods.append(method_open)

    # declare close method
    if method_close and method_close not in methods:
        outfile.write("""\
static void
%s(struct sol_flow_node *node, void *data)
{
""" % method_close)
        print_data_struct(outfile, struct)
        outfile.write("""\
    /* TODO: implement close method */
}

""")
        methods.append(method_close)

    methods_connect = {}
    methods_disconnect = {}
    methods_process = {}
    if "in_ports" in data:
        for o in data["in_ports"]:
            port_methods = o.get("methods", {})
            data_type = o.get("data_type");
            find_methods(port_methods, data_type, "process", methods_process)
            find_methods(port_methods, data_type,"connect", methods_connect)
            find_methods(port_methods, data_type, "disconnect",
                         methods_disconnect)
    if "out_ports" in data:
        for o in data["out_ports"]:
            port_methods = o.get("methods", {})
            data_type = o.get("data_type");
            find_methods(port_methods, data_type,"connect", methods_connect)
            find_methods(port_methods, data_type, "disconnect",
                         methods_disconnect)

    for method_connect in methods_connect:
        if method_connect in methods:
            continue
        methods.append(method_connect)

        outfile.write("""\
static int
%s(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id)
{
""" % method_connect)
        print_data_struct(outfile, struct)
        outfile.write("""\
    /* TODO: implement connect method */

    return 0;
}

""")

    for method_disconnect in methods_disconnect:
        if method_disconnect in methods:
            continue
        methods.append(method_disconnect)

        outfile.write("""\
static int
%s(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id)
{
""" % method_disconnect)
        print_data_struct(outfile, struct)
        outfile.write("""\
    /* TODO: implement disconnect method */

    return 0;
}

""")

    for method_process in methods_process:
        if method_process in methods:
            continue
        methods.append(method_process)

        outfile.write("""\
static int
%s(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet)
{
""" % method_process)
        print_data_struct(outfile, struct)
        single_type = get_single_packet_type(methods_process[method_process])
        if single_type:
            outfile.write("""\
    int r;
    %sin_value;

    r = %s;
    SOL_INT_CHECK(r, < 0, r);

""" % (data_type_to_c(single_type),
       data_type_to_packet_getter(single_type)))
        outfile.write("""\
    /* TODO: implement process method */

    return 0;
}

""")


def generate_stub(stub_file, inputs_list, prefix, is_module):
    data = []
    base_names = []
    methods = []
    # blacklist our types that are often used
    structs = ['sol_drange', 'sol_irange', 'sol_rgb', 'sol_direction_vector' ]
    packets = []

    try:
        for input_json in inputs_list:
            data.append(load_json(input_json, prefix))
            base_names.append(get_base_name(input_json.name))
    except:
        raise

    license_header(stub_file)

    for base_name in base_names:
        include_header(stub_file, base_name)

    include_common_headers(stub_file)

    add_empty_line(stub_file)

    # declare all custom packets - structs and functions
    for data_item in data:
        if not "types" in data_item:
            declare_packets(stub_file, data_item, packets)
        else:
            for t in data_item["types"]:
                declare_packets(stub_file, t, packets)

    # declare private data structs
    for data_item in data:
        if not "types" in data_item:
            declare_structs(stub_file, data_item, structs)
        else:
            for t in data_item["types"]:
                node_name = c_clean(t["name"].split("/")[1])
                t["name_c"] = data_item["name_c"]+"_" + node_name
                t["NAME_C"] = t["name_c"].upper()
                t["prefix_c"] = data_item["prefix_c"]

                declare_structs(stub_file, t, structs)

    # declare all node and port methods
    for data_item in data:
        if not "types" in data_item:
            declare_methods(stub_file, data_item, methods)
        else:
            for t in data_item["types"]:
                declare_methods(stub_file, t, methods)

    for base_name in base_names:
        include_source(stub_file, base_name)


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("--prefix",
                        help="Prefix to use in C generated code",
                        type=str)
    parser.add_argument("--module",
                        help="It'll generate a module instead of builtin node types.",
                        type=bool)
    parser.add_argument("--force",
                        help="Force stub file rewrite.",
                        default=False,
                        type=bool)
    parser.add_argument("output_stub",
                        help="Output stub code (.c) file",
                        type=str)
    parser.add_argument("inputs_list", nargs='+',
                        help="List of input description files in JSON format",
                        type=argparse.FileType('r'))

    args = parser.parse_args()
    if os.path.exists(args.output_stub) and not args.force:
        raise StubError("Can't overwrite stub file '%s'. Remove it yourself." %
                        args.output_stub)
    try:
        stub_file = open(args.output_stub, 'w')
    except:
        raise

    try:
        generate_stub(stub_file, args.inputs_list, args.prefix, args.module)
    except:
        raise
    finally:
        stub_file.close()
