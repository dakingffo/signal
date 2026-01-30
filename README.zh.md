# Signal

[![C++](https://img.shields.io/badge/C++-20-blue.svg?logo=cplusplus)](https://isocpp.org/)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

![GCC](https://img.shields.io/badge/GCC-green?logo=gcc)
![Clang](https://img.shields.io/badge/Clang-orange?logo=llvm)

![ASAN Status](https://img.shields.io/badge/ASAN-Pass-brightgreen?logo=llvm)
![TSAN Status](https://img.shields.io/badge/TSAN-Pass-brightgreen?logo=llvm)

*lightweight signal-slot based on stdexec*

*基于stdexec的信号槽*

[English](./README.md) 

基于stdexec设计的信号与槽，同时提供QtLike的朴素`signal-slot`和`sender/receiver`风格的强类型系统。

## 快速上手

`emitter be like:`
```C++
    using daking::signal;
    using daking::enable_signal;
    struct MyEmitter : enable_signal<signal<int>, signal<void>> {
        ...
    }; // 可以发送signal<int>和signal<void>的emitter;

    // 区分同参数signal
    struct IntSignal : signal<int> { using base::base; /* 继承构造函数*/ };

    // 扩展发送能力
    struct MyEmitter2 : MyEmitter, enable_signal<IntSignal> {
        ...
    }; // 可以发送signal<int>、signal<void>以及 IntSignal。
```
每个`emitter`内嵌一个`stdexec::async_scope`,等待正在发生的槽函数事件事件完成，用以满足结构化并发要求。

`connect/emit usage:`
```C++
    using daking::signal;
    using daking::enable_signal;
    struct Emitter : enable_signal<signal<int>, signal<void>> {
    } emitter;

    daking::connect<signal<void>>(emitter, stdexec::just(42) | stdexec::then([](int num) { std::cout << num << std::endl;}));
    daking::connect<signal<int>>(emitter, stdexec::then([](int num) { std::cout << num << std::endl;}));

    daking::emit(signal<void>{}, daking::broadcast, emitter);     // print: 42
    daking::emit(signal<int>{128}, daking::broadcast, emitter);   // print: 128
    // broadcast标签: 向所有槽广播信号，但不关注任何槽的运行情况

    // 重载>>，使用此运算符是表明它有“发出信号”的副作用。
    signal<int>{128} >> daking::emit(daking::broadcast, emitter); // print: 128
```
继承了`signal<void>`的`signal`对应的槽函数是可拷贝`stdexec::sender`，
其余的`signal`对应的槽函数是可拷贝`stdexec::sender_adaptor_closure`，且调用签名与`signal`携带参数相同。
每个信号可以对应多个槽函数。因此要求非`void`信号的参数必须可拷贝。

`connection management:`
```C++
    using daking::signal;
    using daking::enable_signal;
    using daking::connection;
    struct Emitter : enable_signal<signal<int>> {
    } emitter;

    connection<signal<int>> auto con1 = daking::connect<signal<int>>(emitter, 
        stdexec::then([](int num) { return num * 2;}));
    connection<signal<int>> auto con2 = daking::connect<signal<int>>(emitter, 
        stdexec::then([](int num) { return num / 2;}));

    con1.disable(); // 对应的槽函数被停用
    con1.enable();  // 对应的槽函数被启用

    auto sender1 = signal<int>{42} >> daking::emit(con1, con2) 
                 | stdexec::then([](int l, int r) { std::cout << l + r << std::endl; });
    stdexec::sync_wait(std::move(sender1)); // print 105 (42 * 2 + 42 / 2)
    // 无标签: 不依赖链接端点：这两个链接的emitter不必是相同的

    auto sender2 = signal<int>{42} >> daking::emit(daking::capture, emitter, con1, con2) 
                 | stdexec::then([](int l, int r) { std::cout << l + r << std::endl; });
    stdexec::sync_wait(std::move(sender2)); // print 105
    // capture标签: 向所有槽函数发送信号，但捕获特定connection的槽函数的结果

    daking::disconnect<signal<int>>(emitter, con1) // 彻底移除con1(正在执行的槽函数不受影响)
```
`connection`是一个概念，表明此链接的信号类型。它是一个轻量级句柄，不涉及生命周期管理，如果你不需要它，那么就无视它。

## Benchmark

Run on (16 X 3992.06 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x8)
  L1 Instruction 32 KiB (x8)
  L2 Unified 1024 KiB (x8)
  L3 Unified 16384 KiB (x1)

### 1. 不同槽（Slot）数量下的信号发射开销

这组数据展示了随着连接到信号上的槽数量增加，系统每秒钟能够处理的信号发射次数（吞吐量）。

| 槽数量 (Slots) | 耗时 (Time) | CPU 时间 | 迭代次数 | 发射能力 (items/s) |
| --- | --- | --- | --- | --- |
| **10** | 148 ns | 148 ns | 4,860,493 | **6.76 M/s** |
| **100** | 1,419 ns | 1,419 ns | 491,056 | **704.49 k/s** |
| **1000** | 13,579 ns | 13,579 ns | 50,930 | **73.64 k/s** |

---

### 2. 单次信号发送延迟 (HDR Histogram)

这组数据展示了在处理原始逻辑（Raw Logic）时，信号发射的延迟分布情况。通过 P50、P99 等分位数反映了系统的稳定性。

| 测试项 | P50 延迟 (中位数) | P99 延迟 | P99.9 延迟 | 迭代次数 |
| --- | --- | --- | --- | --- |
| **单次发送延迟 (HDR)** | **20.04 ns** | **30.06 ns** | **60.12 ns** | 2,268 |


## 安装 (Installation)

只需在您的项目中包含 `./include/signal.hpp` 文件即可（依赖于stdexec）。
也提供CMake复现BENCHMARK测试以及构建example和test用例。

## 许可证 (LICENSE)

daking::signal 使用 [MIT 许可证](./LICENSE.txt) 授权。