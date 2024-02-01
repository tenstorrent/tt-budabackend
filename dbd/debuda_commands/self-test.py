# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
from tt_object import TTObject, TTObjectIDDict
import tt_util as util
from tt_graph import Queue, Op
from tt_coordinate import OnChipCoordinate, CoordinateTranslationError

command_metadata = {
    "short": "st",
    "type": "dev",
    "description": "Tests Debuda internals.",
}


def test_object():
    class my_tt_obj(TTObject):
        pass

    oset1 = TTObjectIDDict()
    oset2 = TTObjectIDDict()

    for i in range(15):
        o = my_tt_obj()
        o._id = i
        if i <= 10:
            oset1.add(o)
        if i >= 5:
            oset2.add(o)

    odiff = oset1 - oset2
    print(type(odiff))

    oset3 = oset1.copy()
    print(f"oset3 == oset1: {oset3 == oset1}")

    elem = oset2.first()
    print(elem)
    assert elem in oset1
    assert elem in oset3
    oset1.delete(lambda x: x == elem)
    assert elem not in oset1
    assert elem in oset3
    oset1.compare(oset3)
    oset3.keep(lambda x: x == elem)

    print(oset2.find_id(elem.id()))
    assert oset2.find_id(-1) is None
    util.INFO(f"test_object: PASS")


def test_fanin_fanout_queue_op_level(context):
    graph = context.netlist.graphs.first()
    for fop in graph.ops:
        print(f"{fop} fanin: {graph.get_fanin(fop)}")
        print(f"{fop} fanout: {graph.get_fanout(fop)}")

    for fq in graph.queues:
        print(f"{fq} fanin: {graph.get_fanin(fq)}")
        print(f"{fq} fanout: {graph.get_fanout(fq)}")

    util.INFO(f"test_fanin_fanout: PASS")


def test_fanin_fanout_buffer_level(context):
    graph = context.netlist.graphs.first()
    for fop in graph.ops:
        op_buffers = graph.get_buffers(fop)
        print(f"test_fanin_fanout_buffer_level for {fop}")
        dest_buffers = op_buffers.copy()
        dest_buffers.keep(graph.is_dest_buffer)
        for buffer in dest_buffers:
            buffer_fanins = graph.get_fanin(buffer)
            print(f"{buffer} buffer fanin: {buffer_fanins}")

        src_buffers = op_buffers.copy()
        src_buffers.keep(graph.is_src_buffer)
        for buffer in src_buffers:
            buffer_fanouts = graph.get_fanout(buffer)
            print(f"{buffer} buffer fanout: {buffer_fanouts}")

        print(f"Pipes for buffer: {graph.get_pipes (src_buffers)}")


def test_coordinate_mapping(context):
    d = context.devices[0]
    util.INFO("--- Starting test_coordinate_mapping ---")
    harvesting_mask = d._harvesting["harvest_mask"]  # Save it as we will modify it
    util.INFO(f"Device harvesting mask: {harvesting_mask}")

    # 0. Create a harvesting map for example given by Logical NOC Corrdinate doc (NOC0 Y=2 and Y=10 are harvested):
    for k, v in d.nocVirt_to_nocTr_map.items():
        if v[1] == 18:
            print(f"{k} -> {v}")

    # Get the index of element containing 2 and 10 in the HARVESTING_NOC_LOCATIONS array (they are 2 & 3)
    d._harvesting["harvest_mask"] = (1 << 2) | (1 << 3)
    d._create_harvesting_maps()
    print(d.render(grid_coordinate="nocTr", cell_renderer=lambda x: x.to_str("noc0")))

    # 1. Non-harvested device:
    d._harvesting["harvest_mask"] = 0
    d._create_harvesting_maps()

    C1_1 = OnChipCoordinate(1, 1, "noc0", d)
    assert C1_1 == OnChipCoordinate(1, 1, "noc0", d)
    assert C1_1 == OnChipCoordinate(8, 10, "noc1", d)
    assert C1_1 == OnChipCoordinate(2, 2, "die", d)
    assert C1_1 == OnChipCoordinate(0, 0, "tensix", d)
    assert C1_1 == OnChipCoordinate(18, 18, "nocTr", d)
    assert C1_1 == OnChipCoordinate(0, 0, "netlist", d)

    # 2. Harvested device:
    d._harvesting["harvest_mask"] = 1
    d._create_harvesting_maps()
    assert OnChipCoordinate(1, 0, "tensix", d) == OnChipCoordinate(0, 0, "netlist", d)

    # 3. Printing
    for cs in ["noc0", "noc1", "nocTr", "die", "tensix"]:
        print(f"{cs}: {C1_1.to_str(cs)}")

    # This should throw CoordinateTranslationError as there is no mapping due to harvesting
    try:
        print(f"{cs}: {C1_1.to_str('netlist')}")
        assert False  # Should not reach here
    except CoordinateTranslationError:
        pass

    # The following depend on harvesting

    util.INFO(f"test_coordinate_mapping: PASS")
    util.INFO("--- Ending test_coordinate_mapping ---")


def test_reading_noc_registers(context):
    device = context.devices[0]

    all_good = True
    # for loc in device.get_block_locations():
    loc = OnChipCoordinate(1, 1, "noc0", device)
    val = device.read_print_noc_reg(loc, 0, "ID", 0x2C >> 2, device.RegType.Cmd)
    rd_x = val & 0x3F
    rd_y = (val >> 6) & 0x3F
    (exp_x, exp_y) = loc.to()
    all_good = all_good and rd_x == exp_x and rd_y == exp_y
    if not all_good:
        util.ERROR(f"expected {exp_x}, {exp_y} but got {rd_x}, {rd_y}")
    return all_good


def run(args, context, ui_state=None):
    navigation_suggestions = []
    test_coordinate_mapping(context)
    return navigation_suggestions
    test_reading_noc_registers(context)
    test_object()
    test_fanin_fanout_queue_op_level(context)
    test_fanin_fanout_buffer_level(context)

    return navigation_suggestions
