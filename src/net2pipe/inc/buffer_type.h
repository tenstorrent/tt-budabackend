// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

namespace n2p {
    enum class EBufferType {
        Prolog,
        Epilog,
        GradientOp,
        Intermediate,
        Packer,
        Unpacker,
        DramIO,
        Relay,
        PrologRelay,
        EthernetRelay,
        Unknown
    };

    const std::string c_Prolog = "dram_prolog";
    const std::string c_Epilog = "dram_epilog";
    const std::string c_GradientOp = "gradient_op";
    const std::string c_Intermediate = "intermediate";
    const std::string c_Packer = "packer";
    const std::string c_Unpacker = "unpacker";
    const std::string c_DramIO = "dram_io";
    const std::string c_Relay = "relay";
    const std::string c_EthernetRelay = "ethernet_relay";
    const std::string c_PrologRelay = "prolog_relay";
    const std::string c_Unknown = "unknown";

    const std::string& convert_to_string(EBufferType type) {
        switch (type) {
            case EBufferType::PrologRelay:
                return c_PrologRelay;
            case EBufferType::Prolog:
                return c_Prolog;
            case EBufferType::Epilog:
                return c_Epilog;
            case EBufferType::GradientOp:
                return c_GradientOp;
            case EBufferType::Intermediate:
                return c_Intermediate;
            case EBufferType::Packer:
                return c_Packer;
            case EBufferType::Unpacker:
                return c_Unpacker;
            case EBufferType::DramIO:
                return c_DramIO;
            case EBufferType::Relay:
                return c_Relay;
            case EBufferType::EthernetRelay:
                return c_EthernetRelay;
            default:
                return c_Unknown;
        }
    }
}
