#include <benchmark/benchmark.h>
#include <exec/static_thread_pool.hpp>
#include <stdexec/execution.hpp>
#include "signal.hpp"

using namespace daking;
using namespace stdexec;

struct BenchSignal : signal<int> {};

class BenchEngine : public enable_signal<BenchSignal> {};

static void BM_Signal_Dispatch_Overhead(benchmark::State& state) {
    const size_t SLOTS_NUM = state.range(0);
    BenchEngine engine;
    for(int i = 0; i < SLOTS_NUM; ++i) {
        daking::connect<BenchSignal>(engine, then([](int){}));
    }

    for (auto _ : state) {
        emit(BenchSignal{42}, daking::broadcast, engine);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Signal_Dispatch_Overhead)
    ->Arg(10)
    ->Arg(100)
    ->Arg(1000)
    ->UseRealTime();

BENCHMARK_MAIN();

/*
Run on (16 X 3992.06 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x8)
  L1 Instruction 32 KiB (x8)
  L2 Unified 1024 KiB (x8)
  L3 Unified 16384 KiB (x1)
Load Average: 0.57, 0.22, 0.23
-----------------------------------------------------------------------------------------------------
Benchmark                                           Time             CPU   Iterations UserCounters...
-----------------------------------------------------------------------------------------------------
BM_Signal_Dispatch_Overhead/10/real_time          148 ns          148 ns      4860493 items_per_second=6.76604M/s
BM_Signal_Dispatch_Overhead/100/real_time        1419 ns         1419 ns       491056 items_per_second=704.489k/s
BM_Signal_Dispatch_Overhead/1000/real_time      13579 ns        13579 ns        50930 items_per_second=73.6406k/s
*/