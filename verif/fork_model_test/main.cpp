// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <iostream>
#include <cstdio>
#include <cstdint>
#include <random>
#include <string>
#include <algorithm>
#include "tile_maps.h"

using namespace std;

static mt19937 rnd_engine;
static uniform_int_distribution<int> rnd_distr(0, INT_MAX);


void randomize_blocking(int dim_tiles, int& ublock, int& mblock, int& grid) {

  vector<int> prime_factors;
  prime_factors.push_back(1);

  int n = dim_tiles;
  for (int p = 2; p <= dim_tiles; p++) {
    while ((n % p) == 0) {
      prime_factors.push_back(p);
      n /= p;
    }
  }

  shuffle(prime_factors.begin(), prime_factors.end(), rnd_engine);

  // printf(" ** dim_tiles = %d -> %d prime factors = ", dim_tiles, prime_factors.size());
  // for (int f : prime_factors) {
  //   printf("%d ", f);
  // }
  // printf("\n");

  vector<int> dims;
  for (int i = 0; i < 3; i++) {
    dims.push_back(1);
  }

  int m1 = rnd_distr(rnd_engine) % prime_factors.size();
  bool lower_side = (m1 == 0) ? false : (rnd_distr(rnd_engine) % 2);
  int m2;
  if (!lower_side) {
    int upper_side_size = prime_factors.size() - m1;
    m2 = (upper_side_size == 0) ? m1 : (m1 + (rnd_distr(rnd_engine) % upper_side_size));
  } else {
    m2 = m1;
    m1 = rnd_distr(rnd_engine) % m2;
  }

  for (int i = 0; i < m1; i++) {
    dims[0] *= prime_factors[i];
  }
  for (int i = m1; i < m2; i++) {
    dims[1] *= prime_factors[i];
  }
  for (int i = m2; i < prime_factors.size(); i++) {
    dims[2] *= prime_factors[i];
  }

  shuffle(dims.begin(), dims.end(), rnd_engine);
  ublock = dims[0];
  mblock = dims[1];
  grid = dims[2];

  int dim_tiles_check = ublock*mblock*grid;
  if (dim_tiles_check != dim_tiles) {
		throw runtime_error("factorization error");
  }
  // printf ("   ** Factorized dimension %d = %d * %d * %d\n", dim_tiles, ublock, mblock, grid);
}


