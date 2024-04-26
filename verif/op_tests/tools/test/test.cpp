// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <vector>
#include <map>
#include <iostream>
#include <assert.h>
#include <random>
#include <fstream>

using namespace std;

int main()
{
    typedef struct strip_info_struct
    {
       unsigned char strip_index;
       unsigned char nz_ublocks;
       bool last_strip_in_row;
       bool last_strip_in_tile;
       uint8_t index_array[];
    } strip_info_struct;

    strip_info_struct *strip_info_ptr;

   cout << "Size of strip_info_struct is: " << sizeof(strip_info_struct) << endl; 
   cout << "Done." << endl;
}
