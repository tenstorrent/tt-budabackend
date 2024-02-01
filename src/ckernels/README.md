# CKernels

## Debug Tracing

Debug tracing is implemented as follows: the kernel writes data to L1 memory, and the backend runtime reads from L1 memory and logs the information. During the kernel compilation process, the source code is analyzed to identify all logging macros, resulting in the creation of an "fwlog" file containing a comprehensive list of log messages and their corresponding hash values.

### Log Message Structure

A typical log message in L1 consists of three components:

- **Message Valid Identifier:** This is a 4-byte value.
- **Message ID:** It's a 4-byte hash value generated from the message string.
- **Parameters:** Up to 14 parameters, each represented as a 4-byte value.

### Realtime Logging

Realtime logging is employed during the execution of the kernel. The backend runtime continuously monitors L1 memory, logging messages to the console as soon as they are written to L1. For this purpose, various macros, including `TT_LOG`, `TT_LOG_NB`, `TT_RISC_ASSERT`, `TT_PAUSE`, and `TT_LLK_DUMP`, are used.

### Post-execution Logging

Post-execution logging occurs after the kernel execution. The backend runtime waits for the kernel to finish execution and then reads a dedicated log dump buffer. This type of logging continues until the buffer is full, after which messages are silently discarded. Macros like `TT_DUMP_LOG` and `TT_DUMP_ASSERT` are used for post-execution logging.

### Example Usage

To illustrate, here's an example of using the `TT_LOG` macro:

```cpp
TT_LOG("Hello world {}!\n", 3);
```

### Configuration for Debugging

Before launching the budabackend runtime, you'll need to configure specific runtime environment variables to control debugging and logging within the kernel.

- KERNEL_DEBUG_SYMBOLS
    * *Value*: 1 (Enable) or 0 (Disable)
    * *Description*: Toggle the generation of debug symbols during kernel compilation. Set to 1 for symbol generation and 0 to disable it.

- ENABLE_TT_LOG
    * *Value*: 1 (Enable) or 0 (Disable)
    * *Description*:    Determine whether custom logging is activated in the kernel. <br>
        Enabling this variable (1) allows the use of custom logging macros such as ```TT_LOG, TT_LOG_NB, TT_LOG_DEBUG, TT_PAUSE, TT_RISC_ASSERT, TT_DUMP_LOG and TT_DUMP_ASSERT```.<br>
        By default, this feature is enabled if CONFIG=debug is set.

- ENABLE_TT_LLK_DUMP
    * *Value*: 1 (Enable) or 0 (Disable)
    * *Description*: Control the logging of top-level LLK function names along with their argument values. Setting this variable to 1 enables the use of the TT_LLK_DUMP macro for this purpose.

```bash
KERNEL_DEBUG_SYMBOLS=1 ENABLE_TT_LOG=1 ENABLE_TT_LLK_DUMP=1 ./build/tests/verif/op_tests/test_op --netlist ./verif/op_tests/netlists/netlist_unary_op.yaml --silicon
```