int main(int argc, char* argv[]) {

  if (argc != 3) {
    printf ("Usage fork_model_test <num test rounds> <random seed>\n\n");
    return 0;
  }

  int num_rounds = stoi(argv[1]);
  int rnd_seed = stoi(argv[2]);

  rnd_engine.seed(rnd_seed);

  int resource_max[3];
  resource_max[0] = 16;
  resource_max[1] = 2000;
  resource_max[2] = 2000;

  vector<string> resource_name;
  resource_name.push_back("fork streams per core");
  resource_name.push_back("producer stream phases per core");
  resource_name.push_back("consumer stream phases per core");

  for (int round = 0; round < num_rounds; round++) {

    int resource_estimate[3];
    int resource_analysis[3];

    int producer_t = 1 + rnd_distr(rnd_engine) % 32;
    int producer_ublock_r = 1 + rnd_distr(rnd_engine) % 8;
    int producer_ublock_c = 1 + rnd_distr(rnd_engine) % 8;
    int producer_mblock_r = 1 + rnd_distr(rnd_engine) % 8;
    int producer_mblock_c = 1 + rnd_distr(rnd_engine) % 8;
    int producer_grid_r = 1 + rnd_distr(rnd_engine) % 8;
    int producer_grid_c = 1 + rnd_distr(rnd_engine) % 8;
    int producer_t_buf = (rnd_distr(rnd_engine) % 4) ? 1 : producer_t;
    bool producer_row_major_ublock_scan_order = rnd_distr(rnd_engine) % 2;

    three_d_array_tile_src_map tm("producer_op", "consumer_op",
                                  producer_t, 
                                  producer_ublock_r, producer_ublock_c, 
                                  producer_mblock_r, producer_mblock_c,
                                  producer_grid_r, producer_grid_c,
                                  producer_t_buf,    
                                  producer_row_major_ublock_scan_order); 

    int reblock_type = rnd_distr(rnd_engine) % 3; // 0 = matmul input 0, 1 = matmul input 1, 2 = eltwise input
    bool consumer_row_major_ublock_scan_order = (reblock_type == 2) ? (rnd_distr(rnd_engine) % 2) : (reblock_type == 1);

    printf("\n\nRound %d:\n", round);
    printf("Producer: buf_size_mb = %d, %s\n", producer_t_buf*2, tm.producer_data_format.to_string().c_str());
    printf("Reblock type: %d\n", reblock_type);

    int consumer_t = tm.get_size(map_dims::t);
    int consumer_rt = tm.get_size(map_dims::rt);
    int consumer_ct = tm.get_size(map_dims::ct);

    int consumer_ublock_r, consumer_mblock_r, consumer_grid_r;
    int consumer_ublock_c, consumer_mblock_c, consumer_grid_c;
    randomize_blocking(consumer_rt, consumer_ublock_r, consumer_mblock_r, consumer_grid_r);
    randomize_blocking(consumer_ct, consumer_ublock_c, consumer_mblock_c, consumer_grid_c);

    
    consumer_to_producer_tile_map tile_map;
    int consumer_ublock_tiles_k;
    int consumer_mblock_ublocks_k;
    if (reblock_type == 0) {
      consumer_ublock_tiles_k = consumer_ublock_c;
      consumer_mblock_ublocks_k = consumer_mblock_c * consumer_grid_c; // randomize full k dimension this way
      tile_map = tm.get_op_matmul_row_input(consumer_t, 
                                            consumer_ublock_r, consumer_ublock_tiles_k,
                                            consumer_mblock_r, consumer_mblock_ublocks_k,
                                            consumer_grid_r, consumer_grid_c
                                            );
      tm.estimate_resource_usage(OpClass::MatMul, 0,
                                 consumer_t,
                                 consumer_grid_r, consumer_grid_c,
                                 consumer_mblock_r, consumer_mblock_c, 
                                 consumer_ublock_r, consumer_ublock_c,
                                 consumer_mblock_ublocks_k, consumer_ublock_tiles_k,
                                 consumer_row_major_ublock_scan_order,
                                 resource_estimate[0],
                                 resource_estimate[1],
                                 resource_estimate[2]);
    }
    else if (reblock_type == 1) {
      consumer_ublock_tiles_k = consumer_ublock_r;
      consumer_mblock_ublocks_k = consumer_mblock_r * consumer_grid_r; // randomize full k dimension this way
      tile_map = tm.get_op_matmul_col_input(consumer_t, 
                                            consumer_ublock_tiles_k, consumer_ublock_c,
                                            consumer_mblock_ublocks_k, consumer_mblock_c,
                                            consumer_grid_r, consumer_grid_c
                                            );
      tm.estimate_resource_usage(OpClass::MatMul, 1,
                                 consumer_t,
                                 consumer_grid_r, consumer_grid_c,
                                 consumer_mblock_r, consumer_mblock_c, 
                                 consumer_ublock_r, consumer_ublock_c,
                                 consumer_mblock_ublocks_k, consumer_ublock_tiles_k,
                                 consumer_row_major_ublock_scan_order,
                                 resource_estimate[0],
                                 resource_estimate[1],
                                 resource_estimate[2]);
    }
    else {
      consumer_ublock_tiles_k = 0;
      consumer_mblock_ublocks_k = 0;
      tile_map =  tm.get_op_eltwise_input(consumer_t, 
                                          consumer_ublock_r, consumer_ublock_c,
                                          consumer_mblock_r, consumer_mblock_c,
                                          consumer_grid_r, consumer_grid_c,
                                          consumer_row_major_ublock_scan_order
                                          );
      tm.estimate_resource_usage(OpClass::EltwiseUnary, 0,
                                 consumer_t,
                                 consumer_grid_r, consumer_grid_c,
                                 consumer_mblock_r, consumer_mblock_c, 
                                 consumer_ublock_r, consumer_ublock_c,
                                 consumer_mblock_ublocks_k, consumer_ublock_tiles_k,
                                 consumer_row_major_ublock_scan_order,
                                 resource_estimate[0],
                                 resource_estimate[1],
                                 resource_estimate[2]);
    }

    //tile_map.print();
    resource_analysis[0] = tile_map.max_producer_core_fan_out();
    resource_analysis[1] = tile_map.max_producer_core_phases();
    resource_analysis[2] = tile_map.max_consumer_core_phases();

    std::stringstream consumer_str; 
    consumer_str << "ublock_tiles_r = " << consumer_ublock_r << ", ";
    consumer_str << "ublock_tiles_c = " << consumer_ublock_c << ", ";
    consumer_str << "ublock_tiles_k = " << consumer_ublock_tiles_k << ", ";
    consumer_str << "mblock_ublocks_m = " << consumer_mblock_r << ", ";
    consumer_str << "mblock_ublocks_n = " << consumer_mblock_c << ", ";
    consumer_str << "mblock_ublocks_k = " << consumer_mblock_ublocks_k << ", ";
    consumer_str << "num_cores_r = " << consumer_grid_r << ", ";
    consumer_str << "num_cores_c = " << consumer_grid_c << ", ";
    consumer_str << "t = " << consumer_t << ", ";
    consumer_str << "row_major_ublock_scan_order = " << consumer_row_major_ublock_scan_order;
    printf("Consumer: %s\n", consumer_str.str().c_str());    
    printf("  TM/reblock pipe resources (estimate / analysis):\n");
    bool estimate_error = false;
    bool estimate_pessimistic = false;
    for (int r = 0; r < 3; r++) {
      printf("    %s -> %d / % d\n", resource_name[r].c_str(), resource_estimate[r], resource_analysis[r]);
      if (resource_estimate[r] < resource_analysis[r]) {
        estimate_error = true;
      }
      if ((resource_estimate[r] > resource_max[r]) && (resource_analysis[r] <= resource_max[r])) {
        estimate_pessimistic = true;
      }      
    }
    if (estimate_error) {
      printf ("ERROR: resource estimate too optimistic\n");
    }
    if (estimate_pessimistic) {
      printf ("WARNING: resource estimate too pessimistic\n");
    }
  
  }

  return 0;
}



