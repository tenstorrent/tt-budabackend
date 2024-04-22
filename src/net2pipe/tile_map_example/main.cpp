// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <iostream>
#include <cstdio>
#include <cstdint>
#include "tile_maps.h"

using namespace std;


int main(int argc, char* argv[]) {

  // Instantiate three_d_array_tile_src_map with producer op data format parameters:
  three_d_array_tile_src_map tm("producer_op", "consumer_op",
                                2,     // producer t
                                2, 2,  // producer ublock tiles r, c
                                2, 2,  // producer mblock ublocks r, c
                                2, 2,  // producer grid r, c
                                1,     // producer output buf size mblocks (not counting double-buffering)
                                true); // producer output has row major ublock scan order?

  // Apply TMs to producer output
  {
    std::string tm_name = "hslice";
    std::vector<int> tm_args;
    tm_args.push_back(2);
    tm = tm.apply_tm(tm_name, tm_args);
  }
  {
    std::string tm_name = "vstack";
    std::vector<int> tm_args;
    tm_args.push_back(2);
    tm = tm.apply_tm(tm_name, tm_args);
  }

  // prints the canonical form resulting from the TMs and how each tile maps to producer core & tile index
  tm.print();
  cout << "\n\n";

  // Finally, reblock the post-TM canonical form to the consumer data format and compute the consumer input pipes.
  // Depending on the consumer op type and input, we need to call one of the following functions:
  //     three_d_array_tile_src_map::get_op_matmul_col_input
  //     three_d_array_tile_src_map::get_op_matmul_row_input
  //     three_d_array_tile_src_map::get_op_eltwise_input
  consumer_to_producer_tile_map tile_map =  tm.get_op_eltwise_input(0, /* kernel_bcast_tiles */
                                                                    false, /* kernel_bcast_tiles_per_t */
                                                                    2,    // consumer t
                                                                    1, 2, // ublock r, c
                                                                    2, 1, // mblock ublocks r, c,
                                                                    8, 2, // grid r, c
                                                                    true  // consumer_row_major_ublock_scan_order
                                                                    );

  cout << "\n\n";

  // prints the resulting producer->consumer pipes
  tile_map.print();
  cout << "\n\n";


  // Estimate worst-caset resource usage using the constraints from netlist_resource_constraints.txt.
  // (This doesn't run the TM engine, just looks at producer and consumer data format + TMs and
  // applies these constraints.)
  int max_producer_core_fan_out_streams_estimate;
  int max_producer_core_scatter_stream_blob_phases_estimate;
  int max_consumer_core_gather_stream_blob_phases_estimate;

  tm.estimate_resource_usage(OpClass::EltwiseBinary, 0,
                             2, // consumer t
                             8, 2, // consumer grid r, c
                             2, 1, // consumer mblock ublocks r, c
                             1, 2, // consumer_ublock tiles r, c
                             0, 0, // consumer_mblock_ublocks_k, consumer_ublock_tiles_k -> not used for eltwise, need to be specified for matmul
                             true, // consumer row major ublock scan order
                             max_producer_core_fan_out_streams_estimate, // outputs
                             max_producer_core_scatter_stream_blob_phases_estimate,
                             max_consumer_core_gather_stream_blob_phases_estimate);

  // Actual resource usage of the pipes produced by the TM engine:
  int max_producer_core_fan_out_streams = tile_map.max_producer_core_fan_out();
  int max_producer_core_scatter_stream_blob_phases = tile_map.max_producer_core_phases();
  int max_consumer_core_gather_stream_blob_phases = tile_map.max_consumer_core_phases();

  cout << "Max producer core fan out streams - estimated: " << max_producer_core_fan_out_streams_estimate << ", actual: " << max_producer_core_fan_out_streams  << "\n";
  cout << "Max producer core scatter stream blob phases - estimated: " << max_producer_core_scatter_stream_blob_phases_estimate << ", actual: " << max_producer_core_scatter_stream_blob_phases  << "\n";
  cout << "Max cosumer core gather stream blob phases - estimated: " << max_consumer_core_gather_stream_blob_phases_estimate << ", actual: " << max_consumer_core_gather_stream_blob_phases  << "\n";

  cout << "\n\n";

}
