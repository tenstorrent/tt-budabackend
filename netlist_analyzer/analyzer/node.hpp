// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <yaml-cpp/yaml.h>

#include "link.hpp"
#include "util_types.h"
#include "common/analyzer_api_types.hpp"

#include "pipe.hpp"

#define L1_BYTES_PER_CYCLE  32 // TODO FIX
#define NOC_BYTES_PER_CYCLE 32
// 1600 mhz * 2 (ddr) * 32 bit (4B) = 12.800 GB/s
// 12.8 GB/s over 1200mhz AI clk = 10.67 B / cycle
#define GS_DRAM_BYTES_PER_CYCLE 10.67 
#define WH_DRAM_BYTES_PER_CYCLE 32
#define PCIE_BYTES_PER_CYCLE 16 // TODO FIX ME : DO GLOBAL SEARCH
#define ETH_BYTES_PER_CYCLE  10 // = (100 Gbit / 8 bits / 1.2 GHz)

namespace analyzer {

class Op;

class Node {
    public:
    struct NodeLinks {
        
        std::shared_ptr<Link> noc0_in_north;
        std::shared_ptr<Link> noc0_out_south;
        std::shared_ptr<Link> noc0_in_west;
        std::shared_ptr<Link> noc0_out_east;

        std::shared_ptr<Link> noc1_in_south;
        std::shared_ptr<Link> noc1_out_north;
        std::shared_ptr<Link> noc1_in_east;
        std::shared_ptr<Link> noc1_out_west;
    };

    Node(const GridLoc& loc, const NodeLinks& node_links) {
        external_links = node_links;
        soc_location = loc;
        
        this->noc0_link_in = std::make_shared<Link>("noc0_link_in_" + std::to_string(loc.y) + "_y_" + std::to_string(loc.x) + "_x", NOC_BYTES_PER_CYCLE);
        this->noc0_link_out = std::make_shared<Link>("noc0_link_out_" + std::to_string(loc.y) + "_y_" + std::to_string(loc.x) + "_x", NOC_BYTES_PER_CYCLE);
        this->noc1_link_in = std::make_shared<Link>("noc1_link_in_" + std::to_string(loc.y) + "_y_" + std::to_string(loc.x) + "_x", NOC_BYTES_PER_CYCLE);
        this->noc1_link_out = std::make_shared<Link>("noc1_link_out_" + std::to_string(loc.y) + "_y_" + std::to_string(loc.x) + "_x", NOC_BYTES_PER_CYCLE);

        registerInternalLink(this->noc0_link_in);
        registerInternalLink(this->noc0_link_out);
        registerInternalLink(this->noc1_link_in);
        registerInternalLink(this->noc1_link_out);
    };

    float getNocBandwidth(int id);
    
    virtual void routeFromNodeToNoc0(Pipe* p, int data_size) {
    }
    virtual void routeToNodeFromNoc0(Pipe* p, int data_size) {
    }

    virtual void routeFromNodeToNoc1(Pipe* p, int data_size) {
    }
    virtual void routeToNodeFromNoc1(Pipe* p, int data_size) {
    }
    
    void registerInternalLink(std::shared_ptr<Link> link) {
        internal_links.push_back(link);
    }

    std::vector<std::shared_ptr<Link>> getInternalLinks() const {
        return internal_links;
    }

    // attrs
    GridLoc soc_location;
    std::string node_type;
    NodeLinks external_links;
    std::vector<std::shared_ptr<Link>> internal_links;

    std::shared_ptr<Link> noc0_link_in;
    std::shared_ptr<Link> noc0_link_out;
    
    std::shared_ptr<Link> noc1_link_in;
    std::shared_ptr<Link> noc1_link_out;

    // Deprecated
    Op* mapped_op = nullptr;
};

class RouterNode : public Node {
    public:
    RouterNode(const GridLoc& loc, const NodeLinks& node_links) : Node(loc, node_links) {
        node_type = "router";
    };
    ~RouterNode() = default;

};

class CoreNode : public Node {
    public:
        CoreNode(const GridLoc& loc, const NodeLinks& node_links) : Node(loc, node_links) {
            this->node_type = "core";    
        };
        ~CoreNode() = default;

