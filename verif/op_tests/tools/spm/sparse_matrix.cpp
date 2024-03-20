// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <vector>
#include <map>
#include <set>
#include <iostream>
#include <assert.h>
#include <random>
#include <fstream>
#include <cstdlib>

using namespace std;
std::mt19937 generator;

bool rand_bool()
{
   std::uniform_int_distribution<> disti(0,1);
   return(disti(generator));
}

int rand_int(int min, int max)
{
   std::uniform_int_distribution<> disti(min,max);
   return(disti(generator));
}

float rand_float(float min, float max)
{
   std::uniform_real_distribution<> disti(min,max);
   return(disti(generator));
}

//////
// Sparse matrix building block classes
//////
class tt_encoding_tile
{
   public:
   vector<uint8_t> vals;

   int push_byte_ptr;
   int pop_byte_ptr;

   typedef struct {
      uint32_t index:30;
      uint32_t last_block_in_row:1;
      uint32_t last_block_in_pg_tile:1;
   } block_encoding_t;

   typedef union {
      block_encoding_t f;
      uint32_t val;
   } block_encoding_u;

   void clear()
   {
      push_byte_ptr = 0;
      pop_byte_ptr = 0;
      for(int r=0;r<4096;++r)
      {
            vals[r] = 0;
      }
   }
   void push_byte_val(uint8_t in)
   {
      assert(push_byte_ptr < 4096);
      vals[push_byte_ptr] = in;
      push_byte_ptr++;
   }
   void push_int_val(uint32_t in)
   {
      uint8_t bytes[4];
      bytes[0] =  (uint32_t)in & 0xFF;
      bytes[1] = ((uint32_t)in & 0xFF00) >> 8;
      bytes[2] = ((uint32_t)in & 0xFF0000) >> 16;
      bytes[3] = ((uint32_t)in & 0xFF000000) >> 24;

      // guarantee alignment
      // assert(push_byte_ptr % 4 == 0);
      push_byte_val(bytes[0]);
      push_byte_val(bytes[1]);
      push_byte_val(bytes[2]);
      push_byte_val(bytes[3]);
   }

   void push_int16_val(uint16_t in)
   {
      uint8_t bytes[2];
      bytes[0] =  (int)in & 0xFF;
      bytes[1] = ((int)in & 0xFF00) >> 8;

      // guarantee alignment
      // assert(push_byte_ptr % 4 == 0);
      push_byte_val(bytes[0]);
      push_byte_val(bytes[1]);
   }

   bool pop_equal_byte_val(uint8_t in)
   {
      bool eq = false;
      uint8_t rd_val;
      rd_val = pop_byte_val();
      if(rd_val != in) pop_byte_ptr--;
      else eq = true;
      return eq;
   }

   bool pop_equal_tile_index_val(uint16_t in, uint16_t *tile_ptr, int max_tile_index_width)
   {
      bool eq = false;
      uint16_t rd_val;
      rd_val = pop_int16_val();
      if((rd_val&((1<<max_tile_index_width)-1)) != in) {
         pop_byte_ptr=-2;
      } else {
         eq = true;
         *tile_ptr = rd_val>>max_tile_index_width;
      }   
      return eq;
   }

   bool pop_equal_ublock_index_val(uint16_t in, uint16_t *encoded_num_tiles, int max_tile_ptr_width)
   {
      bool eq = false;
      uint16_t rd_val;
      rd_val = pop_int16_val();
      if((rd_val&((1<<max_tile_ptr_width)-1)) != in) {
         pop_byte_ptr=-2;
      } else {
         eq = true;
         *encoded_num_tiles = rd_val>>max_tile_ptr_width;
      }   
      return eq;
   }

   uint8_t pop_byte_val()
   {
      assert (pop_byte_ptr<push_byte_ptr);
      assert (pop_byte_ptr < 4096);
      uint8_t out;
      out = vals[pop_byte_ptr];
      pop_byte_ptr++;

      return(out);
   }
   bool pop_equal_int_val(uint32_t in)
   {
      bool eq = false;
      uint32_t rd_val;
      rd_val = pop_int_val();
      if(in != rd_val) pop_byte_ptr = pop_byte_ptr - 4;
      else eq = true;
      return eq;
   }
   uint32_t pop_int_val()
   {
      uint8_t bytes[4];
      uint32_t out;
      bytes[0] = pop_byte_val();
      bytes[1] = pop_byte_val();
      bytes[2] = pop_byte_val();
      bytes[3] = pop_byte_val();
      out = bytes[0] | ((bytes[1] &0xff) << 8) | ((bytes[2] &0xff) << 16) | ((bytes[3] &0xff) << 24);
      return(out);
   }

   bool pop_equal_int16_val(uint16_t in)
   {
      bool eq = false;
      uint16_t rd_val;
      rd_val = pop_int16_val();
      if(in != rd_val) pop_byte_ptr = pop_byte_ptr - 2;
      else eq = true;
      return eq;
   }

   bool pop_equal_block_index_val(uint32_t in,uint8_t *last_in_row_flag_seen,  uint8_t *last_in_encoding_tile_flag)
   {
      bool eq = false;
      block_encoding_u block_encoding;  
      block_encoding.val  = pop_int_val();
      if(in != block_encoding.f.index) pop_byte_ptr = pop_byte_ptr - 4;
      else { 
         *last_in_row_flag_seen = (uint8_t)block_encoding.f.last_block_in_row;
         *last_in_encoding_tile_flag = (uint8_t)block_encoding.f.last_block_in_pg_tile;
         eq = true;
      }   
      return eq;
   }

   uint16_t pop_int16_val()
   {
      uint8_t bytes[2];
      uint16_t out;
      bytes[0] = pop_byte_val();
      bytes[1] = pop_byte_val();
      out = bytes[0] | ((bytes[1] &0xff) << 8);
      return(out);
   }
   tt_encoding_tile()
   {
      push_byte_ptr = 0;
      pop_byte_ptr = 0;
      for(int r=0;r<4096;++r)
      {
         vals.push_back(0);
      }
   }
};

class tt_spm_tile
{
   public:
   vector<vector <float> > vals;
   bool zero;

   bool is_tile_zero()
   {
      return zero;
   }
   bool is_equal(tt_spm_tile &rhs)
   {
      bool equal = true;
      for(int r=0;r<32;++r)
      {
         for(int c=0;c<32;++c)
         {
            if(vals[r][c] != rhs.vals[r][c]) equal = false;
         }
      }
      return equal;
   }

   tt_spm_tile()
   {
      zero = true;
      for(int r=0;r<32;++r)
      {
         vector<float> tmp;
         for(int c=0;c<32;++c)
         {
            float num = 0.0;
            tmp.push_back(num);
         }
         vals.push_back(tmp);
      }
   }

   tt_spm_tile(int mb_rows, int mb_columns, int ub_rows, int ub_columns,
               int block_r, int block_c, int mb_r, int mb_c, int tr, int tc,
               vector<vector <float> > &in_mat)
   {
      zero = true;
      // initialize the tile
      // and allocate its storage

      for(int r=0;r<32;++r)
      {
         vector<float> tmp;
         for(int c=0;c<32;++c)
         {
            float num;
            int r_index = block_r*(mb_rows*ub_rows*32) + mb_r*(ub_rows*32) + tr*32 + r;
            int c_index = block_c*(mb_columns*ub_columns*32) + mb_c*(ub_columns*32) + tc*32 + c;
            num = in_mat[r_index][c_index];

            if(num != 0.0) zero = false;
            tmp.push_back(num);
         }
         vals.push_back(tmp);
      }
   }

};

class tt_spm_ublock
{
   public:
   int index;
   int num_tiles;

   vector<int> tile_index_list;
   vector<int> tile_ptr_list;

   bool is_zero()
   {
      return (num_tiles == 0);
   }

   int get_number_of_tiles()
   {
      return(num_tiles);
   }

   tt_spm_ublock() {index = 0; num_tiles = 0;};
};

class tt_spm_block
{
   public:
   uint32_t index;
   int last_block_in_row;
   int last_block_in_pg_tile;

   vector<tt_spm_ublock> ublock_list;

