#pragma once

#include <cstddef>
#include <cstdint>

// Servo pusher gate; each station owns its own instance.
// ToF decisions (push / pass) go into a FIFO as soon as each item is measured. Every item the
// LM393 confirms at the pusher (debounced) consumes the oldest decision: push -> servo PUSH for
// push_ms, HOME, recover_ms for the retract; pass -> nothing. A consumed item must leave the LM393
// (empty for clear_ms) before the next one can be matched, so a stuck/slow item is never pushed twice.
// Several items can therefore be between ToF and pusher at once. Fault/disable -> HOME, queue cleared.
// ponytail: matching is by order only; an item the ToF misses (or sees twice) shifts the queue
// until entries expire (wait_limit_ms). Upgrade: match by travel time ToF->LM393.
enum class TrialStage { Idle, Extended, Recovery };
enum class TrialEvent { None, Push, Passed, Returned, Ready, TimedOut, Unmatched, SensorLost };

struct Station1TrialGate {
    static constexpr std::size_t QUEUE_MAX = 8;

    // Calibratable timings (ms). Defaults are starting values; tune on the bench.
    std::uint32_t position_stable_ms = 10;  // LM393 must stay active this long (noise filter)
    std::uint32_t wait_limit_ms = 1500;     // a decision not matched by LM393 within this is dropped
    std::uint32_t push_ms = 160;            // servo held at PUSH (SG90 ~150 ms for 90 deg at 5 V)
    std::uint32_t recover_ms = 150;         // time for the servo to retract to HOME
    std::uint32_t clear_ms = 30;            // LM393 empty continuously before the next item counts

    TrialStage stage = TrialStage::Idle;
    std::uint32_t stage_since = 0;
    std::uint32_t position_since = 0;
    bool position_timing = false;
    bool need_release = false;  // consumed item still in front of the LM393
    std::uint32_t release_since = 0;
    bool release_timing = false;

    bool q_push[QUEUE_MAX]{};
    std::uint32_t q_time[QUEUE_MAX]{};
    std::size_t q_head = 0;
    std::size_t q_count = 0;

    constexpr void reset() {
        stage = TrialStage::Idle;
        position_timing = false;
        need_release = false;
        release_timing = false;
        q_count = 0;
    }

    // Called once per ToF decision. false = queue full (item will not be handled).
    constexpr bool enqueue(bool push, std::uint32_t now) {
        if (q_count == QUEUE_MAX) return false;
        const std::size_t i = (q_head + q_count) % QUEUE_MAX;
        q_push[i] = push;
        q_time[i] = now;
        ++q_count;
        return true;
    }

    // Bench/manual push: only from Idle, bypasses ToF+LM393. Caller must gate on a safe state.
    constexpr bool force_push(std::uint32_t now) {
        if (stage != TrialStage::Idle) return false;
        stage = TrialStage::Extended;
        stage_since = now;
        return true;
    }

    constexpr TrialEvent tick(std::uint32_t now, bool enabled, bool tof_ok, bool position_active) {
        if (!enabled || !tof_ok) {
            const bool was_active = stage == TrialStage::Extended || q_count > 0;
            reset();
            return was_active && !tof_ok ? TrialEvent::SensorLost : TrialEvent::None;
        }

        if (need_release) {
            if (position_active) {
                release_timing = false;
            } else {
                if (!release_timing) {
                    release_timing = true;
                    release_since = now;
                }
                if (now - release_since >= clear_ms) need_release = false;
            }
        }

        switch (stage) {
        case TrialStage::Extended:
            if (now - stage_since < push_ms) return TrialEvent::None;
            stage = TrialStage::Recovery;
            stage_since = now;
            return TrialEvent::Returned;
        case TrialStage::Recovery:
            if (now - stage_since < recover_ms) return TrialEvent::None;
            stage = TrialStage::Idle;
            return TrialEvent::Ready;
        case TrialStage::Idle:
            break;
        }

        if (q_count > 0 && now - q_time[q_head] >= wait_limit_ms) {
            const bool dropped_push = q_push[q_head];
            pop();
            if (dropped_push) return TrialEvent::TimedOut;
        }

        if (need_release) return TrialEvent::None;
        if (!position_active) {
            const bool was_item = position_timing && now - position_since >= position_stable_ms;
            position_timing = false;
            return was_item ? TrialEvent::Unmatched : TrialEvent::None;  // LM393 item ToF never decided
        }
        if (!position_timing) {
            position_timing = true;
            position_since = now;
        }
        // Item may reach the LM393 before ToF has decided: keep waiting while it is still there.
        if (now - position_since < position_stable_ms || q_count == 0) return TrialEvent::None;

        const bool push = q_push[q_head];
        pop();
        position_timing = false;
        need_release = true;
        release_timing = false;
        if (!push) return TrialEvent::Passed;
        stage = TrialStage::Extended;
        stage_since = now;
        return TrialEvent::Push;
    }

private:
    constexpr void pop() {
        q_head = (q_head + 1) % QUEUE_MAX;
        --q_count;
    }
};
