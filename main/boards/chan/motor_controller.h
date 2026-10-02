#ifndef _ROBOT_MOTOR_CONTROLLER_H_
#define _ROBOT_MOTOR_CONTROLLER_H_

#include <driver/gpio.h>
#include <driver/ledc.h>
#include <esp_timer.h>
#include <algorithm>

// Dieu khien 2 dong co qua driver DRV8833.
// Moi ben (trai/phai) dung 2 chan IN1/IN2:
//   IN1=PWM, IN2=0   -> quay chieu thuan (toc do theo duty cua IN1)
//   IN1=0,   IN2=PWM -> quay chieu nguoc
//   IN1=0,   IN2=0   -> dung tu do (coast)
// Day la cach dieu khien pho bien cho DRV8833, khong can chan nSLEEP rieng
// (neu board co pin_motor_sleep thi phai keo no len HIGH truoc khi dung).
//
// Co san "auto-stop watchdog": moi lenh di chuyen (Forward/Backward/...) mac
// dinh se tu dong Stop() sau mot khoang thoi gian (duration_ms), tru khi goi
// voi duration_ms = 0 (nghia la chay lien tuc cho toi khi Stop() duoc goi tay).
// Dieu nay dam bao motor khong bao gio "lac"/chay mai neu phia tren (MCP tool,
// LLM) quen goi stop - vi du khi chuyen trang thai Speaking -> Listening.
class MotorController {
public:
    // invert_left/invert_right: dat true neu ben do quay nguoc chieu mong muon
    // (do day dau nguoc hoac IN1/IN2 bi dao) - khong can dau lai day vat ly.
    MotorController(gpio_num_t left_in1, gpio_num_t left_in2,
                     gpio_num_t right_in1, gpio_num_t right_in2,
                     bool invert_left = false, bool invert_right = false)
        : left_in1_(left_in1), left_in2_(left_in2),
          right_in1_(right_in1), right_in2_(right_in2),
          invert_left_(invert_left), invert_right_(invert_right) {
        InitChannel(left_in1_, LEDC_CHANNEL_0);
        InitChannel(left_in2_, LEDC_CHANNEL_1);
        InitChannel(right_in1_, LEDC_CHANNEL_2);
        InitChannel(right_in2_, LEDC_CHANNEL_3);

        esp_timer_create_args_t timer_args = {};
        timer_args.callback = &MotorController::OnAutoStopTimer;
        timer_args.arg = this;
        timer_args.name = "motor_auto_stop";
        esp_timer_create(&timer_args, &auto_stop_timer_);

        Stop();
    }

    ~MotorController() {
        if (auto_stop_timer_) {
            esp_timer_stop(auto_stop_timer_);
            esp_timer_delete(auto_stop_timer_);
        }
    }

    // speed: -100..100 (am = lui, duong = tien, 0 = dung)
    void SetLeft(int speed) {
        SetSide(LEDC_CHANNEL_0, LEDC_CHANNEL_1, invert_left_ ? -speed : speed);
    }
    void SetRight(int speed) {
        SetSide(LEDC_CHANNEL_2, LEDC_CHANNEL_3, invert_right_ ? -speed : speed);
    }

    // duration_ms = 0 -> chay lien tuc, khong tu dong dung (huy watchdog neu dang co)
    // duration_ms > 0 -> chay xong tu dong Stop() sau duration_ms
    void Forward(int speed = 100, int duration_ms = 800) {
        SetLeft(speed);
        SetRight(speed);
        ArmAutoStop(duration_ms);
    }

    void Backward(int speed = 100, int duration_ms = 800) {
        SetLeft(-speed);
        SetRight(-speed);
        ArmAutoStop(duration_ms);
    }

    void TurnLeft(int speed = 100, int duration_ms = 600) {
        SetLeft(-speed);
        SetRight(speed);
        ArmAutoStop(duration_ms);
    }

