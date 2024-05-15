#in_q_base_addr=("10000000" "11000000" "12000000")
#out_q_base_addr=("13000000")
in_q_base_addr=("10000000" "11000000")
out_q_base_addr=("12000000")
tile_count=4
tile_size=2080

for q_addr in "${in_q_base_addr[@]}"
do
  cmd="./device/bin/silicon/grayskull/tt-script ./device/bin/silicon/grayskull/ttx_status.py --args \"x=1,y=0,addr=$q_addr,burst=8,data=0\""
  printf "cmd\n"
  eval $cmd
done  

for q_addr in "${out_q_base_addr[@]}"
do
  cmd="./device/bin/silicon/grayskull/tt-script ./device/bin/silicon/grayskull/ttx_status.py --args \"x=1,y=0,addr=$q_addr,burst=8,data=0\""
  printf "cmd\n"
  eval $cmd
done  

for q_addr in "${in_q_base_addr[@]}"
do
  for ((i=0;i<$tile_count;i++))
  do
    addr=$(printf '%x' $((16#$q_addr+$tile_size*$i+32)))	   
    data=$(printf '0x%x' $(($tile_size/16)))
    burst=4
    cmd="./device/bin/silicon/grayskull/tt-script ./device/bin/silicon/grayskull/ttx_status.py --args \"x=1,y=0,addr=$addr,burst=$burst,data=$data\""
    printf "cmd\n"
    eval $cmd
    addr=$(printf '%x' $((16#$addr+16)))
    data=$(printf '0x%x' $(($tile_size/16)))
    data="0x4400"
    burst=$(($tile_size-16))
    cmd="./device/bin/silicon/grayskull/tt-script ./device/bin/silicon/grayskull/ttx_status.py --args \"x=1,y=0,addr=$addr,burst=$burst,data=$data\""
    printf "cmd\n"
    eval $cmd
  done
done   

for q_addr in "${in_q_base_addr[@]}"
do
  addr=$(printf '%x' $((16#$q_addr+4)))
  data="1"
  cmd="./device/bin/silicon/grayskull/tt-script ./device/bin/silicon/grayskull/ttx_status.py --args \"x=1,y=0,addr=$addr,data=$data\""
  printf "cmd\n"
  eval $cmd
done

#./device/bin/silicon/grayskull/tt-script ./device/bin/silicon/grayskull/ttx_status.py --args "x=1,y=0,addr=10000020,burst=4,data=82"
#./device/bin/silicon/grayskull/tt-script ./device/bin/silicon/grayskull/ttx_status.py --args "x=1,y=0,addr=10000030,burst=2064,data=0x4400"
#./device/bin/silicon/grayskull/tt-script ./device/bin/silicon/grayskull/ttx_status.py --args "x=1,y=0,addr=10000840,burst=8320,data=82"
#./device/bin/silicon/grayskull/tt-script ./device/bin/silicon/grayskull/ttx_status.py --args "x=1,y=0,addr=11000020,burst=4,data=82"
#./device/bin/silicon/grayskull/tt-script ./device/bin/silicon/grayskull/ttx_status.py --args "x=1,y=0,addr=11000030,burst=2064,data=0x4400"
#./device/bin/silicon/grayskull/tt-script ./device/bin/silicon/grayskull/ttx_status.py --args "x=1,y=0,addr=11000840,burst=8320,data=82"
#./device/bin/silicon/grayskull/tt-script ./device/bin/silicon/grayskull/ttx_status.py --args "x=1,y=0,addr=12000020,burst=4,data=82"
#./device/bin/silicon/grayskull/tt-script ./device/bin/silicon/grayskull/ttx_status.py --args "x=1,y=0,addr=12000030,burst=2064,data=0x4400"
#./device/bin/silicon/grayskull/tt-script ./device/bin/silicon/grayskull/ttx_status.py --args "x=1,y=0,addr=12000840,burst=8320,data=82"
#./device/bin/silicon/grayskull/tt-script ./device/bin/silicon/grayskull/ttx_status.py --args "x=1,y=0,addr=10000004,data=1"
#./device/bin/silicon/grayskull/tt-script ./device/bin/silicon/grayskull/ttx_status.py --args "x=1,y=0,addr=11000004,data=1"
#./device/bin/silicon/grayskull/tt-script ./device/bin/silicon/grayskull/ttx_status.py --args "x=1,y=0,addr=12000004,data=1"
