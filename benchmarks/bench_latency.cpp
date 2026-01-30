#include <benchmark/benchmark.h>
#include <hdr/hdr_histogram.h>
#include <stdexec/execution.hpp>
#include "signal.hpp"

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#include <intrin.h>
#else
#include <sched.h>
#include <pthread.h>
#include <x86intrin.h>
#endif

using namespace daking;
using namespace stdexec;

const double CYCLES_PER_NS = 3.992; 

void pin_thread(int cpu_id) {
#if defined(_WIN32) || defined(_WIN64)
    SetThreadAffinityMask(GetCurrentThread(), (static_cast<DWORD_PTR>(1) << cpu_id));
#else
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu_id, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#endif
}

struct BenchmarkSignal : signal<int> {};
class BenchmarkEngine : public enable_signal<BenchmarkSignal> {};

static void BM_Signal_RawLogic_Latency_HDR(benchmark::State& state) {
    BenchmarkEngine engine;
    hdr_histogram* hist;
    hdr_init(1, 1000000, 3, &hist);

    pin_thread(1);

    auto conn = daking::connect<BenchmarkSignal>(engine, 
        then([](int val) {
            benchmark::DoNotOptimize(val);
        })
    );

    for (auto _ : state) {
        for (int i = 0; i < 10000; ++i) {
            uint64_t start = __rdtsc();
            

            emit(BenchmarkSignal{42}, daking::broadcast, engine);
            
            uint64_t end = __rdtsc();
            hdr_record_value(hist, end - start);
        }
    }

    state.counters["P50_ns"]   = hdr_value_at_percentile(hist, 50.0) / CYCLES_PER_NS;
    state.counters["P99_ns"]   = hdr_value_at_percentile(hist, 99.0) / CYCLES_PER_NS;
    state.counters["P99.9_ns"] = hdr_value_at_percentile(hist, 99.9) / CYCLES_PER_NS;

    hdr_close(hist);
}

BENCHMARK(BM_Signal_RawLogic_Latency_HDR)->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN();

/*
Running /home/daking/PROJECT/signal/out/build/linux-clang-release/signal_bench_latency
Run on (16 X 3992.06 MHz CPU s)
CPU Caches:
  L1 Data 32 KiB (x8)
  L1 Instruction 32 KiB (x8)
  L2 Unified 1024 KiB (x8)
  L3 Unified 16384 KiB (x1)
Load Average: 0.57, 0.27, 0.24
-----------------------------------------------------------------------------------------
Benchmark                               Time             CPU   Iterations UserCounters...
-----------------------------------------------------------------------------------------
BM_Signal_RawLogic_Latency_HDR        313 us          304 us         2268 P50_ns=20.0401 P99.9_ns=60.1202 P99_ns=30.0601
*/