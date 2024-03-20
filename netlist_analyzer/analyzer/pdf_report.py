#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
# anaylzer_report.py
import cairo
import argparse
import sys
from pathlib import Path
from ruamel.yaml import YAML


total_cycles = 1

def draw_box(ctx, coord, size):
    ctx.set_line_width(0.5)
    ctx.move_to(coord[0], coord[1])
    ctx.line_to(coord[0] + size[0], coord[1])
    ctx.line_to(coord[0] + size[0], coord[1] + size[1])
    ctx.line_to(coord[0], coord[1] + size[1])
    ctx.line_to(coord[0], coord[1])
    ctx.stroke()

def draw_text(ctx, coord, text):
    ctx.set_line_width(0.5)

    ctx.select_font_face("Sans", cairo.FONT_SLANT_NORMAL)
    ctx.set_font_size(5)

    ctx.move_to(coord[0], coord[1])
    ctx.show_text(text)

    #cr.move_to(0.27, 0.65)
    #cr.text_path("void")
    #cr.set_source_rgb(0.5, 0.5, 1)
    #cr.fill_preserve()
    #cr.set_source_rgb(0, 0, 0)
    #cr.set_line_width(0.01)
    #cr.stroke()

    # draw helping lines
    #cr.set_source_rgba(1, 0.2, 0.2, 0.6)
    #cr.arc(0.04, 0.53, 0.02, 0, 2 * pi)
    #cr.arc(0.27, 0.65, 0.02, 0, 2 * pi)
    #cr.fill()

# create canvas etc

#ctx = ...
def locate(start, offset):
    return (start[0] + offset[0], start[1] + offset[1])

def draw_node(ctx, node):
    #node origin loc
    loc = (int(node['location'][0]) * 105 + 50, int(node['location'][1]) * 105 + 50)

    # draw node box
    draw_box(ctx, loc, (100, 100))

    # draw external links
    def usage_str(link_name):
        users = node["links"][link_name]["num_occupants"]
        bw = node["links"][link_name]["total_data_in_bytes"] / float(total_cycles)
        return f"{users:d}, {bw:.2f}"

    #north
    draw_text(ctx, locate(loc, (20, 10)), f"in: {usage_str('noc0_in_north')}")
    draw_text(ctx, locate(loc, (50, 10)), f"out: {usage_str('noc1_out_north')}")

    #south
    draw_text(ctx, locate(loc, (50, 98)), f"in: {usage_str('noc1_in_south')}")
    draw_text(ctx, locate(loc, (20, 98)), f"out: {usage_str('noc0_out_south')}")

    #west
    draw_text(ctx, locate(loc, (2, 40)), f"in: {usage_str('noc0_in_west')}")
    draw_text(ctx, locate(loc, (2, 60)), f"out: {usage_str('noc1_out_west')}")

    #east
    draw_text(ctx, locate(loc, (70, 60)), f"in: {usage_str('noc1_in_east')}")
    draw_text(ctx, locate(loc, (70, 40)), f"out: {usage_str('noc0_out_east')}")

    def internal_usage_str(link_name):
        users = node["internal_links"][link_name]["num_occupants"]
        bw = node["internal_links"][link_name]["total_data_in_bytes"] / float(total_cycles)
        return f"{users:d}, {bw:.2f}"
    #internal in
    draw_text(ctx, locate(loc, (35, 40)), f"in: {internal_usage_str('link_in')}")
    draw_text(ctx, locate(loc, (35, 45)), f"out: {internal_usage_str('link_out')}")

    # draw type
    if (node["type"] == "dram"):
        draw_text(ctx, locate(loc, (35, 35)), "DRAM")
    elif (node["type"] == "core"):
        draw_text(ctx, locate(loc, (35, 35)), "CORE")

    if (node["op_name"] != ""):
        draw_text(ctx, locate(loc, (10, 15)), node["op_name"])

    if (node["op_cycles"] != ""):
        draw_text(ctx, locate(loc, (10, 20)), f"cycles: {node['op_cycles']}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="")
    parser.add_argument('--input-chip-yaml', required=True, help="Chip YAML path")

    state = parser.parse_args(sys.argv[1:])
    input_file_name = state.input_chip_yaml
    output_file_name = Path(input_file_name).with_suffix('.pdf')

    #yaml=YAML(typ='safe')   # default, if not specfied, is 'rt' (round-trip)
    yaml=YAML()   # default, if not specfied, is 'rt' (round-trip)
    yaml_data = yaml.load(open(input_file_name))

    #print("yaml data:")
    #print(yaml_data)


    surface = cairo.PDFSurface(output_file_name, 2000, 2000)

    ctx = cairo.Context(surface)

    total_cycles = yaml_data['bw_limited_op_cycles']
    for node in yaml_data['nodes']:
        #print("node data:")
        #print(node)
        draw_node(ctx, node)
    #draw_box(ctx, (10,20), (30, 40))
    surface.finish()

"""
x, y, x1, y1 = 0.1, 0.5, 0.4, 0.9
x2, y2, x3, y3 = 0.6, 0.1, 0.9, 0.5
ctx.scale(200, 200)
ctx.set_line_width(0.04)
ctx.move_to(x, y)
ctx.curve_to(x1, y1, x2, y2, x3, y3)
ctx.stroke()
ctx.set_source_rgba(1, 0.2, 0.2, 0.6)
ctx.set_line_width(0.02)
ctx.move_to(x, y)
ctx.line_to(x1, y1)
ctx.move_to(x2, y2)
ctx.line_to(x3, y3)
"""
