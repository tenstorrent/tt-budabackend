// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "graph_creator/stream_graph/ncrisc_creators/dram_scatter_offset_compressor.h"

#include <vector>

#include "gtest/gtest.h"

using namespace pipegen2;

// Helper function used to run the test case - creates compressor instance and runs it on the input vector.
// It then compares the size of the expected and the observed output as well as the value of every element
// in those two vectors.
void run_dram_scatter_offset_compression_test(const std::vector<std::uint64_t>& input,
                                              const std::vector<std::uint64_t>& expected_output)
{
    DramScatterOffsetCompressor compressor(input);
    const std::vector<std::uint64_t> observed_output = compressor.compress_dram_scatter_offsets();

    EXPECT_EQ(expected_output.size(), observed_output.size());

    for (std::size_t i = 0; i < expected_output.size(); ++i)
    {
        EXPECT_EQ(expected_output[i], observed_output[i]);
    }
}

/**********************************************************************************************************************
    Tests for function: DramScatterOffsetCompressor::compress_dram_scatter_offsets
**********************************************************************************************************************/

TEST(Pipegen2_DramScatterOffsetCompressor, CompressDramScatterOffsets_NoCompression)
{
    const std::vector<std::uint64_t> input{1, 2, 3, 4, 5, 6, 7, 8, 9, 8, 7, 6, 5, 4, 3, 2, 1};

    const std::vector<std::uint64_t> expected_output{1, 2, 3, 4, 5, 6, 7, 8, 9, 8, 7, 6, 5, 4, 3, 2, 1};

    run_dram_scatter_offset_compression_test(input, expected_output);
}

TEST(Pipegen2_DramScatterOffsetCompressor, CompressDramScatterOffsets_BadWrap)
{
    const std::uint64_t value_which_causes_bad_wrap = 0xFFFFFFFFFF;

    const std::vector<std::uint64_t> input{
        1, 2, 3, 4, 5, 6, 7, 8, 9,
        2, 3, 4, 5, 6, 7, 8, 9, 10,
        3, 4, 5, 6, 7, 8, 9, 10, 11,
        value_which_causes_bad_wrap, 14, 15,

        // Just a random pattern which should not be compressed.
        2, 4, 3, 46, 67, 87, 834, 643, 347, 54, 7567, 5685, 234,
        2464, 457, 458, 68, 23, 36, 5474, 85668,
        12, 65, 76, 2, 5, 765, 235, 3463, 65
    };

    const std::vector<std::uint64_t> expected_output{
        1, 2, 3, 4, 5, 6, 7, 8, 9,
        2, 3, 4, 5, 6, 7, 8, 9, 10,
        3, 4, 5, 6, 7, 8, 9, 10, 11,
        value_which_causes_bad_wrap, 14, 15,

        2, 4, 3, 46, 67, 87, 834, 643, 347, 54, 7567, 5685, 234,
        2464, 457, 458, 68, 23, 36, 5474, 85668,
        12, 65, 76, 2, 5, 765, 235, 3463, 65
    };

    run_dram_scatter_offset_compression_test(input, expected_output);
}

TEST(Pipegen2_DramScatterOffsetCompressor, CompressDramScatterOffsets_LengthNotDivisibleByPatternSize)
{
    const std::vector<std::uint64_t> input{
        1, 2, 3, 4, 5, 6, 7, 8, 9,
        2, 3, 4, 5, 6, 7, 8, 9, 10,
        3, 4, 5, 6, 7, 8, 9, 10, 11,
        4, 5, 6
    };

    const std::vector<std::uint64_t> expected_output{1, 2, 3, 4, 5, 6, 7, 8, 9, 0x8000000200000009, 1, 4, 5, 6};

    run_dram_scatter_offset_compression_test(input, expected_output);
}

TEST(Pipegen2_DramScatterOffsetCompressor, CompressDramScatterOffsets_CompressHalfPatternLength)
{
    const std::vector<std::uint64_t> input{
        1, 2, 3, 4, 5, 6, 7, 8, 9,
        2, 3, 4, 5, 6, 7, 8, 9, 10
    };

    const std::vector<std::uint64_t> expected_output{1, 2, 3, 4, 5, 6, 7, 8, 9, 0x8000000100000009, 1};

    run_dram_scatter_offset_compression_test(input, expected_output);
}