   int number_of_ublocks;
   int bytes_in_struct; // just for debug visibility, not used elsewhere
   public:
   bool is_zero()
   {
      return (number_of_ublocks == 0);
   }
   void clear()
   {
      index =0;
      last_block_in_pg_tile = 0;
      last_block_in_row = 0;
      ublock_list.clear();

      number_of_ublocks = 0;
   }

   int get_bytes_in_struct()
   {
      // block index ::: tile store pointer ::: number_of_ublocks ::: last block in row ::: last block in pipegen tile ::: then ublock and tile index data
      int size = 4 + 4 + 2 + (number_of_ublocks*2) + (2*total_data_tiles_in_block());
      bytes_in_struct = size;
      return(size);
   }

   int total_data_tiles_in_block()
   {
      int tile_count = 0;
      for(auto ublock : ublock_list)
      {
         tile_count += ublock.get_number_of_tiles();
      }
      return(tile_count);
   }

   void add_ublock(tt_spm_ublock &ublock)
   {
      ublock_list.push_back(ublock);
      number_of_ublocks++;
      assert(number_of_ublocks <= 256);
   }

   tt_spm_block()
   {  
      this->clear();
   }
};


//////
// Dense /sparse matrix class
//////
class tt_matrix
{
   bool ublock_scan_order;
   int rm_rows;
   int rm_columns;
   int block_rows;
   int block_columns;
   int mb_rows;
   int mb_columns;
   int ub_rows;
   int ub_columns;

   vector<vector <float> > vals; // row major values

   // globals
   int current_block_index;
   int current_ublock_index;

   tt_spm_block tmp_block;
   vector< vector <tt_spm_block> > blocks;
   vector< vector <tt_spm_tile> > tiles;

   vector< vector <tt_spm_tile> > unique_tiles;
   vector< map<int,int> >tile_index_map;

   vector< vector <tt_encoding_tile> > encoding_tiles;

   // Debugging structures to dump more information
   set<int> zero_tiles;
   map<int, int> non_zero_tiles;

   public:
   // test functions can access internal data
   friend bool validate_sparse(bool ublock_scan_order, tt_matrix &sparse_in);
   friend bool validate_sparse_tiled(bool ublock_scan_order, tt_matrix &sparse_in, const int max_tile_index_width, const int max_tile_ptr_width);
   friend bool validate_sparse_binary(bool ublock_scan_order, tt_matrix &sparse_in);

   tt_matrix(bool ublock_scan_order, int block_rows, int block_columns, int mb_rows, int mb_columns, int ub_rows, int ub_columns)
   {
      this->ublock_scan_order = ublock_scan_order;
      this->block_rows    = block_rows   ;
      this->block_columns = block_columns;
      this->mb_rows       = mb_rows      ;
      this->mb_columns    = mb_columns   ;
      this->ub_rows       = ub_rows      ;
      this->ub_columns    = ub_columns   ;

      rm_rows       = block_rows*mb_rows*ub_rows*32;
      rm_columns    = block_columns*mb_columns*ub_columns*32;
   }

   void dump_debug_information () {
      // Make sure the total number of tiles match 
   
      cout << zero_tiles.size() << " Zero Tiles: " << endl;
      for (const auto& zero_tile_index: zero_tiles) {
         cout << "W" << zero_tile_index << " ";
      }
      cout << endl << "End of Zero Tiles " << endl;
      cout << non_zero_tiles.size() << " Non Zero Tiles: " << endl;
      for (const auto& non_zero_tile_it: non_zero_tiles) {
         int non_zero_tile_index = non_zero_tile_it.first;
         int sparse_tile_index = non_zero_tile_it.second;
         cout << "W" << non_zero_tile_index << "-->S" << sparse_tile_index << endl;
      }
      cout << "End of Non Zero Tiles " << endl;
      cout  << "block_rows   : " << block_rows    << endl
            << "block_columns: " << block_columns << endl
            << "mb_rows      : " << mb_rows       << endl
            << "mb_columns   : " << mb_columns    << endl
            << "ub_rows      : " << ub_rows       << endl
            << "ub_columns   : " << ub_columns    << endl;      
      assert ((zero_tiles.size() + non_zero_tiles.size()) == (ub_columns*mb_columns*mb_rows*block_columns*block_rows));
   }

   bool too_many_tiles()
   {
      bool too_many = false;

      for(auto unique_tile_row : unique_tiles)
      {
         if(unique_tile_row.size() > 256) too_many = true;
      }
      return too_many;
   }
   void info(   )
   {
      cout << "Non-unique tiles: " << tiles[0].size() << "  Unique tiles: " << unique_tiles[0].size() << endl;
      cout << rm_rows << " " << rm_columns << " " << block_rows << " " << block_columns << " " << mb_rows << " " << mb_columns << " " << ub_rows << " " << ub_columns << endl;
      info_bytes_in_block_row();
   }
   void info_bytes_in_block_row()
   {
      for(auto block_row : blocks)
      {
         int row_size = 0;
         cout << "Block row size: ";
         for(auto block : block_row)
         {
            row_size += block.get_bytes_in_struct();
         }
         cout << row_size << " " << endl;
      }
   }
   void init_identity()
   {
      assert(rm_rows == rm_columns);

      float num;

      // initialize the tile
      // and allocate its storage
      for(int i=0;i<rm_rows;++i)
      {
         vector<float> tmp;
         for(int j=0;j<rm_columns;++j)
         {
            if(i == j) num = 1.0;//rand_float(0,10);
            else num = 0.0;
            tmp.push_back(num);
         }
         vals.push_back(tmp);
      }
   }

   void init_shift_matrix(int x_shift, int y_shift)
   {
      assert(rm_rows == rm_columns);
      int start_line;
      int end_line;
      float num;
      if(y_shift > 0)
      {
         start_line = y_shift;
      }
      else
      {
         start_line = 0;
      }

      if(y_shift < 0)
      {
         end_line = rm_rows - y_shift;
      }
      else
      {
         end_line = rm_rows;
      }

      if(y_shift > 0)
      {
         for(int i=0;i<y_shift;i=++i)
         {
            vector<float> tmp;
            for(int j=0;j<rm_columns;++j)
            {
               tmp.push_back(0.0);
            }
            vals.push_back(tmp);
         }
      }
      for(int i=y_shift;i<end_line;i=++i)
      {
         vector<float> tmp;
         for(int j=0;j<rm_columns;++j)
         {
            if(x_shift > 0)
            {
               if(i == (j+x_shift)) num = 1.0;
               else num = 0.0;
               tmp.push_back(num);
            }
            else if(x_shift < 0)
            {
               if((i+x_shift) == j) num = 1.0;
               else num = 0.0;
               tmp.push_back(num);
            }
            else
            {
               if(i == j) num = 1.0;
               else num = 0.0;
               tmp.push_back(num);
            }
         }
         vals.push_back(tmp);
      }
      if(y_shift < 0)
      {
         for(int i=0;i<y_shift;i=++i)
         {
            vector<float> tmp;
            for(int j=0;j<rm_columns;++j)
            {
               tmp.push_back(0.0);
            }
            vals.push_back(tmp);
         }
      }
   }

   void init_one_per_tile(float num)
   {
      for(int i=0;i<rm_rows;i++)
      {
         vector<float> tmp;
         for(int j=0;j<rm_columns;++j)
         {
            if((i % 32 == 0) && (j % 32 == 0)) tmp.push_back(num);
            else tmp.push_back(0.0);
         }
         vals.push_back(tmp);
      }
   }

   void init_one_per_tile()
   {
      for(int i=0;i<rm_rows;i++)
      {
         vector<float> tmp;
         for(int j=0;j<rm_columns;++j)
         {
            if((i % 32 == 0) && (j % 32 == 0)) tmp.push_back(rand_float(0,10));
            else tmp.push_back(0.0);
         }
         vals.push_back(tmp);
      }
   }

   void init_fill_one_block()
   {
      int r_block_bound = mb_rows * ub_rows * 32;
      int c_block_bound = mb_columns * ub_columns * 32;
      int r_ublock_bound = ub_rows * 32;
      int c_ublock_bound = ub_columns * 32;

      bool filled_one_already = false;
      bool fill_this_block = false;
      for(int i=0;i<rm_rows;i=++i)
      {
         vector<float> tmp;
         for(int j=0;j<rm_columns;++j)
         {
            if((i % r_block_bound == 0) && (j % c_block_bound == 0))
            {
               if(!fill_this_block)
               {
                  fill_this_block = rand_bool();
                  filled_one_already = false;
               }
               if(fill_this_block) filled_one_already = true;
            }
            if(fill_this_block && (!filled_one_already)) tmp.push_back(rand_float(0,10));
            else tmp.push_back(0.0);
         }
         vals.push_back(tmp);
      }
   }

