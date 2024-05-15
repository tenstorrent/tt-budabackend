This directory should contain all common code (libraries, apis, functions) for any riscv processor.

Currently it containes code that it not exclusive to riscv processors but this should be changed.

## TT_LOG
* Runtime requirements - set environment variable ```ENABLE_TT_LOG=1``` before running a test on device.
It will create logging server inside backend which is used to poll messages from L1.

Riscv needs to include ```"tt_log.h"``` and use one of predefined MACROs for logging. 

There are two types of logging, using mailbox address or debug buffer.

### Logging using mailbox
Backend is polling mailbox constatnyl, and if it finds that message is there, it will execute 
Mailbox size is 64 bytes.
First 8 bytes are message header, and used to mark that new message exists and to write message identifier.
Rest 56 bytes represents space for message arguments. There can be up to 14 arguments. Size of each argument is 4 bytes.

```cpp
// Direct logging to console.
// Block kernel/firmware until kernel clears message header(first 8 bytes in mailbox).
std::uint32 value = 1;
TT_LOG("I want to log something {}", value);

// Similar to TT_LOG, but kernel is not blocked.
// It does not gurantee that mailbox message will not be overwritten.
TT_LOG_NB("I want to log something without blocking kernel {}", value);

// Direct logging to console with pausing backend.
// Blocks kernel/firmware until user explicitely continue.
TT_PAUSE("I am pausing when i = {}", value);

// Blocks kernel/firmware and waits for backend to exit.
// Backend will log assert to console.
log_assert("Exit because i = {}", value);
```

### Logging into debug buffer
Each kernel has 2kb of L1 space that can be used for logging. Messages are written into buffer without kernel blocking,
but if end of buffer is reached, no more messages will be stored in buffer. In this case, backend does not poll silicon,
constantly, but it will only poll it once when program is done.

```cpp
// Logs to console after run_program is done.
TT_DUMP_LOG("I want to log something to debug buffer {}", value);

// Asserts after run_program is done.
TT_DUMP_ASSERT("I want to asssert using debug buffer {}", value);
```