TEST(Pipegen2_DramScatterOffsetCompressor, CompressDramScatterOffsets_AllSameElements)
{
    const unsigned int input_size = 1000;
    const std::vector<std::uint64_t> input(input_size, 3);

    const std::vector<std::uint64_t> expected_output{3, 3, 3, 3, 3, 3, 3, 3, 0x8000007c00000008, 0};

    run_dram_scatter_offset_compression_test(input, expected_output);
}

TEST(Pipegen2_DramScatterOffsetCompressor, CompressDramScatterOffsets_LargeInputSize)
{
    const unsigned int input_size = 10000;
    const std::vector<std::uint64_t> input(input_size, 3);

    const std::vector<std::uint64_t> expected_output{3, 3, 3, 3, 3, 3, 3, 3, 0x800004e100000008, 0};

    run_dram_scatter_offset_compression_test(input, expected_output);
}

TEST(Pipegen2_DramScatterOffsetCompressor, CompressDramScatterOffsets_MultipleCompressions)
{
    const std::vector<std::uint64_t> input{
        1, 2, 3, 4, 5, 6, 7, 8, 9,
        2, 3, 4, 5, 6, 7, 8, 9, 10,
        3, 4, 5, 6, 7, 8, 9, 10, 11,

        88, 90, 92, 94, 96, 98, 100, 102, 104, 106, 108,
        1088, 1090, 1092, 1094, 1096, 1098, 1100, 1102, 1104, 1106, 1108,
        2088, 2090, 2092, 2094, 2096, 2098, 2100, 2102, 2104, 2106, 2108,
        3088, 3090, 3092, 3094, 3096, 3098, 3100, 3102, 3104, 3106, 3108
    };

    const std::vector<std::uint64_t> expected_output{
        1, 2, 3, 4, 5, 6, 7, 8, 9, 0x8000000200000009, 1,
        88, 90, 92, 94, 96, 98, 100, 102, 104, 106, 108, 0x800000030000000b, 1000};

    run_dram_scatter_offset_compression_test(input, expected_output);
}

/**********************************************************************************************************************
    Sanity tests which compare pipegen1 and pipegen2 outputs on some of the examples.
**********************************************************************************************************************/

TEST(Pipegen2_DramScatterOffsetCompressor, CompressDramScatterOffsets_SanityTest1)
{
    const std::vector<std::uint64_t> input{
        0x41019b580, 0x4101a2f60, 0x4101aa940, 0x4101b2320, 0x4101b9d00, 0x4101c16e0, 0x4101c90c0, 0x4101d0aa0,
        0x4101d8480, 0x4101dfe60, 0x4101e7840, 0x4101ef220, 0x4101f6c00, 0x4101fe5e0, 0x410205fc0, 0x41020d9a0,
        0x410215380, 0x41021cd60, 0x410224740, 0x41022c120, 0x410233b00, 0x41023b4e0, 0x410242ec0, 0x41024a8a0,
        0x410252280, 0x410259c60, 0x410261640, 0x4104d2080, 0x4104d9a60, 0x4104e1440, 0x4104e8e20, 0x4104f0800,
        0x4104f81e0, 0x4104ffbc0, 0x4105075a0, 0x41050ef80, 0x410516960, 0x41051e340, 0x410525d20, 0x41052d700,
        0x4105350e0, 0x41053cac0, 0x4105444a0, 0x41054be80, 0x410553860, 0x41055b240, 0x410562c20, 0x41056a600,
        0x410571fe0, 0x4105799c0, 0x4105813a0, 0x410588d80, 0x410590760, 0x410598140, 0x410808b80, 0x410810560,
        0x410817f40, 0x41081f920, 0x410827300, 0x41082ece0, 0x4108366c0, 0x41083e0a0, 0x410845a80, 0x41084d460,
        0x410854e40, 0x41085c820, 0x410864200, 0x41086bbe0, 0x4108735c0, 0x41087afa0, 0x410882980, 0x41088a360,
        0x410891d40, 0x410899720, 0x4108a1100, 0x4108a8ae0, 0x4108b04c0, 0x4108b7ea0, 0x4108bf880, 0x4108c7260,
        0x4108cec40, 0x410b3f680, 0x410b47060, 0x410b4ea40, 0x410b56420, 0x410b5de00, 0x410b657e0, 0x410b6d1c0,
        0x410b74ba0, 0x410b7c580, 0x410b83f60, 0x410b8b940, 0x410b93320, 0x410b9ad00, 0x410ba26e0, 0x410baa0c0,
        0x410bb1aa0, 0x410bb9480, 0x410bc0e60, 0x410bc8840, 0x410bd0220, 0x410bd7c00, 0x410bdf5e0, 0x410be6fc0,
        0x410bee9a0, 0x410bf6380, 0x410bfdd60, 0x410c05740, 0x410e76180, 0x410e7db60, 0x410e85540, 0x410e8cf20,
        0x410e94900, 0x410e9c2e0, 0x410ea3cc0, 0x410eab6a0, 0x410eb3080, 0x410ebaa60, 0x410ec2440, 0x410ec9e20,
        0x410ed1800, 0x410ed91e0, 0x410ee0bc0, 0x410ee85a0, 0x410eeff80, 0x410ef7960, 0x410eff340, 0x410f06d20,
        0x410f0e700, 0x410f160e0, 0x410f1dac0, 0x410f254a0, 0x410f2ce80, 0x410f34860, 0x410f3c240
    };

    const std::vector<std::uint64_t> expected_output{
        0x41019b580, 0x4101a2f60, 0x4101aa940, 0x4101b2320, 0x4101b9d00, 0x4101c16e0, 0x4101c90c0, 0x4101d0aa0,
        0x4101d8480, 0x4101dfe60, 0x4101e7840, 0x4101ef220, 0x4101f6c00, 0x4101fe5e0, 0x410205fc0, 0x41020d9a0,
        0x410215380, 0x41021cd60, 0x410224740, 0x41022c120, 0x410233b00, 0x41023b4e0, 0x410242ec0, 0x41024a8a0,
        0x410252280, 0x410259c60, 0x410261640, 0x800000040000001b, 0x336b00
    };

    run_dram_scatter_offset_compression_test(input, expected_output);
}