   void init_fill_one_random_ublock_per_block()
   {
      int r_block_bound = mb_rows * ub_rows * 32;
      int c_block_bound = mb_columns * ub_columns * 32;
      int r_ublock_bound = ub_rows * 32;
      int c_ublock_bound = ub_columns * 32;

      bool filled_one_already = false;
      bool fill_this_ublock = false;
      for(int i=0;i<rm_rows;i=++i)
      {
         vector<float> tmp;
         for(int j=0;j<rm_columns;++j)
         {
            if((i % r_block_bound == 0) && (j % c_block_bound == 0))
            {
               fill_this_ublock = false;
               filled_one_already = false;
            }
            if((i % r_ublock_bound == 0) && (j % c_ublock_bound == 0))
            {
               if((!fill_this_ublock) && (!filled_one_already) && rand_bool()) fill_this_ublock = true;
               else if((fill_this_ublock) && (!filled_one_already)) filled_one_already = true;
            }
            if(fill_this_ublock && (!filled_one_already)) tmp.push_back(rand_float(0,10));
            else tmp.push_back(0.0);
         }
         vals.push_back(tmp);
      }
   }

   void init_random()
   {
      // initialize matrix to zero
      for(int i=0;i<rm_rows;++i)
      {
         vector<float> tmp;
         for(int j=0;j<rm_columns;++j)
         {
            tmp.push_back(0.0);
         }
         vals.push_back(tmp);
      }

      // generate number of 1's
      int number = rand_int(100,150);
      for(int i=0;i<number;++i)
      {
         // generate random co-ordinates within matrix
         int r = rand_int(0,rm_rows-1);
         int c = rand_int(0,rm_columns-1);
         float num = 1.0;//rand_float(1,10);
         vals[r][c] = num;
      }
   }

   void randomize()
   {
      // initialize matrix to zero
      for(int i=0;i<rm_rows;++i)
      {
         vector<float> tmp;
         for(int j=0;j<rm_columns;++j)
         {
            tmp.push_back(rand_float(-1.0, 1.0));
         }
         vals.push_back(tmp);
      }
   }

   void zeroit()
   {
      // initialize matrix to zero
      for(int i=0;i<rm_rows;++i)
      {
         vector<float> tmp;
         for(int j=0;j<rm_columns;++j)
         {
            tmp.push_back(0.0f);
         }
         vals.push_back(tmp);
      }
   }

   void matmul(tt_matrix *in0, tt_matrix *in1)
   {
      assert(in0->rm_columns == in1->rm_rows);
      assert(in0->rm_rows == rm_rows);
      assert(in1->rm_columns == rm_columns);

      // initialize matrix to zero
      for(int i=0;i<rm_rows;++i)
      {
         for(int j=0;j<rm_columns;++j)
         {
            for (int k=0; k<in0->rm_columns; k++) {
                  vals[i][j] = vals[i][j] + in0->vals[i][k]*in1->vals[k][j];
            }
         }
      }
   }

   void build_spm(int number_of_tensix_rows, bool dump_debug_info=true, int num_encodings_per_tile=4096)
   {
      // Clear previously held data
      blocks.clear();
      tiles.clear();

      // Traverse row of blocks
      for(int br=0; br < block_rows; ++br)
      {
         // Add row container to 'blocks' and 'tiles' vectors
         blocks.push_back(vector<tt_spm_block>());
         tiles.push_back(vector<tt_spm_tile>());

         // block indices are unique per row of blocks, so reset block index here
         current_block_index = 0;
         // Traverse blocks in each row
         int bc=0;
         for(bc=0; bc < block_columns; ++bc)
         {
            encode_spm_block(br, bc);
         }

         // Check if row is empty and put in placeholder empty block
         if(blocks[br].size() == 0)
         {
            tmp_block.clear();
            tmp_block.index = 0;
            tmp_block.number_of_ublocks = 0;
            blocks[br].push_back(tmp_block);
         }

         // Mark last block in row
         blocks[br].back().last_block_in_row = 1;
      }
      uniquify_tiles();
      combine_core_rows(number_of_tensix_rows);
      mark_last_in_pipegen_tile(num_encodings_per_tile);
      if (dump_debug_info) {
         dump_debug_information();
      }	 
      //gen_tiled_encodings();
   }
   private:
   void uniquify_tiles()
   {
      // go through all tiles and make list of unique tiles
      // first tile just goes in there
      // every subsequent tile needs to be compared to whole list
      // to determine uniqueness
      // mappings of old to new tile pointers are kept as the process
      // proceeds
      //
      // at the end, all tile pointers are updated to new ones
      // tile indexing rules will be relaxed whole initially building the SPM
      // really there must be no more than 256 tiles *AFTER* uniquification

      for(int i=0; i<tiles.size(); ++i)
      {
         unique_tiles.push_back(vector<tt_spm_tile>());
         tile_index_map.push_back(map<int,int>());
         int old_index = 0;
         for(tt_spm_tile &in_tile : tiles[i])
         {
            bool found_equal = false;
            int new_index = 0;
            for(tt_spm_tile &out_tile: unique_tiles[i])
            {
               if(in_tile.is_equal(out_tile))
               {
                  assert(unique_tiles[i].size() != 0);
                  assert(found_equal == false);
                  found_equal = true;
                  tile_index_map[i][old_index] = new_index;
               }
               new_index++;
            }
            if((!found_equal))// || (unique_tiles[i].size()==0))
            {
               // could delete the tile from original list...
               unique_tiles[i].push_back(in_tile);
               tile_index_map[i][old_index] = unique_tiles[i].size() - 1;
            }
            old_index++;
         }
      }

      int flat_index = 0;
      int relative_index = 0;
      int i = 0;
      for (auto& non_zero_tile : non_zero_tiles) {
         if (i < tile_index_map.size()) {
            if (relative_index >= tile_index_map[i].size()) {
               relative_index = 0;
               i++;
            }
            if (tile_index_map[i].find(relative_index) != tile_index_map[i].end()) {
               non_zero_tiles.at(non_zero_tile.first) = tile_index_map[i].at(relative_index);
            }
         }
         relative_index++;
         flat_index++;
      }

      // go through all structures and update tile indices
      int block_row_index=0;
      for(block_row_index=0; block_row_index < blocks.size(); ++block_row_index)
      {
         std::vector<tt_spm_block>::iterator block_ptr_iter;
         for(block_ptr_iter=blocks[block_row_index].begin();block_ptr_iter!=blocks[block_row_index].end();++block_ptr_iter)
         {
            std::vector<tt_spm_ublock>::iterator ublock_ptr_iter;
            for(ublock_ptr_iter=block_ptr_iter->ublock_list.begin(); ublock_ptr_iter!=block_ptr_iter->ublock_list.end(); ++ublock_ptr_iter)
            {
               std::vector<int>::iterator tile_ptr_iter;
               for(tile_ptr_iter=ublock_ptr_iter->tile_ptr_list.begin(); tile_ptr_iter!=ublock_ptr_iter->tile_ptr_list.end(); ++tile_ptr_iter)
               {
                  int new_index = tile_index_map[block_row_index][(*tile_ptr_iter)];
                  //cout << block_row_index << " " << new_index << " " << (*tile_ptr_iter) << endl;
                  (*tile_ptr_iter) = new_index;
               }
            }
         }
      }
   }