    void TurnRight(int speed = 100, int duration_ms = 600) {
        SetLeft(speed);
        SetRight(-speed);
        ArmAutoStop(duration_ms);
    }

    // Drive both sides independently (-100..100 each) with the auto-stop watchdog.
    // duration_ms = 0 -> no watchdog (caller must Stop()).
    void Drive(int left, int right, int duration_ms) {
        ArmAutoStop(duration_ms);
        SetLeft(left);
        SetRight(right);
    }

    void Stop() {
        CancelAutoStop();
        SetLeft(0);
        SetRight(0);
    }

private:
    gpio_num_t left_in1_, left_in2_, right_in1_, right_in2_;
    bool invert_left_, invert_right_;
    esp_timer_handle_t auto_stop_timer_ = nullptr;
    static constexpr int kPwmFreqHz = 20000;       // 20kHz, ngoai nguong nghe duoc de tranh e e
    static constexpr int kPwmResolutionBits = LEDC_TIMER_10_BIT; // 0..1023

    void ArmAutoStop(int duration_ms) {
        CancelAutoStop();
        if (duration_ms > 0) {
            esp_timer_start_once(auto_stop_timer_, (uint64_t)duration_ms * 1000);
        }
        // duration_ms == 0: chay lien tuc, khong dat timer (da huy o CancelAutoStop)
    }

    void CancelAutoStop() {
        if (auto_stop_timer_ && esp_timer_is_active(auto_stop_timer_)) {
            esp_timer_stop(auto_stop_timer_);
        }
    }

    static void OnAutoStopTimer(void* arg) {
        auto* self = static_cast<MotorController*>(arg);
        self->SetLeft(0);
        self->SetRight(0);
        // Khong goi Stop() (vi Stop() lai goi CancelAutoStop tren chinh timer nay
        // dang chay callback - an toan hon la tu tat truc tiep 2 kenh o day).
    }

    void InitChannel(gpio_num_t pin, ledc_channel_t channel) {
        if (channel == LEDC_CHANNEL_0) {
            ledc_timer_config_t timer_cfg = {};
            timer_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
            timer_cfg.duty_resolution = (ledc_timer_bit_t)kPwmResolutionBits;
            timer_cfg.timer_num = LEDC_TIMER_0;
            timer_cfg.freq_hz = kPwmFreqHz;
            timer_cfg.clk_cfg = LEDC_AUTO_CLK;
            ledc_timer_config(&timer_cfg);
        }
        ledc_channel_config_t ch_cfg = {};
        ch_cfg.gpio_num = pin;
        ch_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
        ch_cfg.channel = channel;
        ch_cfg.timer_sel = LEDC_TIMER_0;
        ch_cfg.duty = 0;
        ch_cfg.hpoint = 0;
        ledc_channel_config(&ch_cfg);
    }

    void SetSide(ledc_channel_t ch_fwd, ledc_channel_t ch_bwd, int speed) {
        speed = std::clamp(speed, -100, 100);
        int duty_max = (1 << kPwmResolutionBits) - 1;
        int duty = std::abs(speed) * duty_max / 100;

        if (speed >= 0) {
            ledc_set_duty(LEDC_LOW_SPEED_MODE, ch_fwd, duty);
            ledc_update_duty(LEDC_LOW_SPEED_MODE, ch_fwd);
            ledc_set_duty(LEDC_LOW_SPEED_MODE, ch_bwd, 0);
            ledc_update_duty(LEDC_LOW_SPEED_MODE, ch_bwd);
        } else {
            ledc_set_duty(LEDC_LOW_SPEED_MODE, ch_fwd, 0);
            ledc_update_duty(LEDC_LOW_SPEED_MODE, ch_fwd);
            ledc_set_duty(LEDC_LOW_SPEED_MODE, ch_bwd, duty);
            ledc_update_duty(LEDC_LOW_SPEED_MODE, ch_bwd);
        }
    }
};

#endif // _ROBOT_MOTOR_CONTROLLER_H_
