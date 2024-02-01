### Summary

(Summarize the bug encountered concisely)

### Priority

Also specify if workarounds are desired while a fix is being developed.

### Netlist(s)

Please attach the netlists or provide them inline

### Cluster Description Files 

N/A if single chip. Else please attach the cluster description file or provide it inline

### Branch/Commits

- Pybuda commit/branch (if from Pybuda)

- Budabackend commit/branch


### Work-arounds

If the failure is "Failed to Route", try enabling FW managed ethernet streams with `TT_ENABLE_FW_ETH_STREAMS=1`. Does the test still fail?

If the failure is reported in router, what happens when running with `ROUTER_SKIP_RESOURCE_VALIDATION_CHECK=1`?

### Suggested fixes

(If you have a suggestion)

/assign @snijjar
/label ~Net2pipe::Router ~Bug