   void encode_spm_block(int block_r, int block_c)
   {
      tmp_block.clear();
      // write in block index, tile store pointer
      tmp_block.index = current_block_index;

      current_ublock_index = 0; // ublock indexes are unique per block

      // go through ublocks in block and encode each
      if(ublock_scan_order)
      {
         for(int mc=0;mc<mb_columns;++mc)
         {
            for(int mr=0;mr<mb_rows;++mr)
            {
               // write in ublock index
               encode_spm_ublock(block_r, block_c, mr, mc);
            }
         }

      }
      else
      {
         for(int mr=0;mr<mb_rows;++mr)
         {
            for(int mc=0;mc<mb_columns;++mc)
            {
               // write in ublock index
               encode_spm_ublock(block_r, block_c, mr, mc);
            }
         }
      }

      assert(current_ublock_index < 257);

      // if non-zero, push completed temp block into block vector
//      if(!tmp_block.is_zero()) blocks[block_r].push_back(tmp_block);
// kovski asked for temporary hack that puts in zero/empty blocks into encoding
blocks[block_r].push_back(tmp_block);
      // index increments either way
      current_block_index++;
   }
   void encode_spm_ublock(int block_r, int block_c, int mb_r, int mb_c)
   {
      int current_tile_index = 0;
      int tile_count = 0;

      // make a temporary ublock and populate it
      tt_spm_ublock tmp_ublock;
   
      // go through tiles in ublock and encode each
      for(int tr=0;tr<ub_rows;++tr)
      {
         for(int tc=0;tc<ub_columns;++tc)
         {
            tt_spm_tile tmp_tile(mb_rows, mb_columns, ub_rows, ub_columns, block_r, block_c, mb_r, mb_c, tr, tc, vals);
            int final_r = block_r * mb_rows * ub_rows + mb_r * ub_rows + tr;
            int final_c = block_c * mb_columns * ub_columns + mb_c * ub_columns + tc;
            int row_major_index = final_r * block_columns * mb_columns*ub_columns + final_c;
            if(!tmp_tile.is_tile_zero())
            {
               // add tile to the rows tile store
               tiles[block_r].push_back(tmp_tile);

               // check that tile index and pointer fit in a byte each and add them to struct
               // assert(current_tile_index < 256);
               // assert((tiles[block_r].size() -1) < 256);
               //tmp_ublock.tile_index_list.push_back(current_tile_index);//acejkov change encoding for perf
               tmp_ublock.tile_index_list.push_back((tr<<16) | (tc&0xffff) );
               tmp_ublock.tile_ptr_list.push_back(tiles[block_r].size()-1);

               tile_count++;
               non_zero_tiles.insert({row_major_index, -1});
            } else {
               zero_tiles.insert({row_major_index});
            }
            current_tile_index++;
         }
      }
      tmp_ublock.index = current_ublock_index;
      tmp_ublock.num_tiles = tile_count;

      // if non-zero - add ublock struct to blocks ublock list
      if(!tmp_ublock.is_zero()) tmp_block.add_ublock(tmp_ublock);

      // index increments either way
      current_ublock_index++;
   }

   void mark_last_in_pipegen_tile(int num_encodings_per_tile=4096)
   {
      bool first = true;
      tt_spm_block *last_block = NULL;
      // mark last in tile blocks
      int block_row_index=0;
      for(block_row_index=0; block_row_index < blocks.size(); ++block_row_index)
      {
         std::vector<tt_spm_block>::iterator block_ptr_iter;
         int current_tile_bytes_used = 0;
         for(block_ptr_iter=blocks[block_row_index].begin();block_ptr_iter!=blocks[block_row_index].end();++block_ptr_iter)
         {
            int block_size = block_ptr_iter->get_bytes_in_struct();
            if((current_tile_bytes_used + block_size) > num_encodings_per_tile)
            {
               // mark previous block as last in pipegen tile
               // reset to tile beginning
               last_block->last_block_in_pg_tile = 1;
               current_tile_bytes_used = block_size;
            }
            else
            {
               current_tile_bytes_used += block_size;
            }
            if(block_ptr_iter->last_block_in_row) block_ptr_iter->last_block_in_pg_tile = 1;
            last_block = &(*block_ptr_iter);
         }
      }
   }

   void equalize_tile_row_sizes(bool pad_enc_tiles = false, bool pad_sparse_tiles = false, int num_encoding_tiles = 1, int num_sparse_tiles = 1)
   {
      int max_dt_size = 0;
      int max_enc_size = 0;
      // get max data tile row size
      for(auto data_tile_row : unique_tiles)
      {
         if(data_tile_row.size()>max_dt_size) max_dt_size = data_tile_row.size();
      }

      // get max encoding row size
      for(auto enc_tile_row : encoding_tiles)
      {
         if(enc_tile_row.size()>max_enc_size) max_enc_size = enc_tile_row.size();
      }
       
      // go through data rows and pad pad to max with zero tiles
      for(int data_tile_row=0; data_tile_row<unique_tiles.size(); data_tile_row++)
      {

         int data_tile_size = unique_tiles[data_tile_row].size();
         for(int i=0; i < (max_dt_size - data_tile_size);++i)
         {
            unique_tiles[data_tile_row].push_back(tt_spm_tile()); // create a tile filled with zeros and append to row
         }
      }

      // go through encoding rows and pad to max with zero tiles
      for(int enc_tile_row=0; enc_tile_row<encoding_tiles.size(); enc_tile_row++)
      {
         int enc_tile_size = encoding_tiles[enc_tile_row].size();
         for(int i=0; i < (max_enc_size - enc_tile_size);++i)
         {
            encoding_tiles[enc_tile_row].push_back(tt_encoding_tile()); // create a tile filled with zeros and append to row
         }
      }

      // check this all worked
      for(auto data_tile_row : unique_tiles)
      {
         assert(data_tile_row.size()==max_dt_size);
      }

      // get max encoding row size
      for(auto enc_tile_row : encoding_tiles)
      {
         assert(enc_tile_row.size()==max_enc_size);
      }

      if (pad_sparse_tiles) {
         assert(num_sparse_tiles>=max_dt_size);
         for(int data_tile_row=0; data_tile_row<unique_tiles.size(); data_tile_row++)
         {
            for(int i=0; i < (num_sparse_tiles - max_dt_size);++i)
            {
               unique_tiles[data_tile_row].push_back(tt_spm_tile()); // create a tile filled with zeros and append to row
            }
         }
      }   

      if (pad_enc_tiles) {
         cout << "num_encoding_tiles: " << num_encoding_tiles << " max_enc_size: " << max_enc_size << endl;
         assert(num_encoding_tiles>=max_enc_size);
         // go through encoding rows and pad to max with zero tiles
         for(int enc_tile_row=0; enc_tile_row<encoding_tiles.size(); enc_tile_row++)
         {
            for(int i=0; i < (num_encoding_tiles - max_enc_size);++i)
            {
               encoding_tiles[enc_tile_row].push_back(tt_encoding_tile()); // create a tile filled with zeros and append to row
            }
         }
      } 

   }

   public:
   void gen_tiled_encodings(const int max_sparse_tile_index_width=6, const int max_sparse_tile_ptr_width=10, const int tile_col_ptr_width=5)
   {
      tt_encoding_tile tmp_tile;
      int br=0;
      const int max_sparse_ublock_index_width = max_sparse_tile_ptr_width;
      assert((max_sparse_tile_index_width+max_sparse_tile_ptr_width) <= 16);
      for(auto block_row : blocks)
      {
         encoding_tiles.push_back(vector<tt_encoding_tile>());
         for(auto block : block_row)
         {
            tt_encoding_tile::block_encoding_u block_encoding;
            block_encoding.val = 0;
            block_encoding.f.index = block.index;
            block_encoding.f.last_block_in_row = block.last_block_in_row;
            block_encoding.f.last_block_in_pg_tile = block.last_block_in_pg_tile;
            tmp_tile.push_int_val(block_encoding.val);
            tmp_tile.push_int16_val((uint16_t)block.number_of_ublocks);
            for(auto ublock : block.ublock_list)
            {
               assert(ublock.tile_index_list.size() == ublock.tile_ptr_list.size());
               assert(ublock.index < (int) pow(2, (float) max_sparse_ublock_index_width)); // ublock_index_width == sparse_tile_ptr_width
               assert(ublock.num_tiles <= (int) pow(2, (float) max_sparse_tile_index_width)); // num_tiles_width == sparse_tile_index_width
               int num_tiles = (ublock.num_tiles == (1<<max_sparse_tile_index_width)) ? 0 : ublock.num_tiles; // special encoding for max num tiles
               uint16_t tmp = (ublock.index & ((1<<max_sparse_tile_ptr_width)-1)) | (num_tiles << max_sparse_tile_ptr_width);
               tmp_tile.push_int16_val(tmp);
               for(int tile_ind=0; tile_ind < ublock.tile_index_list.size(); ++tile_ind)
               {
                  //assert(ublock.tile_index_list[tile_ind] < max_tile_index);
                  //assert(ublock.tile_index_list[tile_ind] < (int) pow(2, (float) max_tile_index_width));
                  assert((ublock.tile_index_list[tile_ind] >> 16) < (int) pow(2, (float) (max_sparse_tile_index_width-tile_col_ptr_width)));
                  assert((ublock.tile_index_list[tile_ind] & 0xffff) < (int) pow(2, (float) (tile_col_ptr_width)));
                  //tmp_tile.push_byte_val(ublock.tile_index_list[tile_ind]);
                  assert(ublock.tile_ptr_list[tile_ind] < (int) pow(2, (float) max_sparse_tile_ptr_width));
                  //tmp_tile.push_byte_val(ublock.tile_ptr_list[tile_ind]);
                  uint16_t tile_index = ((ublock.tile_index_list[tile_ind]>>16)<<tile_col_ptr_width) | (ublock.tile_index_list[tile_ind] & ((1<<tile_col_ptr_width)-1));
                  uint16_t tmp = (tile_index & ((1<<max_sparse_tile_index_width)-1)) | (ublock.tile_ptr_list[tile_ind] << max_sparse_tile_index_width);
                  tmp_tile.push_int16_val(tmp);

               }
            }
            if(block.last_block_in_pg_tile)
            {
               encoding_tiles[br].push_back(tmp_tile);
               tmp_tile.clear();
            }
         }
         br++;
      }
   }