        std::shared_ptr<Op> op;
        std::string node_op = "invalid";
};

class DramInternal {
    public:
        DramInternal() = default;
        virtual void routeToDram(int noc_id, int subchan_id, int dram_id, Pipe* p, int data_size) = 0;
        virtual void routeFromDram(int noc_id, int subchan_id, int dram_id, Pipe* p, int data_size) = 0;
        virtual ~DramInternal() = default;
        
        std::vector<std::shared_ptr<Link>> getInternalLinks() const {
            return links;
        }
    protected:
        std::vector<std::shared_ptr<Link>> links;
};

class gs_dram_channel : public DramInternal {
    public:
        gs_dram_channel(int channel_id) {
            this->channel_id = channel_id;
            this->noc0_noc2axi = std::make_shared<Link>("noc0_noc2axi", L1_BYTES_PER_CYCLE);
            this->noc1_noc2axi = std::make_shared<Link>("noc1_noc2axi", L1_BYTES_PER_CYCLE);
            this->dram_inout = std::make_shared<Link>("dram_inout", GS_DRAM_BYTES_PER_CYCLE);
            links.push_back(noc0_noc2axi);
            links.push_back(noc1_noc2axi);
            links.push_back(dram_inout);
        }
        ~gs_dram_channel() = default;

        void routeToDram(int noc_id, int dram_id, int subchan_id, Pipe* p, int data_size) override {
            if(noc_id == 0) {
                this->noc0_noc2axi->addPipeWithoutVC(p->pipe_id, data_size);
                this->dram_inout->addPipeWithoutVC(p->pipe_id, data_size);
            }
            else if (noc_id == 1) {
                this->noc1_noc2axi->addPipeWithoutVC(p->pipe_id, data_size);
                this->dram_inout->addPipeWithoutVC(p->pipe_id, data_size);
            }
            else {
                // TODO: assert
            }
        }

        void routeFromDram(int noc_id, int dram_id, int subchan_id, Pipe* p, int data_size) override {
            routeToDram( noc_id, dram_id, subchan_id, p, data_size);
        }

        std::shared_ptr<Link> noc0_noc2axi;
        std::shared_ptr<Link> noc1_noc2axi;
        std::shared_ptr<Link> dram_inout;
        int channel_id = -1;
};

class wh_dram_channel  : public DramInternal {
    public:
        wh_dram_channel(int channel_id) {
            this->channel_id = channel_id;
            for(int i = 0; i < 3; i++) {
                auto noc0_noc2axi = std::make_shared<Link>("noc0_subchan" + std::to_string(i) + "_noc2axi", L1_BYTES_PER_CYCLE);
                auto noc1_noc2axi = std::make_shared<Link>("noc1_subchan" + std::to_string(i) + "_noc2axi", L1_BYTES_PER_CYCLE);
                links.push_back(noc0_noc2axi);
                links.push_back(noc1_noc2axi);
                noc2axi.push_back({noc0_noc2axi, noc1_noc2axi});
            }
            dram0_inout = std::make_shared<Link>("dram0_inout", WH_DRAM_BYTES_PER_CYCLE);
            dram1_inout = std::make_shared<Link>("dram1_inout", WH_DRAM_BYTES_PER_CYCLE);

            links.push_back(dram0_inout);
            links.push_back(dram1_inout);
        }

        ~wh_dram_channel() = default;

        void routeToDram(int noc_id, int dram_id, int subchan_id, Pipe* p, int data_size) override {
            this->noc2axi.at(subchan_id).at(noc_id)->addPipeWithoutVC(p->pipe_id, data_size);
            if(p->dram_bank == 0) {
                this->dram0_inout->addPipeWithoutVC(p->pipe_id, data_size);
            }
            else if(p->dram_bank == 1) {
                this->dram1_inout->addPipeWithoutVC(p->pipe_id, data_size);
            }
        }