TEST(Pipegen2_DramScatterOffsetCompressor, CompressDramScatterOffsets_SanityTest2)
{
    const std::vector<std::uint64_t> input{
        0x18a10476980, 0x18a10479220, 0x18a1047bac0, 0x18a1047e360, 0x18a10480c00, 0x18a104771a0, 0x18a10479a40,
        0x18a1047c2e0, 0x18a1047eb80, 0x18a10481420, 0x18a104779c0, 0x18a1047a260, 0x18a1047cb00, 0x18a1047f3a0,
        0x18a10481c40, 0x18a104781e0, 0x18a1047aa80, 0x18a1047d320, 0x18a1047fbc0, 0x18a10482460, 0x18a10478a00,
        0x18a1047b2a0, 0x18a1047db40, 0x18a104803e0, 0x18a10482c80, 0x18a104834c0, 0x18a10485d60, 0x18a10488600,
        0x18a1048aea0, 0x18a1048d740, 0x18a10483ce0, 0x18a10486580, 0x18a10488e20, 0x18a1048b6c0, 0x18a1048df60,
        0x18a10484500, 0x18a10486da0, 0x18a10489640, 0x18a1048bee0, 0x18a1048e780, 0x18a10484d20, 0x18a104875c0,
        0x18a10489e60, 0x18a1048c700, 0x18a1048efa0, 0x18a10485540, 0x18a10487de0, 0x18a1048a680, 0x18a1048cf20,
        0x18a1048f7c0, 0x18a10490000, 0x18a104928a0, 0x18a10495140, 0x18a104979e0, 0x18a1049a280, 0x18a10490820,
        0x18a104930c0, 0x18a10495960, 0x18a10498200, 0x18a1049aaa0, 0x18a10491040, 0x18a104938e0, 0x18a10496180,
        0x18a10498a20, 0x18a1049b2c0, 0x18a10491860, 0x18a10494100, 0x18a104969a0, 0x18a10499240, 0x18a1049bae0,
        0x18a10492080, 0x18a10494920, 0x18a104971c0, 0x18a10499a60, 0x18a1049c300
    };

    const std::vector<std::uint64_t> expected_output{
        0x18a10476980, 0x18a10479220, 0x18a1047bac0, 0x18a1047e360, 0x18a10480c00, 0x18a104771a0, 0x18a10479a40,
        0x18a1047c2e0, 0x18a1047eb80, 0x18a10481420, 0x18a104779c0, 0x18a1047a260, 0x18a1047cb00, 0x18a1047f3a0,
        0x18a10481c40, 0x18a104781e0, 0x18a1047aa80, 0x18a1047d320, 0x18a1047fbc0, 0x18a10482460, 0x18a10478a00,
        0x18a1047b2a0, 0x18a1047db40, 0x18a104803e0, 0x18a10482c80, 0x8000000200000019, 0xcb40
    };

    run_dram_scatter_offset_compression_test(input, expected_output);
}

