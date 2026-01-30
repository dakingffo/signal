# Signal

*Lightweight signal-slot based on stdexec*

[Chinese](./README.zh.md)

A signal-slot system designed based on `stdexec`, providing both a Qt-like simple `signal-slot` mechanism and a strongly-typed `sender/receiver` style system.

## Quick Start

`emitter be like:`

```c++
    using daking::signal;
    using daking::enable_signal;
    
    // An emitter capable of sending signal<int> and signal<void>
    struct MyEmitter : enable_signal<signal<int>, signal<void>> {
        ...
    };

    // Differentiating signals with identical parameters
    struct IntSignal : signal<int> { using base::base; /* Inherit constructors */ };

    // Extending emission capabilities
    struct MyEmitter2 : MyEmitter, enable_signal<IntSignal> {
        ...
    }; // Can now emit signal<int>, signal<void>, and IntSignal.

```

Each `emitter` embeds a `stdexec::async_scope` to ensure that ongoing slot events are completed, satisfying structured concurrency requirements.

`connect/emit usage:`

```c++
    using daking::signal;
    using daking::enable_signal;
    struct Emitter : enable_signal<signal<int>, signal<void>> {
    } emitter;

    // Connecting a void signal to a sender
    daking::connect<signal<void>>(emitter, stdexec::just(42) | stdexec::then([](int num) { std::cout << num << std::endl;}));
    
    // Connecting an int signal to a sender adaptor closure
    daking::connect<signal<int>>(emitter, stdexec::then([](int num) { std::cout << num << std::endl;}));

    daking::emit(signal<void>{}, daking::broadcast, emitter);     // prints: 42
    daking::emit(signal<int>{128}, daking::broadcast, emitter);   // prints: 128
    // 'broadcast' tag: Broadcasts the signal to all slots without tracking their execution.

    // Overloaded >> operator: Indicates the side effect of "emitting a signal".
    signal<int>{128} >> daking::emit(daking::broadcast, emitter); // prints: 128

```

Signals inheriting from `signal<void>` correspond to slots that are copyable `stdexec::sender` objects. Other `signal` types correspond to slots that are copyable `stdexec::sender_adaptor_closure` objects, with call signatures matching the signal parameters. Since a single signal can be dispatched to multiple slots, parameters for non-void signals must be copyable.

`Connection management:`

```c++
    using daking::signal;
    using daking::enable_signal;
    using daking::connection;
    struct Emitter : enable_signal<signal<int>> {
    } emitter;

    connection<signal<int>> auto con1 = daking::connect<signal<int>>(emitter, 
        stdexec::then([](int num) { return num * 2;}));
    connection<signal<int>> auto con2 = daking::connect<signal<int>>(emitter, 
        stdexec::then([](int num) { return num / 2;}));

    con1.disable(); // Deactivates the corresponding slot
    con1.enable();  // Re-activates the corresponding slot

    auto sender1 = signal<int>{42} >> daking::emit(con1, con2) 
                 | stdexec::then([](int l, int r) { std::cout << l + r << std::endl; });
    stdexec::sync_wait(std::move(sender1)); // prints 105 (42 * 2 + 42 / 2)
    // No tag: Independent of endpoints; these connections do not need to belong to the same emitter.

    auto sender2 = signal<int>{42} >> daking::emit(daking::capture, emitter, con1, con2) 
                 | stdexec::then([](int l, int r) { std::cout << l + r << std::endl; });
    stdexec::sync_wait(std::move(sender2)); // prints 105
    // 'capture' tag: Broadcasts to all slots but captures results specifically from the selected connections.

    daking::disconnect<signal<int>>(emitter, con1); // Permanently removes con1 (active executions are unaffected).

```

The `connection` is a concept representing the signal type of the link. It is a lightweight handle that does not involve lifetime management; if you don't need it, you can simply ignore it.

## Benchmark

Run on (16 X 3992.06 MHz CPU s)
CPU Caches:
L1 Data 32 KiB (x8)
L1 Instruction 32 KiB (x8)
L2 Unified 1024 KiB (x8)
L3 Unified 16384 KiB (x1)

### 1. Signal Dispatch Overhead (by Slot Count)

This data demonstrates the system's throughput (signals processed per second) as the number of slots connected to a signal increases.

| Slot Count | Latency (Time) | CPU Time | Iterations | Dispatch Throughput |
| --- | --- | --- | --- | --- |
| **10** | 148 ns | 148 ns | 4,860,493 | **6.76 M/s** |
| **100** | 1,419 ns | 1,419 ns | 491,056 | **704.49 k/s** |
| **1000** | 13,579 ns | 13,579 ns | 50,930 | **73.64 k/s** |

---

### 2. Single Signal Latency (HDR Histogram)

This data shows the latency distribution when processing raw logic, reflecting system stability via P50 and P99 quantiles.

| Test Case | P50 (Median) | P99 Latency | P99.9 Latency | Iterations |
| --- | --- | --- | --- | --- |
| **Single Signal Latency (HDR)** | **20.04 ns** | **30.06 ns** | **60.12 ns** | 2,268 |

## Installation

Simply include the `./include/signal.hpp` file in your project (requires `stdexec`).
A CMake configuration is also provided to reproduce BENCHMARK tests and build examples and test cases.

## License

daking::signal is licensed under the [MIT License](./LICENSE.txt).