        void routeFromDram(int noc_id, int dram_id, int subchan_id, Pipe* p, int data_size) override {
            routeToDram( noc_id, dram_id, subchan_id, p, data_size);
        }

        std::vector<std::vector<std::shared_ptr<Link>>> noc2axi;
        std::shared_ptr<Link> dram0_inout;
        std::shared_ptr<Link> dram1_inout;
        int channel_id;
};

class DramNode : public Node {
    public:
        DramNode(const GridLoc& loc, const NodeLinks& node_links, std::shared_ptr<DramInternal> dram_internal = nullptr,  int channel = 0, int subchannel = 0) : Node(loc, node_links) {
            node_type = "dram";
            this->channel = channel;
            this->subchannel = subchannel;
            this->dram_internal = dram_internal;
        };
        ~DramNode() = default;
        
        int channel = 0;
        int subchannel = 0;
        std::shared_ptr<DramInternal> dram_internal;

        void routeFromNodeToNoc0(Pipe* p, int data_size) override {
            dram_internal->routeFromDram(0, this->channel, this->subchannel, p, data_size);
        }
        void routeToNodeFromNoc0(Pipe* p, int data_size) override {
            routeFromNodeToNoc0(p, data_size);
        }

        void routeFromNodeToNoc1(Pipe* p, int data_size) override {
            dram_internal->routeFromDram(1, this->channel, this->subchannel, p, data_size);
        }
        void routeToNodeFromNoc1(Pipe* p, int data_size) override {
            routeFromNodeToNoc1(p, data_size);
        }
};

class PcieNode : public Node {
    public:
        PcieNode(const GridLoc& loc, const NodeLinks& node_links) : Node(loc, node_links) {
            node_type = "pcie";
            this->noc0_noc2axi = std::make_shared<Link>("noc0_noc2axi", L1_BYTES_PER_CYCLE);
            this->noc1_noc2axi = std::make_shared<Link>("noc1_noc2axi", L1_BYTES_PER_CYCLE);
            this->pcie_inout = std::make_shared<Link>("dram_inout", GS_DRAM_BYTES_PER_CYCLE);
            registerInternalLink(noc0_noc2axi);
            registerInternalLink(noc1_noc2axi);
            registerInternalLink(pcie_inout);
        };
        ~PcieNode() = default;

        void routeFromNodeToNoc0(Pipe* p, int data_size) override {
            this->noc0_noc2axi->addPipeWithoutVC(p->pipe_id, data_size);
            this->pcie_inout->addPipeWithoutVC(p->pipe_id, data_size);
        }

        void routeToNodeFromNoc0(Pipe* p, int data_size) override {
            routeFromNodeToNoc0(p, data_size);
        }

        void routeFromNodeToNoc1(Pipe* p, int data_size) override {
            this->noc1_noc2axi->addPipeWithoutVC(p->pipe_id, data_size);
            this->pcie_inout->addPipeWithoutVC(p->pipe_id, data_size);
        }

        void routeToNodeFromNoc1(Pipe* p, int data_size) override {
            routeFromNodeToNoc1(p, data_size);
        }
        
        std::shared_ptr<Link> noc0_noc2axi;
        std::shared_ptr<Link> noc1_noc2axi;
        std::shared_ptr<Link> pcie_inout;
};

class EthNode : public Node {
    public:
        EthNode(const GridLoc& loc, const NodeLinks& node_links) : Node(loc, node_links) {
            this->node_type = "eth";
            this->to_ethernet = std::make_shared<Link>("to_ethernet_" + std::to_string(loc.y) + "_y_" + std::to_string(loc.x) + "_x", ETH_BYTES_PER_CYCLE);
            this->from_ethernet = std::make_shared<Link>("from_ethernet_" + std::to_string(loc.y) + "_y_" + std::to_string(loc.x) + "_x", ETH_BYTES_PER_CYCLE);
        };
        ~EthNode() = default;

        std::shared_ptr<Link> to_ethernet;
        std::shared_ptr<Link> from_ethernet;

        std::shared_ptr<Op> op; // Must be eth op
        std::string node_op = "invalid";
};

}  // namespace analyzer