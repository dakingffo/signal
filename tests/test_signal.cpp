#include <gtest/gtest.h>
#include <stdexec/execution.hpp>
#include <exec/static_thread_pool.hpp>
#include <exec/async_scope.hpp>
#include <atomic>
#include <string>

#include "signal.hpp"

using namespace daking;
using namespace stdexec;

// --- Signal Definitions ---
struct Tick : daking::signal<int> {};
struct Msg  : daking::signal<std::string> {};
struct Identity : daking::signal<std::thread::id> {};

// --- Emitter Components ---
struct BaseEmitter : enable_signal<Tick, Msg> {
    void do_tick(int i) { emit(Tick{i}, broadcast, this); }
    void do_msg(std::string s) { emit(Msg{ std::move(s)}, broadcast, this); }
};

struct DerivedEmitter : BaseEmitter, enable_signal<Identity> {
    void do_id() { emit(Identity{std::this_thread::get_id()}, broadcast, this); }
};

// --- Test Suite ---
class SignalTest : public ::testing::Test {
protected:
    exec::static_thread_pool pool_{4};
    decltype(pool_.get_scheduler()) sch_ = pool_.get_scheduler();
};

// 1. Basic Functionality and Type-Safe Routing
TEST_F(SignalTest, BasicConnectAndEmit) {
    BaseEmitter emitter;
    int result = 0;
    std::string message;

    // Connect using stdexec closures
    daking::connect<Tick>(emitter, then([&](int i) { result = i; }));
    daking::connect<Msg>(emitter, then([&](std::string s) { message = s; }));

    emitter.do_tick(42);
    emitter.do_msg("hello");

    EXPECT_EQ(result, 42);
    EXPECT_EQ(message, "hello");
}

// 2. Logical Control: Enable/Disable Gate (O(1))
TEST_F(SignalTest, LogicalGating) {
    BaseEmitter emitter;
    int count = 0;

    auto con = daking::connect<Tick>(emitter, then([&](int) { count++; }));

    emitter.do_tick(1);
    EXPECT_EQ(count, 1);

    con.disable(); // Atomic release store
    emitter.do_tick(1);
    EXPECT_EQ(count, 1); // Should not increase

    con.enable();
    emitter.do_tick(1);
    EXPECT_EQ(count, 2);
}

// 3. Physical Removal: Disconnect (COW)
TEST_F(SignalTest, PhysicalDisconnect) {
    BaseEmitter emitter;
    int count = 0;

    daking::connection<Tick> auto con = daking::connect<Tick>(emitter, then([&](int) { count++; }));

    emitter.do_tick(1);
    EXPECT_TRUE(disconnect<Tick>(emitter, con));

    emitter.do_tick(1);
    EXPECT_EQ(count, 1); // Slot is physically gone
    
    // Subsequent operations on connection should fail gracefully
    EXPECT_FALSE(con.enable()); 
}

// 4. Thread Pool Integration & Async Release
TEST_F(SignalTest, AsyncExecutionInPool) {
    std::atomic<int> async_count{0};
    DerivedEmitter emitter;
    const int iterations = 100;

    // "Stitch" the signal into the thread pool
    daking::connect<Tick>(emitter, 
        continues_on(sch_) | then([&async_count](int) {
            async_count++;
        })
    );

    for(int i = 0; i < iterations; ++i) {
        emitter.do_tick(i);
    }

    // emitter_impl destructor will call sync_wait(on_empty())
    // ensuring all 100 tasks complete before we verify.
}

// 5. Mixin Inheritance Routing
TEST_F(SignalTest, InheritanceRouting) {
    DerivedEmitter emitter;
    bool tick_called = false;
    bool id_called = false;

    // signal_cast resolves the correct unit in the inheritance tree
    daking::connect<Tick>(emitter, then([&](int) { tick_called = true; }));
    daking::connect<Identity>(emitter, then([&](auto) { id_called = true; }));

    emitter.do_tick(1);
    emitter.do_id();

    EXPECT_TRUE(tick_called);
    EXPECT_TRUE(id_called);
}

// 6. Multiple Connections to Same Signal
TEST_F(SignalTest, MultiSlotCOW) {
    BaseEmitter emitter;
    int a = 0, b = 0;

    daking::connection<Tick> auto c1 = daking::connect<Tick>(emitter, then([&](int i) { a += i; }));
    daking::connection<Tick> auto c2 = daking::connect<Tick>(emitter, then([&](int i) { b += i; }));

    emitter.do_tick(10);
    EXPECT_EQ(a, 10);
    EXPECT_EQ(b, 10);

    disconnect<Tick>(emitter, c1);
    emitter.do_tick(10);
    EXPECT_EQ(a, 10); // Stayed same
    EXPECT_EQ(b, 20); // Increased
}