# **Summary**

# **Steps to reproduce**


# **Checklists**

## $`\textcolor{purple}{\text{Front End}}`$ 
- run with dram decoupled and see if any change
- calculate theoretical pipeline rate for the graph in question (tensors/sec)
- check buffering on fork/join paths and ensure it's sufficient
- provide measured pipeline rate (the epoch throughput)
- identify ops that seem to be problematic like Stan did in recent debug of bert training and resnet inference

## $`\textcolor{blue}{\text{Ops/Milos}}`$ 
- review highlighted ops parameters and operation in netlist and provide comments
- review perf dumps and provide comments

## $`\textcolor{darkblue}{\text{PerfAnalysis/Jackson}}`$ 
- isolate the suspected 2-3 ops and make a test netlist that has them in the same locations and configs, aiming to allow proving the problem in question. For example - for the recent resnet debug - make a netlist with the op that is forking to another op and dram; feed it with a 'feeder'; drain the fork with a drainer; keep the write to dram. See what happens.

## $`\textcolor{lightblue}{\text{DataMovement/Radomir}}`$ 
- investigate noc/pipes/dram in small netlist, larger netlist

/assign @jnie @macimovic @rjakovljevic
/label ~Perf
