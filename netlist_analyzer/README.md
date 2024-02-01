# Netlist-Analyzer (aka DPNRA)
The goal of the Data-flow Place aNd Route Analyzer (DPNRA) is to provide feedback on netlist performance, ideal NoC performance (theoretical maximum) , and modelled NoC performance (accounting for effects such as stream gather performance, VC head of line blocking, etc).

Main performance metrics:
- epoch performance measured in clock cycles per tensor (aka clock cycles per input)
- aggregate link (NoC, DRAM, etc) usage

# Build

`make netlist_analyzer/tests`

# Running the analyzer
After build and installing the visualizer dependencies the analyzer can be run for grayskull or wormhole b0 architectures:
 `build/test/netlist_analyzer/tests/test_netlist_analyzer --run-net2pipe --netlist <path to netlist> --arch {wormhole_b0, grayskull}`

A console report of the worst op within the netlist, most utilized links, and cycles per tensor is reported along with a fully serialized model of the architecture that can be input to the visualizer are stored in `analyzer_out/<netlist name>/analyzer_output_temporal_epoch_##.yaml`

## Grayskull
`ARCH_NAME=grayskull build/test/netlist_analyzer/tests/test_netlist_analyzer --run-net2pipe --arch grayskull --netlist <path to netlist>`
  
## Wormhole B0
`ARCH_NAME=wormhole_b0 build/test/netlist_analyzer/tests/test_netlist_analyzer --run-net2pipe --arch wormhole_b0 --netlist <path to netlist>`

# Route-UI Visualizer
The latest visualizer build can be found here:
https://yyz-gitlab.local.tenstorrent.com/tenstorrent/route-ui/-/releases/0.0.7

Feedback issue thread:
https://yyz-gitlab.local.tenstorrent.com/tenstorrent/route-ui/-/issues/7