TEST(Pipegen2_DramScatterOffsetCompressor, CompressDramScatterOffsets_SanityTest3)
{
    const std::vector<std::uint64_t> input{
        0x1871002df20, 0x18710089ea0, 0x1871002e7e0, 0x1871008a760, 0x187100359c0, 0x18710091940, 0x18710036280,
        0x18710092200, 0x1871003d460, 0x187100993e0, 0x1871003dd20, 0x18710099ca0, 0x18710044f00, 0x187100a0e80,
        0x187100457c0, 0x187100a1740, 0x1871004c9a0, 0x187100a8920, 0x1871004d260, 0x187100a91e0, 0x18710054440,
        0x187100b03c0, 0x18710054d00, 0x187100b0c80, 0x1871005bee0, 0x187100b7e60, 0x1871005c7a0, 0x187100b8720,
        0x18710063980, 0x187100bf900, 0x18710064240, 0x187100c01c0, 0x1871006b420, 0x187100c73a0, 0x1871006bce0,
        0x187100c7c60, 0x18710072ec0, 0x187100cee40, 0x18710073780, 0x187100cf700, 0x1871007a960, 0x187100d68e0,
        0x1871007b220, 0x187100d71a0, 0x18710082400, 0x187100de380, 0x18710082cc0, 0x187100dec40
    };

    const std::vector<std::uint64_t> expected_output{
        0x1871002df20, 0x18710089ea0, 0x1871002e7e0, 0x1871008a760, 0x187100359c0, 0x18710091940, 0x18710036280,
        0x18710092200, 0x8000000500000008, 0xf540
    };

    run_dram_scatter_offset_compression_test(input, expected_output);
}

