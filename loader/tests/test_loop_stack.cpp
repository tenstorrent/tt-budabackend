// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "runtime.hpp"
#include "verif.hpp"

// FW loop info structure, 16b fields should be enough
struct LoopInfo {
    int curr_iter = 0;
    int last_iter = 0;  // loop_count-1, so we fit power-of-2 numbers
    int epoch_ptr = 0;
};

// FW loop stack structure
template <int STACK_SIZE=4>
struct LoopStack {
    LoopInfo table[STACK_SIZE];
    int ptr = -1;
    LoopInfo* top() {
        if (ptr < 0) return nullptr;
        return &table[ptr];
    }
    void push(const LoopInfo &info) {
        ptr++;
        table[ptr] = info;
        if (ptr >= STACK_SIZE) throw std::runtime_error("LoopStack overflow");
    }
    void pop() {
        ptr--;
    }
};

// Global loop stack
LoopStack<4> lstack;

// Helper function to print loop stack depth
std::string prefix() {
    std::string p = "-";
    for (int j = 0; j < lstack.ptr; j++) p += "-";
    return p;
}

struct Command {
    std::string op_code;
    int loop_count;
};

void run_epoch_queue(const std::vector<Command> &eq) {
    for (int i = 0; i < eq.size();) {
        const auto &cmd = eq[i];

        if (cmd.op_code == "LoopStart") 
        {
            lstack.push({.last_iter = cmd.loop_count-1, .epoch_ptr = i+1});
            std::cout << prefix() << " New loop at depth=" << lstack.ptr << " ptr=" << i << std::endl;
        }
        else if (cmd.op_code == "LoopEnd")
        {
            auto *curr_loop = lstack.top();
            if (curr_loop->curr_iter < curr_loop->last_iter) {
                curr_loop->curr_iter++;
                i = curr_loop->epoch_ptr; // jump back to start of loop
                std::cout << prefix() <<  " Increment loop iter to " << curr_loop->curr_iter << " at depth=" << lstack.ptr << std::endl;
                continue;
            } else {
                std::cout << prefix() << " End loop at depth=" << lstack.ptr << std::endl;
                lstack.pop();
            }
        }
        else
        {
            std::cout << prefix() << ">> Running " << cmd.op_code << " at depth=" << lstack.ptr << std::endl;
        }
        i++;
    }
};

// Prototype for a loop stack implemented using fixed memory for #1087
// - unlimited parallel loops
// - limited depth defined by stack size
int main(int argc, char** argv)
{
    std::cout << "=========== Parallel Count(5) > Stack Size(4) =========" << std::endl;
    std::vector<Command> parallel_programs = {
        {"LoopStart", 2},
            {"P0_Epoch", 0},
        {"LoopEnd", 0},
        {"LoopStart", 2},
            {"P1_Epoch", 0},
        {"LoopEnd", 0},
        {"LoopStart", 2},
            {"P2_Epoch", 0},
        {"LoopEnd", 0},
        {"LoopStart", 2},
            {"P3_Epoch", 0},
        {"LoopEnd", 0},
        {"LoopStart", 2},
            {"P4_Epoch", 0},
        {"LoopEnd", 0},
        {"LoopStart", 2},
            {"P5_Epoch", 0},
        {"LoopEnd", 0},
    };
    run_epoch_queue(parallel_programs);

    std::cout << "========= Nested Count(4) == Stack Size(4) ===========" << std::endl;
    std::vector<Command> nested = {
        {"Foo0", 0},
        {"LoopStart", 2},
            {"Foo1", 0},
            {"LoopStart", 2},
                {"Foo2", 0},
                {"LoopStart", 2},
                    {"Foo3", 0},
                    {"LoopStart", 2},
                        {"Foo4", 0},
                    {"LoopEnd", 0},
                {"LoopEnd", 0},
            {"LoopEnd", 0},
        {"LoopEnd", 0},
    };
    run_epoch_queue(nested);

    std::cout << "========= Nested + Parallel ===========" << std::endl;
    std::vector<Command> training = {
        {"LoopStart", 2},
            {"LoopStart", 10},
                {"FwdEpoch0", 0},
                {"FwdEpoch1", 0},
            {"LoopEnd", 0},
            {"LoopStart", 10},
                {"Bwd_2", 0},
                {"Bwd_3", 0},
            {"LoopEnd", 0},
        {"LoopEnd", 0},
    };
    run_epoch_queue(training);
    return 0;
}

