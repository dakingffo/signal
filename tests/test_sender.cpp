#include <gtest/gtest.h>
#include "signal.hpp" 
#include <string>
#include <vector>
#include <memory>

using namespace daking;

// Define a test signal and an emitter that enables it
using TestSignal = daking::signal<int, std::string>;
struct TestEmitter : enable_signal<TestSignal> {};

// --- Test Fixture ---
class SenderTest : public ::testing::Test {
protected:
    TestEmitter emitter;
};

// 1. Test Point-to-Point Aggregation (P2P Aggregate)
// Ensures multiple connections can be executed simultaneously and their results aggregated into a tuple.
TEST_F(SenderTest, AggregateMultipleConnections) {
    // Connect slots
    connection<TestSignal> auto con1 = daking::connect<TestSignal>(emitter, 
        stdexec::then([](int i, std::string s) {
            return i + 10;
        }));
    connection<TestSignal> auto con2 = daking::connect<TestSignal>(emitter, 
        stdexec::then([](int i, std::string s) {
            return s + " world";
        }));

    // Emit signal to specific connections and aggregate results
    auto composite_sender = TestSignal{5, "hello"} >> emit(con1, con2);

    auto result = stdexec::sync_wait(std::move(composite_sender));
    ASSERT_TRUE(result.has_value());
    auto [resA, resB] = result.value();

    EXPECT_EQ(resA, 15);
    EXPECT_EQ(resB, "hello world");
}

// 2. Test Disable/Enable Functionality
TEST_F(SenderTest, ConnectionDisableTest) {
    auto con = daking::connect<TestSignal>(emitter, stdexec::then([](int i, std::string s) { 
        return i; 
    }));

    // --- Disable the connection ---
    con.disable();

    // Note: operator>> returns a sender; it should not throw immediately
    auto sender = TestSignal(1, "test") >> daking::emit(con);
    
    // Execution of the sender should fail and throw when the connection is disabled
    EXPECT_THROW({
        stdexec::sync_wait(std::move(sender));
    }, std::runtime_error);

    // --- Restore the connection ---
    con.enable();
    auto [res] = *stdexec::sync_wait(TestSignal(42, "work") >> daking::emit(con));
    EXPECT_EQ(res, 42);
}

// 3. Test Safety after Emitter Destruction
TEST_F(SenderTest, EmitterDestructionSafety) {
    // Define slot logic using a named lambda for type consistency
    auto slot_logic = stdexec::then([](int i, std::string s) { return i; });
    
    using ConType = decltype(daking::connect<TestSignal>(std::declval<TestEmitter&>(), std::move(slot_logic)));
    std::optional<ConType> con_storage;

    {
        TestEmitter local_emitter;
        auto con = daking::connect<TestSignal>(local_emitter, std::move(slot_logic));
        con_storage.emplace(std::move(con));
        // local_emitter is destroyed here, triggering scope_.on_empty()
    }

    // Emitter is gone; the internal weak_ptr in the connection should fail to lock
    auto dead_sender = TestSignal(1, "dead") >> daking::emit(con_storage.value());

    // Verify that the error is caught during asynchronous execution
    try {
        stdexec::sync_wait(std::move(dead_sender));
        FAIL() << "Should have thrown a runtime_error";
    } catch (const std::runtime_error& e) {
        EXPECT_STREQ(e.what(), "Can't create sender: the connection has been closed.");
    }
}

// 4. Test Void Signal Chain
// Validates that signals with no arguments can still drive a pipeline and return values.
using VoidSignal = daking::signal<void>;
struct VoidEmitter : enable_signal<VoidSignal> {};

TEST(GiantChainTest, VoidToIntChain) {
    VoidEmitter emitter;
    
    auto slot_sender = stdexec::just() 
                     | stdexec::then([]() { return 100; });
    
    auto con = daking::connect<VoidSignal>(emitter, std::move(slot_sender));

    auto chain = VoidSignal{} >> daking::emit(con)
               | stdexec::then([](int val) {
                   return val * 2; // Expected: 200
               })
               | stdexec::then([](int val) -> std::string {
                   return "Result: " + std::to_string(val);
               });

    auto [result] = *stdexec::sync_wait(std::move(chain));
    
    EXPECT_EQ(result, "Result: 200");
}

// 5. Test Complex Data Structures in Giant Chain
struct StudentRecord {
    std::string name;
    int age;
    std::vector<int> scores;
    std::shared_ptr<std::string> metadata; // Simulation of shared resources
};

using ComplexSignal = daking::signal<StudentRecord, double>; // Record and weighting factor
struct ComplexEmitter : daking::enable_signal<ComplexSignal> {};

TEST(GiantChainTest, ComplexDataToGiantChain) {
    ComplexEmitter emitter;

    // Define slot: calculates weighted average
    auto processing_slot = stdexec::then([](StudentRecord record, double weight) {
        if (record.scores.empty()) return 0.0;
        
        double sum = 0;
        for (int s : record.scores) sum += s;
        
        double average = sum / record.scores.size();
        return average * weight; 
    });

    auto con = daking::connect<ComplexSignal>(emitter, std::move(processing_slot));

    // Prepare complex test data
    StudentRecord student{
        "Alice", 
        20, 
        {85, 90, 95, 80}, 
        std::make_shared<std::string>("Spring_2026")
    };

    // Build chain: Signal Emit -> Weighted Calc -> Grade Assignment -> String Formatting
    auto chain = ComplexSignal{std::move(student), 1.1} >> daking::emit(con)
               | stdexec::then([](double final_score) {
                   // Categorize the weighted score
                   if (final_score >= 100.0) return 'S';
                   if (final_score >= 90.0)  return 'A';
                   return 'B';
               })
               | stdexec::then([](char grade) {
                   return std::string("Final Grade: ") + grade;
               });

    auto [result] = *stdexec::sync_wait(std::move(chain));

    // Validation Logic:
    // Avg: (85+90+95+80)/4 = 87.5
    // Weighted: 87.5 * 1.1 = 96.25 -> Result: Grade A
    EXPECT_EQ(result, "Final Grade: A");
}

