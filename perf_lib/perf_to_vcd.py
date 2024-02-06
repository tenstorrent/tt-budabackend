# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
import sys
import json
from datetime import datetime
from pprint import pprint
from re import search

reference_min = 33
reference_max = 127
r1 = reference_min
r2 = reference_min
r3 = reference_min

special_timestamp_ids = ['chunk-read-issued', 'tiles-flushed']
special_events = ['epoch-q-slot-complete', 'dram-write-sent', 'dram-write-tile-cleared']
stream_info = {} #This dictionary holds stream info. We need chunk-size-tiles in particular to generate
                 #global writes counter.
vcd = {} # This dictionary holds all timestamp/value data for all signals across all epochs.
vcd_definitions=''
vcd_initializations=''
vcd_file=''
vcd_file_name='perf_dump.vcd'
aiclk_period_ns = 2

get_bin = lambda x, n: format(x, 'b').zfill(n)

def compute_global_reads(epoch, global_event_name, global_event_reference, global_event_size):
    reads_issued = 0
    reads_changed = False
    for timestamp in sorted(vcd):
        reads_changed = False
        for items in vcd[timestamp]:
            for signals in items.keys():
                if search(epoch, signals):
                    if search('chunk-read-issued', signals):
                        x = signals.rsplit('dram-read', 1)
                        y = x[1].rsplit('-', 4)
                        z = x[0]+'info'+y[0]
                        #print('chunk size tiles key = ' + z)
                        #UC -- FIXME if z in stream_info:
                        #UC -- FIXME     chunk_size_tiles = stream_info[z]
                        #UC -- FIXME else:
                        #UC -- FIXME     print(signals)
                        #UC -- FIXME     print('Error: perf_to_vcd - Cannot find stream to chunk-size-tiles mapping for read issued.')
                        #UC -- FIXME     exit(1)
                        chunk_size_tiles = 4
                        if items[signals][1] == 1:
                            reads_issued = reads_issued + chunk_size_tiles
                            reads_changed = True
                    if search('tiles-flushed', signals):
                        x = signals.rsplit('dram-read', 1)
                        y = x[1].rsplit('-', 3)
                        z = x[0]+'info'+y[0]
                        #print('chunk size tiles key = ' + z)
                        #UC -- FIXME if z in stream_info:
                        #UC -- FIXME     chunk_size_tiles = stream_info[z]
                        #UC -- FIXME else:
                        #UC -- FIXME     print(signals)
                        #UC -- FIXME     print('Error: perf_to_vcd - Cannot find stream to chunk-size-tiles mapping for read flushed.')
                        #UC -- FIXME     exit(1)
                        chunk_size_tiles = 4
                        if items[signals][1] == 1:
                            reads_issued = reads_issued - chunk_size_tiles
                            reads_changed = True
        if reads_changed:
            #print ('Reads issued = '+str(reads_issued))
            vcd[timestamp].append({global_event_name:[global_event_reference, reads_issued, global_event_size]})

def compute_global_writes(epoch, global_event_name, global_event_reference, global_event_size):
    writes_issued = 0
    writes_changed = False
    for timestamp in sorted(vcd):
        writes_changed = False
        for items in vcd[timestamp]:
            for signals in items.keys():
                if search(epoch, signals):
                    if search('dram-write-sent', signals):
                        x = signals.rsplit('dram-write-sent', 1)
                        y = x[1].rsplit('-', 1)
                        z = x[0]+'info'+y[0]
                        #print('chunk size tiles key = ' + z)
                        #UC -- FIXME if z in stream_info:
                        #UC -- FIXME     chunk_size_tiles = stream_info[z]
                        #UC -- FIXME else:
                        #UC -- FIXME     print(signals)
                        #UC -- FIXME     print('Error: perf_to_vcd - Cannot find stream to chunk-size-tiles mapping for write sent.')
                        #UC -- FIXME     exit(1)
                        chunk_size_tiles = 4
                        if items[signals][1] == 1:
                            #there are two tile-cleared events for each write issued.
                            writes_issued = writes_issued + chunk_size_tiles
                            writes_changed = True
                    if search('dram-write-tile-cleared', signals):
                        if items[signals][1] == 1:
                            writes_issued = writes_issued - 2
                            if writes_issued < 0:
                                writes_issued = 0
                            writes_changed = True
        if writes_changed:
            #print ('Writes issued = '+str(writes_issued))
            vcd[timestamp].append({global_event_name:[global_event_reference, writes_issued, global_event_size]})

