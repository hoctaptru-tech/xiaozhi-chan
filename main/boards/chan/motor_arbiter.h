#ifndef MOTOR_ARBITER_H
#define MOTOR_ARBITER_H

#include "motor_controller.h"

#include <algorithm>
#include <atomic>
#include <functional>
#include <mutex>
#include <utility>

#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// The ONLY object allowed to talk to the MotorController. It exists so that the voice
// command (MCP "self.robot.move") and the small "body language" gestures can never fight
// over the motors, which is what causes current spikes and resets:
//
//   1. A voice command ALWAYS wins. It takes over at once, and a running gesture notices
//      and gives up without touching the motors again.
//   2. After a voice command, gestures stay away until it has finished plus a quiet time.
//   3. Gestures never run while charging.
//   4. Every command: speed capped, soft start (ramp), and a short dead time before the
//      wheels reverse direction, so the battery never sees a sudden full-power jump.
//   5. "Continuous" can never run forever: it is limited to kMaxContinuousMs.
//
// All state is protected by one mutex, so there is no check-then-act race.
class MotorArbiter {
public:
    // ---------------- Tuning ----------------
    static constexpr int kMaxSpeed = 100;                  // hard cap for voice commands (0..100)
    static constexpr int kMaxGestureSpeed = 90;            // hard cap for gestures (short, in place)
    static constexpr int kMaxContinuousMs = 30000;         // safety limit for "continuous"
    static constexpr int kRampMs = 80;                     // soft start time
    static constexpr int kReverseDeadMs = 80;              // pause before changing direction
    static constexpr int kGestureRampMs = 40;              // gestures start faster so they look lively
    static constexpr int64_t kVoiceQuietUs = 6LL * 1000 * 1000;  // gestures wait this long after a voice move
    // ----------------------------------------

    enum class Action { kForward, kBackward, kTurnLeft, kTurnRight };

    MotorArbiter(MotorController* motor, std::function<bool()> is_charging)
        : motor_(motor), is_charging_(std::move(is_charging)) {}

    // Voice / MCP command. duration_ms = 0 means "continuous" (still capped).
    void VoiceDrive(Action action, int speed, int duration_ms) {
        speed = std::clamp(speed, 0, kMaxSpeed);
        int left = 0, right = 0;
        switch (action) {
            case Action::kForward:   left = speed;  right = speed;  break;
            case Action::kBackward:  left = -speed; right = -speed; break;
            case Action::kTurnLeft:  left = -speed; right = speed;  break;
            case Action::kTurnRight: left = speed;  right = -speed; break;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        epoch_.fetch_add(1);  // preempts any gesture
        DriveLocked(left, right, duration_ms);
        const int run_ms = duration_ms > 0 ? duration_ms : kMaxContinuousMs;
        voice_until_us_ = esp_timer_get_time() + static_cast<int64_t>(run_ms) * 1000 + kVoiceQuietUs;
    }

    void VoiceStop() {
        std::lock_guard<std::mutex> lock(mutex_);
        epoch_.fetch_add(1);
        StopLocked();
        voice_until_us_ = esp_timer_get_time() + kVoiceQuietUs;
    }

    // One gesture step: turn in place (dir < 0 = left, dir > 0 = right) for ms.
    // Blocks until the step is over. Returns false if it was refused, preempted by a
    // voice command, or aborted through should_abort (the motors are left alone when a
    // voice command took over).
    bool GestureTurn(int dir, int speed, int ms, const std::function<bool()>& should_abort) {
        uint32_t my_epoch;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (esp_timer_get_time() < voice_until_us_ || (is_charging_ && is_charging_())) {
                return false;
            }
            my_epoch = epoch_.load();
            speed = std::clamp(speed, 0, kMaxGestureSpeed);
            DriveLocked(dir < 0 ? -speed : speed, dir < 0 ? speed : -speed, ms, kGestureRampMs);
        }

        const int64_t end_us = esp_timer_get_time() + static_cast<int64_t>(ms) * 1000;
        while (esp_timer_get_time() < end_us) {
            if (epoch_.load() != my_epoch) {
                return false;  // a voice command took over: do not touch the motors
            }
            if (should_abort && should_abort()) {
                std::lock_guard<std::mutex> lock(mutex_);
                if (epoch_.load() == my_epoch) {
                    StopLocked();
                }
                return false;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        std::lock_guard<std::mutex> lock(mutex_);
        if (epoch_.load() != my_epoch) {
            return false;
        }
        StopLocked();
        return true;
    }

private:
    MotorController* motor_;
    std::function<bool()> is_charging_;
    std::mutex mutex_;
    std::atomic<uint32_t> epoch_{0};  // bumped by every voice command
    int64_t voice_until_us_ = 0;
    int last_left_ = 0;
    int last_right_ = 0;
    int64_t last_drive_end_us_ = 0;

    void StopLocked() {
        motor_->Stop();
        last_left_ = 0;
        last_right_ = 0;
    }

    // Caller holds mutex_.
    void DriveLocked(int left, int right, int duration_ms, int ramp_ms = kRampMs) {
        const int64_t now = esp_timer_get_time();
        const bool reversing = (left * last_left_ < 0 || right * last_right_ < 0) &&
                               now < last_drive_end_us_ + 300000;
        if (reversing) {
            StopLocked();
            vTaskDelay(pdMS_TO_TICKS(kReverseDeadMs));
        }

        const int run_ms = duration_ms > 0 ? duration_ms : kMaxContinuousMs;
        // The watchdog is armed first and also covers the soft start, so a stuck caller
        // can never leave the motors running.
        motor_->Drive(left * 40 / 100, right * 40 / 100, run_ms + ramp_ms);
        vTaskDelay(pdMS_TO_TICKS(ramp_ms / 2));
        motor_->SetLeft(left * 70 / 100);
        motor_->SetRight(right * 70 / 100);
        vTaskDelay(pdMS_TO_TICKS(ramp_ms / 2));
        motor_->SetLeft(left);
        motor_->SetRight(right);

        last_left_ = left;
        last_right_ = right;
        last_drive_end_us_ = esp_timer_get_time() + static_cast<int64_t>(run_ms) * 1000;
    }
};

#endif  // MOTOR_ARBITER_H