// 6. Test Basic Capture Functionality
// Capture allows broadcasting to all slots while specifically returning results from selected connections.
TEST_F(SenderTest, CaptureBasicSuccess) {
    int broadcast_count = 0;

    // Slot 1: Connection intended for result capture
    auto con1 = daking::connect<TestSignal>(emitter, stdexec::then([](int i, std::string s) {
        return i + 1; 
    }));

    // Slot 2: Background broadcast slot (simulates side effects, not part of capture)
    auto con_bg = daking::connect<TestSignal>(emitter, stdexec::then([&](int i, std::string s) {
        broadcast_count++; 
    }));

    // Execute Capture
    auto sender = daking::emit(TestSignal{10, "capture"}, daking::capture, emitter, con1);
    
    auto result = stdexec::sync_wait(std::move(sender));
    
    ASSERT_TRUE(result.has_value());
    auto [res1] = result.value();

    EXPECT_EQ(res1, 11);          // Captured result is correct
    EXPECT_EQ(broadcast_count, 1); // Non-captured slot still triggered once via broadcast
}

// 7. Test Capture Pipe Syntax
TEST_F(SenderTest, CapturePipeSyntax) {
    auto con1 = daking::connect<TestSignal>(emitter, stdexec::then([](int i, std::string s) { return i * 2; }));
    auto con2 = daking::connect<TestSignal>(emitter, stdexec::then([](int i, std::string s) { return s.length(); }));

    // Syntax: Signal >> emit(capture, emitter, connections...)
    auto sender = TestSignal{20, "hello"} >> daking::emit(daking::capture, emitter, con1, con2);

    auto result = stdexec::sync_wait(std::move(sender));
    auto [res1, res2] = result.value();

    EXPECT_EQ(res1, 40);
    EXPECT_EQ(res2, 5);
}

// 8. Error Path: Connection does not belong to the specified Emitter
TEST_F(SenderTest, CaptureWrongEmitterError) {
    TestEmitter other_emitter;
    
    // con1 belongs to 'emitter', but we attempt to capture it using 'other_emitter'
    auto con1 = daking::connect<TestSignal>(emitter, stdexec::then([](int i, std::string s) { return i; }));

    auto sender = daking::emit(TestSignal{1, "err"}, daking::capture, other_emitter, con1);
    
    try {
        stdexec::sync_wait(std::move(sender));
    } catch (const std::runtime_error& e) {
        EXPECT_STREQ(e.what(), "Can't create sender: the connection is not connected to the emmiter or there are the same connections.");
    }
}

// 9. Error Path: Connection manually disabled during Capture
TEST_F(SenderTest, CaptureDisabledConnectionError) {
    auto con = daking::connect<TestSignal>(emitter, stdexec::then([](int i, std::string s) { return i; }));
    
    con.disable(); 

    auto sender = daking::emit(TestSignal{1, "disabled"}, daking::capture, emitter, con);

    try {
        stdexec::sync_wait(std::move(sender));
        FAIL() << "Should have thrown error for disabled connection";
    } catch (const std::runtime_error& e) {
        EXPECT_STREQ(e.what(), "Can't create sender: the connection has been disabled.");
    }
}

// 10. Error Path: Connection released/disconnected before Emission
TEST_F(SenderTest, CaptureClosedConnectionError) {
    auto lambda = [](int i, std::string s) { return i; };
    using ConType = decltype(daking::connect<TestSignal>(emitter, stdexec::then(lambda)));
    std::optional<ConType> con_holder;

    {
        auto temp_con = daking::connect<TestSignal>(emitter, stdexec::then(lambda));
        con_holder.emplace(std::move(temp_con));
        daking::disconnect<TestSignal>(emitter, con_holder.value()); // Explicitly disconnect, destroying the underlying slot
    }

    auto sender = daking::emit(TestSignal{1, "closed"}, daking::capture, emitter, con_holder.value());

    EXPECT_THROW({
        stdexec::sync_wait(std::move(sender));
    }, std::runtime_error);
}

// 11. High-load Hash Collision and Multi-connection Check
// Tests if the internal hash table in the Check function handles multiple connections correctly.
TEST_F(SenderTest, CaptureMultipleConnectionsCheck) {
    // Create multiple connections to verify internal lookup logic
    auto c1 = daking::connect<TestSignal>(emitter, stdexec::then([](int i, std::string){ return i; }));
    auto c2 = daking::connect<TestSignal>(emitter, stdexec::then([](int i, std::string){ return i; }));
    auto c3 = daking::connect<TestSignal>(emitter, stdexec::then([](int i, std::string){ return i; }));
    auto c4 = daking::connect<TestSignal>(emitter, stdexec::then([](int i, std::string){ return i; }));
    auto c5 = daking::connect<TestSignal>(emitter, stdexec::then([](int i, std::string){ return i; }));

    auto sender = daking::emit(TestSignal{100, "multi"}, daking::capture, emitter, c1, c2, c3, c4, c5);
    
    auto result = stdexec::sync_wait(std::move(sender));
    ASSERT_TRUE(result.has_value());
    
    auto results = result.value();
    EXPECT_EQ(std::get<0>(results), 100);
    EXPECT_EQ(std::get<4>(results), 100);
}