def write_vcd_values():
    global aiclk_period_ns
    if vcd:
        start_time = sorted(vcd)[0]
        #print(start_time)
        for timestamp in sorted(vcd):
            vcd_file.write('#'+str(int((timestamp-start_time)*aiclk_period_ns))+'\n')
            for items in vcd[timestamp]:
                for signals in items.keys():
                    ref = items[signals][0]
                    val = items[signals][1]
                    size = items[signals][2]
                    #print (signals +' '+ref+' '+str(val))
                    if size == 1:
                        vcd_file.write(str(val)+ref+'\n')
                    else:
                        vcd_file.write('b'+get_bin(val, size) +' '+ref+'\n')


def write_vcd():
    global vcd_file
    global vcd_file_name
    now = datetime.now()
    dt_string = now.strftime("%A %B %d %H:%M:%S %Y")

    vcd_file = open(vcd_file_name,'w')
    vcd_file.write('$version Generated by perf_to_vcd.py $end\n')
    vcd_file.write('$date '+dt_string+' $end\n')
    vcd_file.write('$timescale 1ns $end\n\n')
    vcd_file.write('$scope module perf_top $end\n')
    vcd_file.write(vcd_definitions)
    vcd_file.write('$upscope $end\n')
    vcd_file.write('$enddefinitions $end\n\n')
    vcd_file.write('$dumpvars\n')
    vcd_file.write(vcd_initializations)
    write_vcd_values()
    vcd_file.close()

def vcd_module_string(module_name):
    return '$scope module '+module_name+' $end\n'

def vcd_end_module_string():
    return '$upscope $end\n'

def vcd_var_string(signal_name, signal_reference):
    return '$var wire 1 '+signal_reference+' '+signal_name+' $end\n'

def vcd_info_var_string(signal_name, signal_reference):
    return '$var wire 32 '+signal_reference+' '+signal_name+' $end\n'

def add_event_to_vcd(time, event_name, event_reference, event_value, event_size):
    if time in vcd:
        vcd[time].append({event_name:[event_reference,event_value, event_size]})
    else:
        event_val=[{event_name:[event_reference,event_value, event_size]}]
        vcd[time] = event_val


def get_signal_reference():
    global r1
    global r2
    global r3

    r1 += 1
    if r1 == reference_max:
        r1 = reference_min
        r2 += 1
        if r2 == reference_max:
            r2 = reference_min
            r3 += 1
            if r3 == reference_max:
                print("ERROR: Cannnot generate reference")
                exit()

    return chr(r3)+chr(r2)+chr(r1)

def get_signal_init_val(signal_name, signal_reference, signal_value):
    if search('info-stream', signal_name):
        return 'b'+get_bin(signal_value, 32) +' '+signal_reference+'\n'
    else:
        return str(signal_value)+signal_reference+'\n'

def get_bus_init_val(signal_reference, signal_value):
        return 'b'+get_bin(signal_value, 32) +' '+signal_reference+'\n'


arg_cnt =  len(sys.argv)
if arg_cnt < 4:
    print ('Insufficient Arguments')
    print ('Usage: '+sys.argv[0]+' ai_clk_MHz out_file_name in_file_1 [in_file_2 ...]')
    exit(1)
