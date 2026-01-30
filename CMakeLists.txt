cmake_minimum_required(VERSION 3.14)
project(DakingSignals CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_BUILD_TYPE MATCHES "Release")
    set(DEFAULT_RELEASE_OPTS "$<$<CXX_COMPILER_ID:MSVC>:/O2 /Ob2>$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-O3>")
endif()

include(FetchContent)

# Google Benchmark
FetchContent_Declare(
    googlebenchmark
    URL https://github.com/google/benchmark/archive/refs/tags/v1.8.3.zip
    FIND_PACKAGE_ARGS NAMES benchmark CONFIG
)
set(BENCHMARK_ENABLE_TESTING OFF)
FetchContent_MakeAvailable(googlebenchmark)

# Google Test
enable_testing()
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
    FIND_PACKAGE_ARGS NAMES GTest CONFIG
)
FetchContent_MakeAvailable(googletest)

# HdrHistogram
FetchContent_Declare(
    hdrhistogram
    URL https://github.com/HdrHistogram/HdrHistogram_c/archive/refs/tags/0.11.8.zip
)
set(HDR_HISTOGRAM_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(HDR_HISTOGRAM_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(HDR_LOG_REQUIRED "DISABLED" CACHE STRING "" FORCE)
FetchContent_MakeAvailable(hdrhistogram)

# stdexec
FetchContent_Declare(
    stdexec
    GIT_REPOSITORY https://github.com/NVIDIA/stdexec.git
    GIT_TAG        main
    FIND_PACKAGE_ARGS NAMES stdexec CONFIG
)
FetchContent_MakeAvailable(stdexec)
if(TARGET stdexec::stdexec)
    get_target_property(STDEXEC_INCLUDE_DIR stdexec::stdexec INTERFACE_INCLUDE_DIRECTORIES)
else()
    set(STDEXEC_INCLUDE_DIR ${stdexec_SOURCE_DIR}/include)
endif()

find_package(Threads REQUIRED)

set(COMMON_COMPILE_OPTS
    PRIVATE
        ${DEFAULT_RELEASE_OPTS}
        $<$<CXX_COMPILER_ID:MSVC>:/EHsc>
)

set(COMMON_DEFINITIONS
    PRIVATE
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:STDEXEC_NO_INTRIN_H __linux__>
)

set(COMMON_INCLUDES
    PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/include 
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${STDEXEC_INCLUDE_DIR}
        ${hdrhistogram_SOURCE_DIR}/src
)

set(COMMON_LIBS
    PRIVATE 
        Threads::Threads
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:atomic>
)

#BENCHMARK
add_executable(signal_bench_overhead benchmarks/bench_overhead.cpp)
target_include_directories(signal_bench_overhead ${COMMON_INCLUDES})
target_link_libraries(signal_bench_overhead 
    PRIVATE 
        benchmark::benchmark_main 
        ${COMMON_LIBS}
)
target_compile_options(signal_bench_overhead ${COMMON_COMPILE_OPTS})
target_compile_definitions(signal_bench_overhead ${COMMON_DEFINITIONS})

add_executable(signal_bench_latency benchmarks/bench_latency.cpp)
target_include_directories(signal_bench_latency ${COMMON_INCLUDES})
target_link_libraries(signal_bench_latency 
    PRIVATE 
        benchmark::benchmark_main 
        hdr_histogram_static 
        ${COMMON_LIBS}
)
target_compile_options(signal_bench_latency ${COMMON_COMPILE_OPTS})
target_compile_definitions(signal_bench_latency ${COMMON_DEFINITIONS})

#TEST
file(GLOB TEST_SRCS "tests/*.cpp")
add_executable(signal_tests ${TEST_SRCS})
target_include_directories(signal_tests ${COMMON_INCLUDES})
target_link_libraries(signal_tests 
    PRIVATE 
        GTest::gtest_main 
        GTest::gmock 
        ${COMMON_LIBS}
)
target_compile_options(signal_tests ${COMMON_COMPILE_OPTS})
target_compile_definitions(signal_tests ${COMMON_DEFINITIONS})
include(GoogleTest)
gtest_discover_tests(signal_tests)

#EXAMPLE
add_executable(signal_event_bus examples/event_bus.cpp)
target_include_directories(signal_event_bus ${COMMON_INCLUDES})
target_link_libraries(signal_event_bus PRIVATE ${COMMON_LIBS})
target_compile_options(signal_event_bus ${COMMON_COMPILE_OPTS})
target_compile_definitions(signal_event_bus ${COMMON_DEFINITIONS})