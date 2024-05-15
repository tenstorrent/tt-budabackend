# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
import unittest, re
from tt_parse_elf import read_elf, mem_access
from tt_firmware import ELF, BUDA_FW_VARS
from tabulate import tabulate


class TestFirmware(unittest.TestCase):
    # @unittest.skip("demonstrating skipping")
    def test_epoch_id_access(self):
        elf = ELF({"brisc": "./debuda_test/brisc/brisc.elf"}, extra_vars=BUDA_FW_VARS)

        # Test if this var is injected in the variable table
        assert "EPOCH_INFO_PTR" in elf.names["brisc"]["variable"]

        # epoch_id_addr, epoch_id_size = elf._get_var_addr_size("brisc", "EPOCH_INFO_PTR.tile_clear_blob")
        # print (f"For name EPOCH_INFO_PTR.dram_buf_addr, addr={epoch_id_addr}, size={epoch_id_size}")

        epoch_id_addr, epoch_id_size = elf._get_var_addr_size(
            "brisc", "EPOCH_INFO_PTR.tile_clear_blob.blob_words"
        )
        print(
            f"For name EPOCH_INFO_PTR.tile_clear_blob.blob_words, addr={epoch_id_addr}, size={epoch_id_size}"
        )

        epoch_id_addr, epoch_id_size = elf._get_var_addr_size(
            "brisc", "EPOCH_INFO_PTR.tile_clear_blob.blob_words[0][0]"
        )
        print(
            f"For name EPOCH_INFO_PTR.tile_clear_blob.blob_words[0][0], addr={epoch_id_addr}, size={epoch_id_size}"
        )

        epoch_id_addr, epoch_id_size = elf._get_var_addr_size(
            "brisc", "EPOCH_INFO_PTR.tile_clear_blob.blob_words[0]"
        )
        print(
            f"For name EPOCH_INFO_PTR.tile_clear_blob.blob_words[0], addr={epoch_id_addr}, size={epoch_id_size}"
        )

        # Test that we can get the address of the epoch_id
        epoch_id_addr, epoch_id_size = elf._get_var_addr_size(
            "brisc", "EPOCH_INFO_PTR.epoch_id"
        )
        assert epoch_id_size == 4
        assert epoch_id_addr == 0x23080 + 652


if __name__ == "__main__":
    unittest.main()
