# Tilizer API Documentation and Design Guide {#tilizer}
This document outlines the different functional modes of the Fast Tilizer and provides some performance metrics to influence Front End design decisions. 

# Overview
The tilizer is used to process and write parameters during compile time and activations during runtime.
It performs a two to three step process on host data (depending on the functional mode), consisting of prestriding (performed for CNNs), data shuffling and format conversions. The processed data is then written to input queues in DRAM.

Note: The tilizer uses multithreading by default for certain input shapes (batch size > 16), data format conversions (non identity coversions) and input queue locations (more on this below). To disable multithreading, please use the environment variable: `TT_BACKEND_DISABLE_MULTI_THREADED_PUSH=1`.

## Data Shuffling
The data shuffling step is a function of the tilizer mode. By default, both activations and parameters are shuffled to conform to the tilized layout. Less exotic modes, such as the MegaRow and Flat layout are also supported. Details regarding these layouts, as well as the Prestriding flow are provided below.

## Format Conversions
Format conversions for all common use cases are supported by the Fast Tilizer. These consist of converting host data in Float32 & Float16 (Float16b included) to any of the following data formats consumed by the device: 
|| || || 
| --- | ----------- | ----------- | ----------- | ----------- |
| Float32  | Float16 | Float16b | Bfp8 | Bfp8b |

Integer formats are also supported for identity conversions: Int32, Int16 and Int8 data can be written to device. Conversions to data-formats using less than 8 bits are not presently supported by the Fast Tilizer.

Int8 Data on host can also be converted to Bfp8 and Bfpb data during tilization. 

# Data Layouts

As a simplified example, assume a 2D input tensor as follows:
```
0 1 2 3
4 5 6 7
```
The memory layout on host will be:
```
0 1 2 3 4 5 6 7
```
Tile dimensions of 2x2 are assumed for this example.
We provide a description of the data written to device for the Tilized, MegaRow and Flat Layouts.

## Tilized (Default Mode)
The two tiles created by the tilizer in this mode will be as follows:
```
 Tile 0   Tile 1
|  0 1  |  2 3  |
|  4 5  |  6 7  |
```
Each tile will be written linearly in DRAM as follows (along with tile headers):
```
Tile 0: 0 1 4 5
Tile 1: 2 3 6 7
```
In practice, the tilizer supports 32x32 tiles and 4D tensors, which requires this process to be performed on each two dimensional tensor face. Additionally, it is required that tensor dimensions be a multiple of 32 in the x and y dimensions.

Note: To keep this example simple, we assume a tile to be the smallest unit of data handled by software. In practice, each tile consists of 4 faces (16x16 datums). Data for each face is linearly laid out in memory, before progressing to the next face. Thus, each tile looks like this:

```
| F1 | F2 |
| F3 | F4 |
```
Where F1, F2, F3 and F4 are 16x16 faces. Laid out in memory, each tile will be as follows (with a tile header prepended):

```
F1 F2 F3 F4
```

## MegaRow
This layout is relevant for integer data used as masks. In this case, each tile will be populated by linearly scanning host data:
```
 Tile 0    Tile 1
|  0 1  |   4 5  |
|  2 3  |   6 7  |
```
When linearly laid out in DRAM, concatenating the two tiles will produce the original data on host:
```
 Tile 0: 0 1 2 3
 Tile 1: 4 5 6 7
```
Tile headers are written to device for this mode as well.

## Flat Layout
This is used primarily for supporting the Embedding Lookup Operation on device and consists of simply copying slices of data from host to device queues in a grid without any shuffling. Tile headers are not written to DRAM in this case.

 As mentioned, splitting data across different cores in a queue grid is a regular data copy. For example, with a [1, 4] grid, each grid will be populated with the following slices of the tensor:
```
 Grid 0   Grid 1  Grid 2  Grid 3
|   0   |   1   |   2   |   3   |
|   4   |   5   |   6   |   4   |
 ```
Horizontally stacking the data in each grid produces the original 2 dimensional tensor.

## Supported Queue Locations
The tilizer can write data to input queues place in host memory, DRAM queues mapped to host MMIO and non MMIO DRAM queues. Accessing host queues provides the highest push BW, followed by MMIO mapped queues and non MMIO mapped queues.

On Grayskull and Wormhole devices, a queue is mapped to host MMIO if its placed on DRAM channel 0 and fits entirely within the following address range: `[0x30000000, 0x40000000]`. Any queues outside this range will use a significantly slower tilize path, due to hardware limitations.

