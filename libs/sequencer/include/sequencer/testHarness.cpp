#ifndef SEQUENCER_TEST_HARNESS_HPP
#define SEQUENCER_TEST_HARNESS_HPP

#include "clock.hpp"
#include "sequencer.hpp"
#include <apps/eventStreamApp.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace sequencer {

/**
 * Test harness for orchestrating sequencer and apps with a mock clock.
 * 
 * Provides deterministic control over time for testing:
 * - stepOnce() - each component does one poll cycle
 * - stepFor(millis) - advance clock and run until time reached
 * - stepTilCompletion() - run until all queues drained
 * 
 * Usage:
 *   TestHarness harness;
 *   harness.setSequencer(&seq);
 *   harness.addApp(&app1);
 *   harness.addApp(&app2);
 *   
 *   // Initialize with harness's mock clock
 *   harness.initialize();
 *   
 *   // Run a single cycle
 *   harness.stepOnce();
 *   
 *   // Advance time and run
 *   harness.stepFor(100);  // 100ms
 *   
 *   // Run until no more events
 *   harness.stepTilCompletion();
 */
class TestHarness {
public:
    /**
     * Create a test harness with a mock clock starting at given time.
     * @param initial_time_ns Initial time in nanoseconds (default: 0)
     */
    explicit TestHarness(std::uint64_t initial_time_ns = 0)
        : clock_(std::make_shared<MockClock>(initial_time_ns)) {}
    
    /**
     * Get the mock clock (for direct manipulation).
     */
    std::shared_ptr<MockClock> clock() { return clock_; }
    
    /**
     * Get current time from mock clock.
     */
    std::uint64_t now() const { return clock_->now(); }
    
    /**
     * Set the sequencer to orchestrate.
     * Must be called before initialize().
     */
    void setSequencer(Sequencer* seq) {
        sequencer_ = seq;
    }
    
    /**
     * Add an app to orchestrate.
     * Must be called before initialize().
     */
    void addApp(apps::EventStreamApp* app) {
        apps_.push_back(app);
    }
    
    /**
     * Initialize all components with the shared mock clock.
     * Call this after setting sequencer and adding apps.
     */
    void initialize() {
        // Inject mock clock into sequencer
        if (sequencer_) {
            sequencer_->setClock(clock_);
            sequencer_->initialize();
        }
        
        // Inject mock clock into apps and initialize
        for (auto* app : apps_) {
            app->setClock(clock_);
            app->initialize();
        }
    }
    
    /**
     * Perform one poll cycle on all components.
     * Does not advance time.
     * 
     * Order: Sequencer polls commands, then each app polls events.
     * 
     * @return Total number of items processed (commands + events)
     */
    std::size_t stepOnce() {
        std::size_t processed = 0;
        
        // Sequencer polls command queues
        if (sequencer_) {
            processed += sequencer_->pollCommands();
        }
        
        // Each app polls event stream
        for (auto* app : apps_) {
            processed += app->pollEvents();
            app->pollPollables(clock_->now());
        }
        
        return processed;
    }
    
    /**
     * Advance time by given milliseconds, running cycles until time reached.
     * 
     * @param millis Milliseconds to advance
     * @param max_cycles Maximum cycles to prevent infinite loops (default: 10000)
     * @return Total number of items processed
     */
    std::size_t stepFor(std::uint64_t millis, std::size_t max_cycles = 10000) {
        std::uint64_t target_time = clock_->now() + millis * 1'000'000;
        std::size_t total_processed = 0;
        std::size_t cycles = 0;
        
        while (clock_->now() < target_time && cycles < max_cycles) {
            total_processed += stepOnce();
            clock_->advanceMillis(1);  // Advance 1ms per cycle
            cycles++;
        }
        
        // Set exact target time
        clock_->set(target_time);
        
        return total_processed;
    }
    
    /**
     * Run until all queues are drained (no more events/commands to process).
     * 
     * @param max_cycles Maximum cycles to prevent infinite loops (default: 10000)
     * @return Total number of items processed
     */
    std::size_t stepTilCompletion(std::size_t max_cycles = 10000) {
        std::size_t total_processed = 0;
        std::size_t cycles = 0;
        
        while (cycles < max_cycles) {
            std::size_t processed = stepOnce();
            total_processed += processed;
            
            if (processed == 0) {
                // Nothing processed, we're done
                break;
            }
            
            // Advance time slightly between cycles
            clock_->advanceMicros(100);
            cycles++;
        }
        
        return total_processed;
    }
    
    /**
     * Run for a specific number of cycles.
     * 
     * @param cycles Number of cycles to run
     * @param advance_micros Microseconds to advance per cycle (default: 100)
     * @return Total number of items processed
     */
    std::size_t stepCycles(std::size_t cycles, std::uint64_t advance_micros = 100) {
        std::size_t total_processed = 0;
        
        for (std::size_t i = 0; i < cycles; i++) {
            total_processed += stepOnce();
            clock_->advanceMicros(advance_micros);
        }
        
        return total_processed;
    }
    
    /**
     * Set mock clock to specific time.
     */
    void setTime(std::uint64_t nanos) {
        clock_->set(nanos);
    }
    
    /**
     * Advance mock clock by given nanoseconds.
     */
    void advanceTime(std::uint64_t nanos) {
        clock_->advance(nanos);
    }
    
    /**
     * Advance mock clock by given milliseconds.
     */
    void advanceTimeMillis(std::uint64_t millis) {
        clock_->advanceMillis(millis);
    }

private:
    std::shared_ptr<MockClock> clock_;
    Sequencer* sequencer_{nullptr};
    std::vector<apps::EventStreamApp*> apps_;
};

} // namespace sequencer

#endif // SEQUENCER_TEST_HARNESS_HPP