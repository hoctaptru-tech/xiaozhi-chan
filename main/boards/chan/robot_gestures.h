#ifndef ROBOT_GESTURES_H
#define ROBOT_GESTURES_H

#include "device_state.h"
#include "motor_arbiter.h"

#include <atomic>
#include <initializer_list>

#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Small "body language" so the robot does not look stiff. In-place wiggles only
// (turn left/right by the same amount, so it ends where it started and never drives
// off a table). All motor access goes through MotorArbiter, so a voice move command
// always wins and nothing can fight over the motors.
//
//   - woken up: the beep plays first, then one quick wake-up shake (see OnWake)
//   - question heard / answer starts (Speaking): a small "got it" nod, then every few
//     seconds a random wiggle, or standing still (random)
//   - Listening: stays still on purpose, motor noise would disturb the microphone
class RobotGestures {
public:
    // ---------------- Tuning ----------------
    // Automatic movements while the robot is answering (the "got it" nod and the random
    // wiggles). Off by default: only the wake-up shake remains. Set to true to bring them back.
    static constexpr bool kSpeakGestures = false;
    static constexpr int kWakeDelayMs = 600;         // the beep (~0.5 s) plays first, then the shake
    static constexpr int kWakeSpeed = 90;            // 0..100 (also capped by MotorArbiter::kMaxGestureSpeed)
    static constexpr int kSpeakSpeed = 60;
    static constexpr int kAckChancePercent = 70;     // "got it" nod when the answer starts
    static constexpr int kSpeakChancePercent = 60;   // chance to move at each turn; else stand still
    static constexpr int kSpeakMinIntervalMs = 2500;
    static constexpr int kSpeakMaxIntervalMs = 6500;
    // Left/right motors are never identical (friction, wiring, battery sag), so one
    // direction often turns less than the other for the same time. Scale the duration of
    // every left/right step here: if the robot turns too little to the right, raise
    // kRightTurnPercent (e.g. 130); if too much to the left, lower kLeftTurnPercent.
    static constexpr int kLeftTurnPercent = 100;
    static constexpr int kRightTurnPercent = 100;
    // ----------------------------------------

    explicit RobotGestures(MotorArbiter* arbiter) : arbiter_(arbiter) {
        xTaskCreate(&RobotGestures::TaskEntry, "gestures", 3072, this, 3, &task_);
    }

    // Called on every device state change. Never blocks.
    void OnStateChanged(DeviceState state) {
        // The wake-up shake must not be cut short when the robot goes straight on to
        // listening (that was the "twitch and stop"). Any other state still cancels it.
        if (state == kDeviceStateListening && wake_in_progress_.load()) {
            return;
        }
        wake_in_progress_.store(false);
        state_.store(state);
        generation_.fetch_add(1);
        if (task_ != nullptr) {
            xTaskNotifyGive(task_);
        }
    }

    // Called when a conversation starts (same moment as the wake beep). The shake starts
    // kWakeDelayMs later, so the beep always comes first.
    void OnWake() {
        generation_.fetch_add(1);  // before the flags, so the task reads the new value
        wake_in_progress_.store(true);
        wake_pending_.store(true);
        if (task_ != nullptr) {
            xTaskNotifyGive(task_);
        }
    }

    // Turn all gestures on/off at runtime. Stops anything in progress.
    void SetEnabled(bool enabled) {
        enabled_.store(enabled);
        generation_.fetch_add(1);
        if (task_ != nullptr) {
            xTaskNotifyGive(task_);
        }
    }

private:
    struct Step {
        int dir;    // -1 = turn left, +1 = turn right
        int speed;  // 0..100
        int ms;
    };

    MotorArbiter* arbiter_;
    TaskHandle_t task_ = nullptr;
    std::atomic<DeviceState> state_{kDeviceStateUnknown};
    std::atomic<uint32_t> generation_{0};  // bumped on every change; running gestures abort
    std::atomic<bool> enabled_{true};
    std::atomic<bool> wake_in_progress_{false};
    std::atomic<bool> wake_pending_{false};

    static void TaskEntry(void* arg) { static_cast<RobotGestures*>(arg)->Run(); }

    bool Cancelled(uint32_t gen) const { return generation_.load() != gen; }

    void Run() {
        for (;;) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            const uint32_t gen = generation_.load();
            if (!enabled_.load()) {
                continue;
            }
            if (wake_pending_.exchange(false)) {
                const uint32_t wake_gen = generation_.load();
                // Beep first, then shake
                if (Wait(wake_gen, kWakeDelayMs)) {
                    PlaySteps(wake_gen, {{-1, kWakeSpeed, 180}, {+1, kWakeSpeed, 480}, {-1, kWakeSpeed, 300}});
                }
                wake_in_progress_.store(false);
                continue;
            }
            switch (state_.load()) {
                case kDeviceStateSpeaking:
                    if (kSpeakGestures) {
                        SpeakLoop(gen);
                    }
                    break;
                default:
                    break;
            }
        }
    }

    // Plays the steps one by one; returns false if refused or aborted.
    bool PlaySteps(uint32_t gen, std::initializer_list<Step> steps) {
        auto should_abort = [this, gen]() { return Cancelled(gen); };
        for (const auto& s : steps) {
            const int ms = s.ms * (s.dir < 0 ? kLeftTurnPercent : kRightTurnPercent) / 100;
            if (Cancelled(gen) || !arbiter_->GestureTurn(s.dir, s.speed, ms, should_abort)) {
                return false;
            }
            vTaskDelay(pdMS_TO_TICKS(40));  // let the motors settle before reversing
        }
        return true;
    }

    // Waits wait_ms in small slices. Returns false if cancelled meanwhile.
    bool Wait(uint32_t gen, uint32_t wait_ms) {
        for (uint32_t t = 0; t < wait_ms; t += 100) {
            if (Cancelled(gen)) {
                return false;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        return !Cancelled(gen);
    }

    void SpeakLoop(uint32_t gen) {
        // "Got it" nod just as the answer starts (after a beat, so the audio is already playing)
        if (!Wait(gen, 300)) {
            return;
        }
        if (static_cast<int>(esp_random() % 100) < kAckChancePercent) {
            PlaySteps(gen, {{+1, kSpeakSpeed, 140}, {-1, kSpeakSpeed, 140}});
        }

        while (!Cancelled(gen)) {
            const uint32_t wait_ms =
                kSpeakMinIntervalMs + esp_random() % (kSpeakMaxIntervalMs - kSpeakMinIntervalMs + 1);
            if (!Wait(gen, wait_ms)) {
                return;
            }
            if (static_cast<int>(esp_random() % 100) >= kSpeakChancePercent) {
                continue;  // stand still this time
            }
            switch (esp_random() % 3) {
                case 0:  // small shake
                    PlaySteps(gen, {{-1, kSpeakSpeed, 160}, {+1, kSpeakSpeed, 320}, {-1, kSpeakSpeed, 160}});
                    break;
                case 1:  // quick look to one side and back
                    PlaySteps(gen, {{+1, kSpeakSpeed, 140}, {-1, kSpeakSpeed, 140}});
                    break;
                default:  // slow sway
                    PlaySteps(gen, {{-1, kSpeakSpeed - 5, 260}, {+1, kSpeakSpeed - 5, 520}, {-1, kSpeakSpeed - 5, 260}});
                    break;
            }
        }
    }
};

#endif  // ROBOT_GESTURES_H