# Performance
Performance sweeps for tilization over different data formats, input shapes and runtime settings can be found [here](https://tenstorrent.sharepoint.com/sites/Software/Shared%20Documents/Forms/AllItems.aspx?id=%2Fsites%2FSoftware%2FShared%20Documents%2FBack%20End%2FPerf%2Ftilizer%5Fperf&viewid=4e4da101%2De1ec%2D4793%2Daaae%2D4dec478a50ce).

These sweeps were performed by pushing data to queues over multiple loops, without invoking any other part of compile or runtime software. Thus, this measures raw push BW in isolation.

Runtime configurations considered are described in the table below:

|Runtime Setting     |Description|
|----------------|-------------------------------|
|Multithreading (On/Off) |Depending on the CPU and format conversions, the tilizer can use up to 16 threads. |
|CPU Freq. Governor Mode (Performance/OnDemand) |Performance mode uses peak frequency immediately, once the workload starts. OnDemand mode has a frequency ramp up period, which lowers the average bandwidth.|
|CPU Allocator (Enabled/Disabled) |Enabling the CPU allocator limits the number of cores the tilizer has access to. This lowers bandwidth, especially with a higher number of threads.|
| Queue Location (Host/DRAM MMIO/DRAM Non MMIO) | Tilizing into host queues is fastest, since there is no PCIe transfer overhead. Writing to MMIO mapped queues is faster than non MMIO mapped queues, due to TLB reconfiguration overhead.|

Note: Data is arranged in spreadsheets according to the CPU it was collected from. Each document has worksheets corresponding to a specific input shape (`<w, t, mblock_m, mblock_n>`).

To perform such a sweep on a specific machine, the following command can be used:
```python3 model/tilize_untilize_api/tilizer_perf_sweep.py --enable_full_sweep --arch <ARCH_NAME> --loops <LOOP_COUNT>```

Specific parameters to sweep over can be selected with the following arguments:

|Argument    |Description|
|----------------|-------------------------------|
|--tensor_dimensions |<w,  t,  mblock_m,  mblock_n>  |
|--input_format |Float32/Float16/Float16_b/Int8/"*" |
| --output_format  | Float32/Float16/Float16_b/Bfp8/Bfp8_b/"*"|
| --multithreaded_push  | 0/1/"*"|
| --freq_governor_mode | performance/ondemand|
| --input_queue_on_host | 0/1/"*" |
| --non_mmio_mapped_input_queue | 0/1/"*" |
| --enable_cpu_allocator | 0/1/"*" |

# Prestriding
When writing activations for CNNs, this data shuffling step is a precursor to tilization. A description of this step is provided [here](tenstorrent/budabackend#957).

In this document, we discuss the performance characteristics of using different input shapes and stride values, based on varied implementations of this algorithm.

During runtime, the tilizer determines the specific prestriding flow to use based on the input shape and stride.


A list of the different prestriding implementations is provided below, in ascending order of performance, as a function of the inputs.

We use two definitions in this list:
1) An input activation is stride aligned when act_x & act_y are multiples of the stride value
2) An input is AVX aligned (derived from Intel AVX instructions) when either of the following are true: act_format = Float32 and act_x & act_y are multiples of 8 or act_format = Float16 and act_x & act_y are multiples of 16.


|Input Characteristics| Prestriding Flow| 
| --- | ----------- | 
| stride not it set (1,2,4)  | Scalar Prestriding (uses loops to iterate over each datum) | 
| Activations are neither stride aligned nor AVX aligned (after rounding to the nearest multiple of stride)| Intermediate (Vector & Scalar) prestriding after applying zero padding |
| Activations are stride aligned but not AVX aligned* | Intermediate Prestriding |
| Activations are not stride aligned but are AVX aligned after padding*| Vector Prestriding after applying zero padding |
|Both alignment requirements met | Vector Prestriding|


*Performance for these two cases is similar.

 Note: For the last 4 entries in this table, we assume that the stride is in the set (1,2,4).
 
Images/sec values for different activation shapes with stride 2 are provided in the *Prestride Perf Sweep* tab in the spreadsheet linked [here](https://tenstorrent.sharepoint.com/:x:/s/Software/EU6Z3i-kq3FPkvjHiJysqA0B7RQHX2wAfxH102gQuXs71w?e=yxNnFw). This sweep is provided for inputs with shape (x,y) = (223,223) -> (226,226) and covers the last 4 cases in the table above. This spreadsheet also provides performance numbers for ResNet across different host & device data formats, as seen in the backend and from Pybuda.