   void combine_core_rows(int tensix_core_rows)
   {
      int rows_per_core = block_rows / tensix_core_rows;
      for(int core_row=0; core_row < tensix_core_rows; ++core_row)
      {
         int l_t_ptr=0;
         for(int rpc=0; rpc < rows_per_core; ++rpc)
         {
            // adjust tile pointers
            l_t_ptr = adjust_tile_ptrs(l_t_ptr, blocks[core_row + (rpc*tensix_core_rows)]);
            if(rpc > 0)
            {
               // concat block rows
               blocks[core_row].insert(blocks[core_row].end(), blocks[core_row + (rpc*tensix_core_rows)].begin(), blocks[core_row + (rpc*tensix_core_rows)].end());
               // concat unique tile rows
               unique_tiles[core_row].insert(unique_tiles[core_row].end(), unique_tiles[core_row + (rpc*tensix_core_rows)].begin(), unique_tiles[core_row + (rpc*tensix_core_rows)].end());
               // concat  tile rows
               tiles[core_row].insert(tiles[core_row].end(), tiles[core_row + (rpc*tensix_core_rows)].begin(), tiles[core_row + (rpc*tensix_core_rows)].end());
            }
         }
      }

      // delete un-neccessary rows
      assert(blocks.size() == unique_tiles.size());
      for(int row = tensix_core_rows; row < blocks.size(); ++row)
      {
         blocks.erase(blocks.end()-1);
         unique_tiles.erase(unique_tiles.end()-1);
         tiles.erase(tiles.end()-1);
      }
   }

   int adjust_tile_ptrs(int ptr_offset, vector<tt_spm_block> &in_block_row)
   {
      int last_tile_ptr=0;
      for(auto block : in_block_row)
      {
         for(auto ublock : block.ublock_list)
         {
            for(int i=0; i < ublock.tile_ptr_list.size(); ++i)
            {
               ublock.tile_ptr_list[i] = ublock.tile_ptr_list[i] + ptr_offset;
               last_tile_ptr  = ublock.tile_ptr_list[i];
            }
         }
      }
      return(last_tile_ptr);
   }

   void dump_spm(string spm_encoding_file_name = "spm_encoding.dat", string spm_data_file_name = "spm_data.dat", bool pad_enc_tiles = false, bool pad_sparse_tiles = false, int num_encoding_tiles = 1, int num_sparse_tiles = 1, int num_encodings_per_tile=4096)
   {
      // dump binary file with encoding tiles and another binary file with unique data tiles
      // files are pre-tilized (if they go through the tilizer the data will be correct for consumption)
      ofstream out_encoding;
      ofstream out_data;
      ofstream in_data;
      out_encoding.open(spm_encoding_file_name, std::ios::out | std::ios::binary);
      out_data.open(spm_data_file_name, std::ios::out | std::ios::binary);
      
      // equalize tile rows
      equalize_tile_row_sizes(pad_enc_tiles, pad_sparse_tiles, num_encoding_tiles, num_sparse_tiles);

      // This assumes equal size tile rows
      for(auto tile_row : unique_tiles)
      {
         for(int r=0;r<32;++r)
         {
            for(auto tile : tile_row)
            {
               for(int c=0;c<32;++c)
               {
                  float num = tile.vals[r][c];
                  out_data.write( reinterpret_cast<const char*>( &num ), sizeof( float ));
               }
            }   
         }
      }

      // this assumes equal size tile rows
      for(auto encoding_tile_row : encoding_tiles)
      {
         for(auto encoding_tile : encoding_tile_row)
         {
            for(int r=0;r<1024;++r)
            {
               uint32_t num=0;
               uint32_t bytes_per_datum = num_encodings_per_tile/1024;
               for (int b=0;b<bytes_per_datum;b++) {
                  // double check on change below
                  num |= (encoding_tile.vals[bytes_per_datum*r+b] << (8*b));
               }
               out_encoding.write( reinterpret_cast<const char*>( &num ), sizeof( uint32_t ));
            }
         }
         // Pad with zeros - FIMXE: remove for byte data
         // for(int n=0;n<768;++n){
         //    uint32_t num=0;
         //    out_encoding.write( reinterpret_cast<const char*>( &num ), sizeof( uint32_t ));
         // }
      }

      out_encoding.close();
      out_data.close();
   }

   void dump_raw(string file_name = "input0.0.bin")
   {
      ofstream in_data;
      in_data.open(file_name, std::ios::out | std::ios::binary);
      
      for(int r=0;r<block_rows*mb_rows*ub_rows*32;++r)
      {
         for(int c=0;c<block_columns*mb_columns*ub_columns*32;++c)
         {
            
            float num = vals[r][c];
            in_data.write( reinterpret_cast<const char*>( &num ), sizeof( float ));
         }
      }

      in_data.close();
   }

   void dumo_dm()
   {
      ofstream out;
      out.open("dm.dat", std::ios::out | std::ios::binary);

      for(auto r : vals)
      {
         for(auto val : r)
         {
            out.write( reinterpret_cast<const char*>( &val ), sizeof( float ));
         }
      }
      out.close();
   }
};


////////
// Binary dump function

// Tile data is freely available in tiles
// tile up the encoding data

// dump the tiles