TEST(Pipegen2_DramScatterOffsetCompressor, CompressDramScatterOffsets_SanityTest4)
{
    const std::vector<std::uint64_t> input{
        0x710101940, 0x710101940, 0x7109c7d20, 0x7109c7d20, 0x714caee60, 0x714caee60, 0x71a556b80, 0x71a556b80,
        0x71d1162e0, 0x71d1162e0, 0x71fb6a1a0, 0x71fb6a1a0, 0x722ba7a80, 0x722ba7a80, 0x72c925760, 0x72c925760,
        0x732717ba0, 0x732717ba0, 0x73d617940, 0x73d617940, 0x73e34a7c0, 0x73e34a7c0, 0x710102160, 0x710102160,
        0x7109c8540, 0x7109c8540, 0x714caf680, 0x714caf680, 0x71a5573a0, 0x71a5573a0, 0x71d116b00, 0x71d116b00,
        0x71fb6a9c0, 0x71fb6a9c0, 0x722ba82a0, 0x722ba82a0, 0x72c925f80, 0x72c925f80, 0x7327183c0, 0x7327183c0,
        0x73d618160, 0x73d618160, 0x73e34afe0, 0x73e34afe0, 0x710102980, 0x710102980, 0x7109c8d60, 0x7109c8d60,
        0x714cafea0, 0x714cafea0, 0x71a557bc0, 0x71a557bc0, 0x71d117320, 0x71d117320, 0x71fb6b1e0, 0x71fb6b1e0,
        0x722ba8ac0, 0x722ba8ac0, 0x72c9267a0, 0x72c9267a0, 0x732718be0, 0x732718be0, 0x73d618980, 0x73d618980,
        0x73e34b800, 0x73e34b800, 0x7101031a0, 0x7101031a0, 0x7109c9580, 0x7109c9580, 0x714cb06c0, 0x714cb06c0,
        0x71a5583e0, 0x71a5583e0, 0x71d117b40, 0x71d117b40, 0x71fb6ba00, 0x71fb6ba00, 0x722ba92e0, 0x722ba92e0,
        0x72c926fc0, 0x72c926fc0, 0x732719400, 0x732719400, 0x73d6191a0, 0x73d6191a0, 0x73e34c020, 0x73e34c020,
        0x7101039c0, 0x7101039c0, 0x7109c9da0, 0x7109c9da0, 0x714cb0ee0, 0x714cb0ee0, 0x71a558c00, 0x71a558c00,
        0x71d118360, 0x71d118360, 0x71fb6c220, 0x71fb6c220, 0x722ba9b00, 0x722ba9b00, 0x72c9277e0, 0x72c9277e0,
        0x732719c20, 0x732719c20, 0x73d6199c0, 0x73d6199c0, 0x73e34c840, 0x73e34c840, 0x7101041e0, 0x7101041e0,
        0x7109ca5c0, 0x7109ca5c0, 0x714cb1700, 0x714cb1700, 0x71a559420, 0x71a559420, 0x71d118b80, 0x71d118b80,
        0x71fb6ca40, 0x71fb6ca40, 0x722baa320, 0x722baa320, 0x72c928000, 0x72c928000, 0x73271a440, 0x73271a440,
        0x73d61a1e0, 0x73d61a1e0, 0x73e34d060, 0x73e34d060, 0x710104a00, 0x710104a00, 0x7109cade0, 0x7109cade0,
        0x714cb1f20, 0x714cb1f20, 0x71a559c40, 0x71a559c40, 0x71d1193a0, 0x71d1193a0, 0x71fb6d260, 0x71fb6d260,
        0x722baab40, 0x722baab40, 0x72c928820, 0x72c928820, 0x73271ac60, 0x73271ac60, 0x73d61aa00, 0x73d61aa00,
        0x73e34d880, 0x73e34d880, 0x710105220, 0x710105220, 0x7109cb600, 0x7109cb600, 0x714cb2740, 0x714cb2740,
        0x71a55a460, 0x71a55a460, 0x71d119bc0, 0x71d119bc0, 0x71fb6da80, 0x71fb6da80, 0x722bab360, 0x722bab360,
        0x72c929040, 0x72c929040, 0x73271b480, 0x73271b480, 0x73d61b220, 0x73d61b220, 0x73e34e0a0, 0x73e34e0a0
    };

    const std::vector<std::uint64_t> expected_output{
        0x710101940, 0x710101940, 0x7109c7d20, 0x7109c7d20, 0x714caee60, 0x714caee60, 0x71a556b80, 0x71a556b80,
        0x71d1162e0, 0x71d1162e0, 0x71fb6a1a0, 0x71fb6a1a0, 0x722ba7a80, 0x722ba7a80, 0x72c925760, 0x72c925760,
        0x732717ba0, 0x732717ba0, 0x73d617940, 0x73d617940, 0x73e34a7c0, 0x73e34a7c0, 0x8000000700000016, 0x820
    };

    run_dram_scatter_offset_compression_test(input, expected_output);
}

TEST(Pipegen2_DramScatterOffsetCompressor, CompressDramScatterOffsets_SanityTest5)
{
    const std::vector<std::uint64_t> input{
        0x710f51da0, 0x710f52de0, 0x71e66a900, 0x710f525c0, 0x710f53600, 0x71e66b120, 0x71e66b940, 0x736f89fc0,
        0x736f8b000, 0x71e66c160, 0x736f8a7e0, 0x736f8b820, 0x710f53e20, 0x710f54e60, 0x71e66c980, 0x710f54640,
        0x710f55680, 0x71e66d1a0, 0x71e66d9c0, 0x736f8c040, 0x736f8d080, 0x71e66e1e0, 0x736f8c860, 0x736f8d8a0,
        0x710f55ea0, 0x710f56ee0, 0x71e66ea00, 0x710f566c0, 0x710f57700, 0x71e66f220, 0x71e66fa40, 0x736f8e0c0,
        0x736f8f100, 0x71e670260, 0x736f8e8e0, 0x736f8f920
    };

    const std::vector<std::uint64_t> expected_output{
        0x710f51da0, 0x710f52de0, 0x71e66a900, 0x710f525c0, 0x710f53600, 0x71e66b120, 0x71e66b940, 0x736f89fc0,
        0x736f8b000, 0x71e66c160, 0x736f8a7e0, 0x736f8b820, 0x800000020000000c, 0x2080
    };

    run_dram_scatter_offset_compression_test(input, expected_output);
}