aiclk_ghz = int(sys.argv[1])/1000
aiclk_period_ns = 1/aiclk_ghz
vcd_file_name = sys.argv[2]
# Opening JSON file
#with open('model/perf_lib/out/epoch_0/perf_postprocess_epoch_0.json') as json_file:
#with open('/home/ucheema/perf_postprocess_epoch_3.json') as json_file:
epoch = 0
for input_num in range (3, arg_cnt):
    with open(sys.argv[input_num]) as json_file:

        print(f"Processing file: {json_file.name}")
        data = json.load(json_file)

        vcd_definitions += vcd_module_string('epoch_'+str(epoch))
        g_reads_signal_ref = get_signal_reference()
        g_writes_signal_ref = get_signal_reference()
        g_reads_signal_name = 'global_reads_issued'
        g_writes_signal_name = 'global_writes_issued'
        vcd_definitions += vcd_info_var_string(g_reads_signal_name, g_reads_signal_ref)
        vcd_initializations += get_bus_init_val(g_reads_signal_ref, 0)
        vcd_definitions += vcd_info_var_string(g_writes_signal_name, g_writes_signal_ref)
        vcd_initializations += get_bus_init_val(g_writes_signal_ref, 0)

        for cores in data.keys():
            print("\n Core Keys: ", cores)
            vcd_definitions += vcd_module_string(cores)
            if 'NCRISC' in data[cores].keys():
                vcd_definitions += vcd_module_string('ncrisc')
                #print ("\n     NCRISC Keys: ", data[cores]['NCRISC'].keys())
                ncrisc = data[cores]['NCRISC']
                if isinstance(ncrisc, dict):
                    for event in ncrisc.keys():

                        #print (event)
                        special_event = 'none'
                        full_event_name = 'epoch_'+str(epoch)+'_'+cores+"_ncrisc_"+event
                        timestamp_id = 'start'
                        #For some events we skip the start time.
                        if search('epoch-q-slot-complete', event):
                            timestamp_id = 'skip'
                            special_event = 'epoch-q-slot-complete'
                        if search('dram-write-sent', event):
                            timestamp_id = 'skip'
                            special_event = 'dram-write-sent'
                        if search('dram-write-tile-cleared', event):
                            timestamp_id = 'skip'
                            special_event = 'dram-write-tile-cleared'
                        if search('dram-io-q-status', event):
                            timestamp_id = 'q-available'
                        if search('buffer-status', event):
                            timestamp_id = 'buf-available'
                        if search('epoch-q-empty', event):
                            timestamp_id = 'q-empty'
                        if search('dram-read-stream', event):
                            timestamp_id = 'chunk-read-issued'
                            full_event_name = 'epoch_'+str(epoch)+'_'+cores+"_ncrisc_"+event+"-"+timestamp_id
                            signal_reference = get_signal_reference()
                            vcd_definitions += vcd_var_string(event+'-'+timestamp_id, signal_reference)
                            vcd_initializations += get_signal_init_val(event, signal_reference, 0)
                        else:
                            if search('misc-info-stream', event):
                                #print("Skipping event: ", event)
                                signal_reference = get_signal_reference()
                                vcd_definitions += vcd_info_var_string(event, signal_reference)
                                vcd_initializations += get_signal_init_val(event, signal_reference, 0)
                                for timestamp,value in zip(ncrisc[event]['time'], ncrisc[event]['data']):
                                    add_event_to_vcd(timestamp, full_event_name, signal_reference, value, 32)
                            elif search('info-stream', event):
                                vcd_definitions += vcd_module_string(event)
                                for x in ncrisc[event]:
                                    signal_reference = get_signal_reference()
                                    vcd_definitions += vcd_info_var_string(x, signal_reference)
                                    vcd_initializations += get_signal_init_val(event, signal_reference, ncrisc[event][x])
                                    if x == 'data-chunk-size-tiles':
                                        stream_info[full_event_name] = ncrisc[event][x]
                                vcd_definitions += vcd_end_module_string()
                            else:
                                signal_reference = get_signal_reference()
                                vcd_definitions += vcd_var_string(event, signal_reference)
                                vcd_initializations += get_signal_init_val(event, signal_reference, 0)

                        if timestamp_id in ncrisc[event]:
                            event_timestamps = ncrisc[event][timestamp_id]
                            #print(event_timestamps)
                            for timestamp in event_timestamps:
                                add_event_to_vcd(timestamp, full_event_name, signal_reference, 1, 1)
                                if timestamp_id in special_timestamp_ids:
                                    add_event_to_vcd(timestamp+50, full_event_name, signal_reference, 0, 1)

                        timestamp_id = 'end'
                        if search('dram-io-q-status', event):
                            timestamp_id = 'q-empty'
                        if search('buffer-status', event):
                            timestamp_id = 'buf-full'
                        if search('epoch-q-empty', event):
                            timestamp_id = 'q-available'
                        if search('dram-read-stream', event):
                            timestamp_id = 'tiles-flushed'
                            full_event_name = 'epoch_'+str(epoch)+'_'+cores+"_ncrisc_"+event+"-"+timestamp_id
                            signal_reference = get_signal_reference()
                            vcd_definitions += vcd_var_string(event+'-'+timestamp_id, signal_reference)
                            vcd_initializations += get_signal_init_val(event, signal_reference, 0)

                        if timestamp_id in ncrisc[event]:
                            event_timestamps = ncrisc[event][timestamp_id]
                            #print(event_timestamps)
                            for timestamp in event_timestamps:
                                if (timestamp_id in special_timestamp_ids or special_event in special_events):
                                    add_event_to_vcd(timestamp, full_event_name, signal_reference, 1, 1)
                                    add_event_to_vcd(timestamp+50, full_event_name, signal_reference, 0, 1)
                                else:
                                    add_event_to_vcd(timestamp, full_event_name, signal_reference, 0, 1)
                else:
                    print ("NCRISC has no perf data.")
                vcd_definitions += vcd_end_module_string() #ncrisc endmodule

            if 'T0' in data[cores].keys():
                vcd_definitions += vcd_module_string('trisc0')
                trisc0 = data[cores]['T0']
                if isinstance(trisc0, dict):
                    for event in trisc0.keys():
                        full_event_name = 'epoch_'+str(epoch)+'_'+cores+"_trisc0_"+event
                        timestamp_id = 'start'

                        signal_reference = get_signal_reference()
                        vcd_definitions += vcd_var_string(event, signal_reference)
                        vcd_initializations += get_signal_init_val(event, signal_reference, 0)

                        if timestamp_id in trisc0[event]:
                            event_timestamps = trisc0[event][timestamp_id]
                            #print(event_timestamps)
                            for timestamp in event_timestamps:
                                add_event_to_vcd(timestamp, full_event_name, signal_reference, 1, 1)

                        timestamp_id = 'end'
                        if timestamp_id in trisc0[event]:
                            event_timestamps = trisc0[event][timestamp_id]
                            #print(event_timestamps)
                            for timestamp in event_timestamps:
                                add_event_to_vcd(timestamp, full_event_name, signal_reference, 0, 1)
                else:
                    print ("TRISC0 has no perf data.")
                vcd_definitions += vcd_end_module_string() #trisc0 endmodule

            if 'T2' in data[cores].keys():
                vcd_definitions += vcd_module_string('trisc2')
                trisc2 = data[cores]['T2']
                if isinstance(trisc2, dict):
                    for event in trisc2.keys():
                        full_event_name = 'epoch_'+str(epoch)+'_'+cores+"_trisc2_"+event
                        timestamp_id = 'start'

                        signal_reference = get_signal_reference()
                        vcd_definitions += vcd_var_string(event, signal_reference)
                        vcd_initializations += get_signal_init_val(event, signal_reference, 0)

                        if timestamp_id in trisc2[event]:
                            event_timestamps = trisc2[event][timestamp_id]
                            #print(event_timestamps)
                            for timestamp in event_timestamps:
                                add_event_to_vcd(timestamp, full_event_name, signal_reference, 1, 1)

                        timestamp_id = 'end'
                        if timestamp_id in trisc2[event]:
                            event_timestamps = trisc2[event][timestamp_id]
                            #print(event_timestamps)
                            for timestamp in event_timestamps:
                                add_event_to_vcd(timestamp, full_event_name, signal_reference, 0, 1)
                else:
                    print ("TRISC2 has no perf data.")
                vcd_definitions += vcd_end_module_string() #trisc2 endmodule
            vcd_definitions += vcd_end_module_string() #core endmodule
        vcd_definitions += vcd_end_module_string() #epoch endmodule
        compute_global_reads('epoch_'+str(epoch)+'_', g_reads_signal_name, g_reads_signal_ref, 32)
        compute_global_writes('epoch_'+str(epoch)+'_', g_writes_signal_name, g_writes_signal_ref, 32)
        epoch = epoch + 1

    # Print the data of dictionary
    #print("\nPerf Data:", data['0-1-matmul_0'])

#pprint(vcd)
write_vcd()
