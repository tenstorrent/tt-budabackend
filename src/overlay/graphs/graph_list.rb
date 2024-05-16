# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0

$extra_tile_header_buffer_size = (2 * 32 * 1024)
msg_info_buf_size = 16 * 2048
$msg_info_buf_addr = $PARAMS[:data_buffer_space_base] - $PARAMS[:overlay_blob_size] - 128 - msg_info_buf_size

require "#{$PARAMS[:blob_gen_root]}/graphs/unary_test.rb"
require "#{$PARAMS[:blob_gen_root]}/graphs/unary_tilize_test.rb"
require "#{$PARAMS[:blob_gen_root]}/graphs/unary_streaming_test.rb"
require "#{$PARAMS[:blob_gen_root]}/graphs/unary_tilize_streaming_test.rb"
require "#{$PARAMS[:blob_gen_root]}/graphs/unary_untilize_streaming_test.rb"
require "#{$PARAMS[:blob_gen_root]}/graphs/binary_test.rb"
require "#{$PARAMS[:blob_gen_root]}/graphs/binary_streaming_test.rb"
require "#{$PARAMS[:blob_gen_root]}/graphs/nary_streaming_test.rb"
require "#{$PARAMS[:blob_gen_root]}/graphs/binary_intermediate_test.rb"
require "#{$PARAMS[:blob_gen_root]}/graphs/binary_intermediate_streaming_test.rb"
require "#{$PARAMS[:blob_gen_root]}/graphs/binary_intermediate2_test.rb"
require "#{$PARAMS[:blob_gen_root]}/graphs/binary_intermediate2_streaming_test.rb"
require "#{$PARAMS[:blob_gen_root]}/graphs/unary_multi_out_test.rb"
require "#{$PARAMS[:blob_gen_root]}/graphs/unary_multi_out_streaming_test.rb"
require "#{$PARAMS[:blob_gen_root]}/graphs/unary_multi_in_test.rb"
require "#{$PARAMS[:blob_gen_root]}/graphs/unary_multi_in_streaming_test.rb"

$tt_overlay_graphs = {
  "unary_test" => $unary_test_graph,
  "unary_tilize_test" => $unary_tilize_test_graph,
  "unary_streaming_test" => $unary_streaming_test_graph,
  "unary_tilize_streaming_test" => $unary_tilize_streaming_test_graph,
  "unary_untilize_streaming_test" => $unary_untilize_streaming_test_graph,
  "binary_test" => $binary_test_graph,
  "binary_streaming_test" => $binary_streaming_test_graph,
  "nary_streaming_test" => $nary_streaming_test_graph,
  "binary_intermediate_test" => $binary_intermediate_test_graph,
  "binary_intermediate_streaming_test" => $binary_intermediate_streaming_test_graph,
  "binary_intermediate2_test" => $binary_intermediate2_test_graph,
  "binary_intermediate2_streaming_test" => $binary_intermediate2_streaming_test_graph,
  "unary_multi_out_test" => $unary_multi_out_test_graph,
  "unary_multi_out_streaming_test" => $unary_multi_out_streaming_test_graph,
  "unary_multi_in_test" => $unary_multi_in_test_graph,
  "unary_multi_in_streaming_test" => $unary_multi_in_streaming_test_graph
}