////////
// Validator function - reconstructs a dense matrix from the sparse info
bool validate_sparse(bool ublock_scan_order, tt_matrix &sparse_in)
{
   bool pass = true;
   // construct a matrix full of zeros to begin with
   tt_matrix new_matrix(ublock_scan_order,sparse_in.block_rows, sparse_in.block_columns, sparse_in.mb_rows, sparse_in.mb_columns, sparse_in.ub_rows, sparse_in.ub_columns);
   for(int i=0;i<sparse_in.rm_rows;++i)
   {
      vector<float> tmp;
      for(int j=0;j<sparse_in.rm_columns;++j)
      {
         tmp.push_back(sparse_in.vals[i][j]);
      }
      new_matrix.vals.push_back(tmp);
   }

   // go through rows of blocks, ublocks, tiles and copy tile data over the zero initialized new matrix
   int block_index = 0;
   int ublock_index = 0;
   int tile_index = 0;

   int block_ptr = 0;
   int ublock_ptr = 0;
   int tile_ptr = 0;
   int last_in_row_flag = 0;
   bool seen_a_one = false;
   for(int br=0; br < sparse_in.block_rows; ++br)
   {
      block_index = 0;
      block_ptr = 0;
      last_in_row_flag = 0;
      for(int bc=0; bc < sparse_in.block_columns; ++bc)
      {
         // if block exists and is not all zero
         if((sparse_in.blocks[br][block_ptr].index == block_index) && (!sparse_in.blocks[br][block_ptr].is_zero()) && (last_in_row_flag == 0))
         {
            ublock_index = 0;
            ublock_ptr = 0;
            last_in_row_flag = sparse_in.blocks[br][block_ptr].last_block_in_row;


            int inner = 0;
            int outer = 0;
            int inner_limit = 0;
            int outer_limit = 0;
            if(sparse_in.ublock_scan_order)
            {
               inner_limit = sparse_in.mb_rows;
               outer_limit = sparse_in.mb_columns;
            }
            else
            {
               inner_limit = sparse_in.mb_columns;
               outer_limit = sparse_in.mb_rows;
            }

            for(outer=0; outer < outer_limit; ++outer)
            {
               for(inner=0; inner < inner_limit; ++inner)
               {
                  int mbr;
                  int mbc;
                  if(sparse_in.ublock_scan_order)
                  {
                     mbr = inner;
                     mbc = outer;
                  }
                  else
                  {
                     mbr = outer;
                     mbc = inner;
                  }
                  // if ublock index matches, and we have not accidentally over-run the list of ublocks
                  assert(ublock_ptr < 256);
                  if(ublock_ptr < sparse_in.blocks[br][block_ptr].number_of_ublocks)
                  {
                     if(sparse_in.blocks[br][block_ptr].ublock_list[ublock_ptr].index == ublock_index)
                     {
                        tile_index = 0;
                        tile_ptr = 0;
                        for(int ubr=0; ubr < sparse_in.ub_rows; ++ubr)
                        {
                           for(int ubc=0; ubc < sparse_in.ub_columns; ++ubc)
                           {
                              // if tile exists and we have not accidentally overrun the list of tiles for given ublock
                              assert(tile_ptr < 256);
                              if(tile_ptr < sparse_in.blocks[br][block_ptr].ublock_list[ublock_ptr].num_tiles)
                              {
                                 if(sparse_in.blocks[br][block_ptr].ublock_list[ublock_ptr].tile_index_list[tile_ptr] == tile_index)
                                 {
                                    for(int r=0; r<32; ++r)
                                    {
                                       for(int c=0; c<32; ++c)
                                       {
                                          int r_index = br*(sparse_in.mb_rows*sparse_in.ub_rows*32) + mbr*(sparse_in.ub_rows*32) + ubr*32 + r;
                                          int c_index = bc*(sparse_in.mb_columns*sparse_in.ub_columns*32) + mbc*(sparse_in.ub_columns*32) + ubc*32 + c;
                                          int tile_store_ptr = sparse_in.blocks[br][block_ptr].ublock_list[ublock_ptr].tile_ptr_list[tile_ptr];
                                          //new_matrix.vals[r_index][c_index] = sparse_in.tiles[br][tile_store_ptr].vals[r][c];
                                          new_matrix.vals[r_index][c_index] = sparse_in.unique_tiles[br][tile_store_ptr].vals[r][c];

                                          // if(new_matrix.vals[r_index][c_index] != sparse_in.vals[r_index][c_index])
                                          // {
                                          //    cout << "ding: " << r_index << " " << c_index << " " << mbr << " " << mbc << " " << ubr << " " << ubc << " " << (int)last_in_row_flag << " " << seen_a_one << endl;

                                          // }
                                          // else
                                          // {
                                          //    //cout << "flop: " << new_matrix.vals[r_index][c_index] << endl;
                                          // }
                                          if(sparse_in.vals[r_index][c_index] != 0.0) seen_a_one = true;
                                       }
                                    }
                                    tile_ptr++;
                                 }
                              }
                              tile_index++;
                           }
                        }
                        ublock_ptr++;
                     }
                  }
                  ublock_index++;
               }
            }
            block_ptr++;
         }
         block_index++;
      }
   }

   for(int i=0;i<sparse_in.rm_rows; ++i)
   {
      for(int j=0;j<sparse_in.rm_columns; ++j)
      {
         if(new_matrix.vals[i][j] != sparse_in.vals[i][j])
         {
            pass = false;
            cout << "fding" << endl;
            exit(1);
         }
         else
         {
            //cout << "flong: " << new_matrix.vals[i][j] << endl;
         }
      }
   }
   return pass;
}

////////
// Validator function - reconstructs a dense matrix from the sparse info
bool validate_sparse_tiled(bool ublock_scan_order, tt_matrix &sparse_in, const int max_tile_index_width=6, const int max_tile_ptr_width=10)
{
   bool pass = false;
   // construct a matrix full of zeros to begin with
   tt_matrix new_matrix(ublock_scan_order, sparse_in.block_rows, sparse_in.block_columns, sparse_in.mb_rows, sparse_in.mb_columns, sparse_in.ub_rows, sparse_in.ub_columns);
   for(int i=0;i<sparse_in.rm_rows;++i)
   {
      vector<float> tmp;
      for(int j=0;j<sparse_in.rm_columns;++j)
      {
         tmp.push_back(sparse_in.vals[i][j]);
      }
      new_matrix.vals.push_back(tmp);
   }

   // go through rows of blocks, ublocks, tiles and copy tile data over the zero initialized new matrix
   int block_index = 0;
   int ublock_index = 0;
   int tile_index = 0;

   int block_ptr = 0;
   int ublock_ptr = 0;
   int tile_ptr = 0;
   uint8_t last_in_row_flag_seen = 0;
   bool seen_a_one = false;

   int encoding_tile_ptr = 0;
   int intra_tile_ptr = 0;
   uint8_t last_in_encoding_tile_flag;

   for(int br=0; br < sparse_in.block_rows; ++br)
   {
      block_index = 0;
      block_ptr = 0;
      last_in_row_flag_seen = 0;
      last_in_encoding_tile_flag = 0;

      encoding_tile_ptr = 0;
      intra_tile_ptr = 0;
      for(int bc=0; bc < sparse_in.block_columns; ++bc)
      {
         if(last_in_encoding_tile_flag)
         {
            encoding_tile_ptr++;
            last_in_encoding_tile_flag = 0;
         }
         // if block exists and is not all zero
         if(last_in_row_flag_seen)
         if(sparse_in.encoding_tiles[br][encoding_tile_ptr].pop_equal_block_index_val(block_index, &last_in_row_flag_seen, &last_in_encoding_tile_flag)) 
         {
            assert(sparse_in.blocks[br][block_ptr].index == block_index);
            int number_of_ublocks = sparse_in.encoding_tiles[br][encoding_tile_ptr].pop_int16_val();
            ublock_index = 0;
            ublock_ptr = 0;
            //last_in_encoding_tile_flag = sparse_in.encoding_tiles[br][encoding_tile_ptr].pop_byte_val();
            //last_in_row_flag_seen = sparse_in.encoding_tiles[br][encoding_tile_ptr].pop_byte_val();
            if((number_of_ublocks != 0))// && (last_in_row_flag_seen == 0))
            {
               int inner = 0;
               int outer = 0;
               int inner_limit = 0;
               int outer_limit = 0;
               if(sparse_in.ublock_scan_order)
               {
                  inner_limit = sparse_in.mb_rows;
                  outer_limit = sparse_in.mb_columns;
               }
               else
               {
                  inner_limit = sparse_in.mb_columns;
                  outer_limit = sparse_in.mb_rows;
               }

               for(outer=0; outer < outer_limit; ++outer)
               {
                  for(inner=0; inner < inner_limit; ++inner)
                  {
                     int mbr;
                     int mbc;
                     if(sparse_in.ublock_scan_order)
                     {
                        mbr = inner;
                        mbc = outer;
                     }
                     else
                     {
                        mbr = outer;
                        mbc = inner;
                     }
                     // if ublock index matches, and we have not accidentally over-run the list of ublocks
                     assert(ublock_ptr < (int) pow(2, (float) max_tile_ptr_width)); // ublock_index_width == tile_ptr_width
                     uint16_t encoded_num_tiles = 0;
                     if(ublock_ptr < number_of_ublocks)
                     {
                        if(sparse_in.encoding_tiles[br][encoding_tile_ptr].pop_equal_ublock_index_val(ublock_index, &encoded_num_tiles, max_tile_ptr_width))
                        {
                           assert(ublock_index == sparse_in.blocks[br][block_ptr].ublock_list[ublock_ptr].index);
                           if (0 == encoded_num_tiles) {
                              encoded_num_tiles = (1<<max_tile_index_width); //special encoding for max num tiles
                           }

                           tile_index = 0;
                           tile_ptr = 0;
                           //int encoded_num_tiles = sparse_in.encoding_tiles[br][encoding_tile_ptr].pop_byte_val();
                           for(int ubr=0; ubr < sparse_in.ub_rows; ++ubr)
                           {
                              for(int ubc=0; ubc < sparse_in.ub_columns; ++ubc)
                              {
                                 // if tile exists and we have not accidentally overrun the list of tiles for given ublock
                                 assert(tile_ptr < (int)pow(2, (float) max_tile_ptr_width));
                                 if(tile_ptr < (int)encoded_num_tiles)
                                 {
                                    uint16_t encoded_tile_ptr = 0;
                                    if(sparse_in.encoding_tiles[br][encoding_tile_ptr].pop_equal_tile_index_val(tile_index, &encoded_tile_ptr, max_tile_index_width))
                                    {
                                       assert(sparse_in.blocks[br][block_ptr].ublock_list[ublock_ptr].tile_index_list[tile_ptr] == tile_index);
                                       assert(encoded_tile_ptr < (int)pow(2, (float) max_tile_ptr_width));
                                       //int encoded_tile_ptr = sparse_in.encoding_tiles[br][encoding_tile_ptr].pop_byte_val();
                                       for(int r=0; r<32; ++r)
                                       {
                                          for(int c=0; c<32; ++c)
                                          {
                                             int r_index = br*(sparse_in.mb_rows*sparse_in.ub_rows*32) + mbr*(sparse_in.ub_rows*32) + ubr*32 + r;
                                             int c_index = bc*(sparse_in.mb_columns*sparse_in.ub_columns*32) + mbc*(sparse_in.ub_columns*32) + ubc*32 + c;
                                             new_matrix.vals[r_index][c_index] = sparse_in.unique_tiles[br][encoded_tile_ptr].vals[r][c];
                                             int tile_store_gold = sparse_in.blocks[br][block_ptr].ublock_list[ublock_ptr].tile_ptr_list[tile_ptr];
                                             int tile_index_gold = sparse_in.blocks[br][block_ptr].ublock_list[ublock_ptr].tile_index_list[tile_ptr];
                                             if(new_matrix.vals[r_index][c_index] != sparse_in.vals[r_index][c_index])
                                             {
                                                cout << "tiled ding: " << r_index << " " << c_index << " " << mbr << " " << mbc << " " << ubr << " " << ubc << " " << (int)last_in_row_flag_seen << " " << seen_a_one << endl;

                                             }
                                             else
                                             {
                                                //cout << "flop: " << new_matrix.vals[r_index][c_index] << endl;
                                             }
                                             if(sparse_in.vals[r_index][c_index] != 0.0) seen_a_one = true;
                                          }
                                       }
                                       tile_ptr++;
                                    }
                                 }
                                 tile_index++;
                              }
                           }
                           ublock_ptr++;
                        }
                     }
                     ublock_index++;
                  }
               }
            }
            block_ptr++;
         }
         block_index++;
      }
   }

   for(int i=0;i<sparse_in.rm_rows; ++i)
   {
      for(int j=0;j<sparse_in.rm_columns; ++j)
      {
         if(new_matrix.vals[i][j] != sparse_in.vals[i][j])
         {
            cout << "tiled fding" << endl;
            exit(1);
         }
         else
         {
            //cout << "flong: " << new_matrix.vals[i][j] << endl;
         }
      }
   }
   return pass;
}

int main(int argc, char **argv)
{
   // int block_rows, int block_columns, int mb_rows, int mb_columns, int ub_rows, int ub_columns
   uint loops = 1;
   uint tests_per_loop = 1;
   uint seed = 0;

   uint test_id = 99;

   switch (argc) {
      case 3: seed = stoi(argv[2]);
      case 2: test_id = stoi(argv[1]);
              break; 
      case 0: 
      case 1: 
              break;
      default: 
              seed = stoi(argv[2]);
              test_id = stoi(argv[1]);
              break; 
   }

   generator.seed(seed);

   if (test_id >= 100) {
      assert(argc >= 14);
      int test = test_id-100;
      bool scan_order = true; //col 
      int num_encodings_per_tile=4096; // 4096 - RawUint32, 2048 - RawUint16, 1024 - RawUint8
      bool dump_output = false; //generate and dump expected output
      int grid_size_r = stoi(argv[3]);
      int grid_size_c = stoi(argv[4]);
      int mblock_m = stoi(argv[5]);
      int mblock_n = stoi(argv[6]);
      int mblock_k = stoi(argv[7]);
      int ublock_rt = stoi(argv[8]);
      int ublock_ct = stoi(argv[9]);
      int ublock_kt = stoi(argv[10]);
      int num_encoding_tiles = stoi(argv[11]);
      int num_sparse_tiles = stoi(argv[12]);
      int max_sparse_tile_ptr_width = stoi(argv[13]);
      int max_sparse_tile_index_width = 16-max_sparse_tile_ptr_width;
      string out_dir = ".";
      cout << "-- Generage encodings and sparse matrix for test id " << test << " -- " << endl;
      cout << "-- grid size [r,c] = [" << grid_size_r << "," << grid_size_c << "]" << endl;
      cout << "-- mblock in0 size [m,n] = [" << mblock_m << "," << mblock_k << "]" << endl;
      cout << "-- ublock in0 size [rt,ct] = [" << ublock_rt << "," << ublock_kt << "]" << endl;
      cout << "-- mblock in1 size [m,n] = [" << mblock_k << "," << mblock_n << "]" << endl;
      cout << "-- ublock in1 size [rt,ct] = [" << ublock_kt << "," << ublock_ct << "]" << endl;
      cout << "-- mblock out size [m,n] = [" << mblock_m << "," << mblock_n << "]" << endl;
      cout << "-- ublock out size [rt,ct] = [" << ublock_rt << "," << ublock_ct << "]" << endl;
      cout << "-- num encoding tiles = " << num_encoding_tiles << endl;
      cout << "-- num sparse tiles = " << num_sparse_tiles << endl;
      if (argc == 15) {
         out_dir = std::string(argv[14]) + to_string(test);
         cout << "-- out dir = " << out_dir << endl;
      }   
      cout << "-------------------------------------" << endl;

      int block_rows    = grid_size_r;
      int block_columns = mblock_k;
      int mb_rows       = mblock_m;
      int mb_columns    = 1;
      int ub_rows       = ublock_rt;
      int ub_columns    = ublock_kt;
      bool init_identity = true;

      int tile_col_ptr_width = 0;

      do {
         tile_col_ptr_width++;
      } while (((ublock_kt-1)>>tile_col_ptr_width)>0);

      tt_matrix sparse_mat(scan_order,block_rows,block_columns,mb_rows,mb_columns,ub_rows,ub_columns);
      if (init_identity) {
         sparse_mat.init_identity();
      } else {
         sparse_mat.init_random();
      }

      if (dump_output) {
         sparse_mat.dump_raw(out_dir + "/" + "input0.0.bin");
      }
      sparse_mat.build_spm(block_rows, false, num_encodings_per_tile);

      if(!sparse_mat.too_many_tiles())
      {
         // mat.init_random();
         // mat.init_identity();
         // mat.init_fill_one_block();
         sparse_mat.gen_tiled_encodings(max_sparse_tile_index_width, max_sparse_tile_ptr_width, tile_col_ptr_width);
         validate_sparse(scan_order,sparse_mat);
         validate_sparse_tiled(scan_order,sparse_mat, max_sparse_tile_index_width, max_sparse_tile_ptr_width);
         sparse_mat.info();
         bool pad_enc_tiles = (num_encoding_tiles > 0);
         bool pad_sparse_tiles = (num_sparse_tiles > 0); 
         sparse_mat.dump_spm(out_dir + "/" + "input2_encoding.0.bin", out_dir + "/" + "input0_sparse.0.bin", pad_enc_tiles, pad_sparse_tiles, num_encoding_tiles, num_sparse_tiles, num_encodings_per_tile);
      }
      cout << "-------------------------------------" << endl;


      if (dump_output) {
         cout << "-- Generate random input 1 -- " << endl;
         tt_matrix act_mat(scan_order,1,1,mblock_k,mblock_n*grid_size_c,ublock_kt,ublock_ct);
         act_mat.randomize();
         act_mat.dump_raw(out_dir + "/" + "input1.0.bin");

         tt_matrix out_mat(scan_order,1,1,mblock_m*grid_size_r,mblock_n*grid_size_c,ublock_rt,ublock_ct);
         out_mat.zeroit();

         cout << "-- Generate output -- " << endl;
         out_mat.matmul(&sparse_mat, &act_mat);
         out_mat.dump_raw(out_dir + "/" + "output0.0.bin");
      }   

   } else {

      for(int i=0;i<1;++i)
      {
         uint test = test_id; //FIXME: rand_int(0,5);
         bool scan_order = true; //col //FIXME rand_bool();
         //test = 99;
         if(test == 0)
         {
            int block_rows    = rand_int(1,4);
            int block_columns = rand_int(1,1);
            int mb_rows       = rand_int(1,4);
            int mb_columns    = rand_int(1,1);
            int ub_rows       = rand_int(8,8);
            int ub_columns    = rand_int(8,8);
            //tt_matrix mat(4,4,4,4,4,4);
            tt_matrix mat(scan_order,block_rows,block_columns,mb_rows,mb_columns,ub_rows,ub_columns);
            mat.init_one_per_tile();
            mat.build_spm(block_rows);
            if(!mat.too_many_tiles())
            {
               cout << "Fill each tile: " << endl;
               mat.gen_tiled_encodings();
               validate_sparse(scan_order,mat);
               validate_sparse_tiled(scan_order,mat);
               mat.info();
            }
         }
         if(test == 1)
         {
            cout << "------ Identity test:" << endl;
            int block_rows    = rand_int(1,1);
            int block_columns = rand_int(1,1);
            int mb_rows       = rand_int(16,16);
            int mb_columns    = rand_int(16,16);
            int ub_rows       = rand_int(16,16);
            int ub_columns    = rand_int(16,16);
            //tt_matrix mat(4,4,4,4,4,4);
            tt_matrix mat(scan_order,block_rows,block_columns,mb_rows,mb_columns,ub_rows,ub_columns);

            // mat.init_random();
            mat.init_identity();
            // mat.init_fill_one_block();
            // mat.init_fill_one_random_ublock_per_block();
            // mat.init_one_per_tile();
            mat.build_spm(block_rows);
            mat.gen_tiled_encodings();
            validate_sparse(scan_order,mat);
            validate_sparse_tiled(scan_order,mat);
            mat.info();
         }
         if(test == 2)
         {
            cout << "------ Random ones test: " << endl;
            int block_rows    = rand_int(1,8);
            int block_columns = rand_int(1,8);
            int mb_rows       = rand_int(1,8);
            int mb_columns    = rand_int(1,8);
            int ub_rows       = rand_int(1,8);
            int ub_columns    = rand_int(1,8);
            //tt_matrix mat(4,4,4,4,4,4);
            tt_matrix mat(scan_order,block_rows,block_columns,mb_rows,mb_columns,ub_rows,ub_columns);

            mat.init_random();
            // mat.init_identity();
            // mat.init_fill_one_block();
            // mat.init_fill_one_random_ublock_per_block();
            // mat.init_one_per_tile();
            mat.build_spm(block_rows);
            mat.gen_tiled_encodings();
            validate_sparse(scan_order,mat);
            validate_sparse_tiled(scan_order,mat);
            mat.info();
         }
         if(test == 3)
         {
            cout << "------ Fill one block test: " << endl;
            int block_rows    = rand_int(1,8);
            int block_columns = rand_int(1,8);
            int mb_rows       = rand_int(1,8);
            int mb_columns    = rand_int(1,8);
            int ub_rows       = rand_int(1,8);
            int ub_columns    = rand_int(1,8);
            //tt_matrix mat(4,4,4,4,4,4);
            tt_matrix mat(scan_order,block_rows,block_columns,mb_rows,mb_columns,ub_rows,ub_columns);

            // mat.init_random();
            // mat.init_identity();
            mat.init_fill_one_block();
            // mat.init_one_per_tile();
            mat.build_spm(block_rows);
            mat.gen_tiled_encodings();
            validate_sparse(scan_order,mat);
            validate_sparse_tiled(scan_order,mat);
            mat.info();
         }
         if(test == 4)
         {
            cout << "------ Fill random ublock in each block test: " << endl;
            int block_rows    = rand_int(1,8);
            int block_columns = rand_int(1,1);
            int mb_rows       = rand_int(1,8);
            int mb_columns    = rand_int(1,8);
            int ub_rows       = rand_int(1,8);
            int ub_columns    = rand_int(1,8);
            //tt_matrix mat(4,4,4,4,4,4);
            tt_matrix mat(scan_order,block_rows,block_columns,mb_rows,mb_columns,ub_rows,ub_columns);
            mat.init_one_per_tile();
            mat.build_spm(block_rows);
            if(!mat.too_many_tiles())
            {
               mat.gen_tiled_encodings();
               validate_sparse(scan_order,mat);
               validate_sparse_tiled(scan_order,mat);
               mat.info();
            }
         }
         if(test == 5)
         {
            cout << "------ Shift matrix: " << endl;
            int block_rows    = rand_int(2,2);
            int block_columns = rand_int(2,2);
            int mb_rows       = rand_int(8,8);
            int mb_columns    = rand_int(8,8);
            int ub_rows       = rand_int(8,8);
            int ub_columns    = rand_int(8,8);
            //tt_matrix mat(4,4,4,4,4,4);
            tt_matrix mat(scan_order,block_rows,block_columns,mb_rows,mb_columns,ub_rows,ub_columns);
            mat.init_shift_matrix(1,1);
            mat.build_spm(block_rows);

            if(!mat.too_many_tiles())
            {
               // mat.init_random();
               // mat.init_identity();
               // mat.init_fill_one_block();
               mat.gen_tiled_encodings();
               validate_sparse(scan_order,mat);
               validate_sparse_tiled(scan_order,mat);
               mat.info();
            }
         }
         if(test == 98)
         {
            cout << "------ Big hift matrix: " << endl;
            int mb_rows       = 8;
            int mb_columns    = 8;
            int ub_rows       = 8;
            int ub_columns    = 2;

            int matrix_dim = 256*256;
            int block_rows    = matrix_dim / (mb_rows*ub_rows*32);
            int block_columns = matrix_dim / (mb_columns*ub_columns*32);

            tt_matrix mat(scan_order,block_rows,block_columns,mb_rows,mb_columns,ub_rows,ub_columns);
            mat.init_shift_matrix(-1,2);
            mat.dump_raw();
            mat.build_spm(block_rows);

            if(!mat.too_many_tiles())
            {
               mat.gen_tiled_encodings();
               validate_sparse(scan_order,mat);
               validate_sparse_tiled(scan_order,mat);
               mat.info();
               //mat.dump_spm();
               break;
            }
         }
         if(test == 99)
         {
            cout << "------ Shift matrix: " << endl;
            int block_rows    = 1;
            int block_columns = 27;
            int mb_rows       = rand_int(4,4);
            int mb_columns    = rand_int(1,1);
            int ub_rows       = rand_int(2,2);
            int ub_columns    = rand_int(2,2);
            //tt_matrix mat(4,4,4,4,4,4);
            tt_matrix mat(scan_order,block_rows,block_columns,mb_rows,mb_columns,ub_rows,ub_columns);
            //mat.init_shift_matrix(-1,5);
            //mat.init_one_per_tile(1.0f);
            //mat.init_one_per_tile();
            //mat.init_fill_one_block();
            mat.init_random();
            mat.dump_raw();
            mat.build_spm(block_rows);

            if(!mat.too_many_tiles())
            {
               // mat.init_random();
               // mat.init_identity();
               // mat.init_fill_one_block();
               mat.gen_tiled_encodings();
               validate_sparse(scan_order,mat);
               validate_sparse_tiled(scan_order,mat);
               mat.info();
               mat.dump_spm();
               break;
            }
         }

         cout << "Pass" << endl;
      }
   }   
   cout << "Done." << endl;

   return 0;
}
