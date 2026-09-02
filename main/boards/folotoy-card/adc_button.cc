#include "adc_button.h"

#include <esp_log.h>

#define TAG "AdcButton"

static constexpr int kStableTimes = 3;        // 连续 N 次同档位才确认切换(去抖)
static constexpr TickType_t kPollPeriod = pdMS_TO_TICKS(10);
static constexpr int kLongPressMs = 1500;

AdcButton::AdcButton(adc_unit_t unit, adc_channel_t channel, const Thresholds& t)
    : channel_(channel), th_(t) {
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = unit,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_cfg, &adc_handle_));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle_, channel_, &chan_cfg));

    auto task_fn = [](void* arg) {
        static_cast<AdcButton*>(arg)->PollTask();
    };
    xTaskCreate(task_fn, "adc_button", 3072, this, 3, nullptr);
}

AdcButton::~AdcButton() {
    if (adc_handle_) {
        adc_oneshot_del_unit(adc_handle_);
    }
}

void AdcButton::OnClick(Level level, std::function<void()> callback) {
    on_click_[(int)level] = callback;
}

void AdcButton::OnLongPress(Level level, std::function<void()> callback) {
    on_long_press_[(int)level] = callback;
}

void AdcButton::Fire(Level level, bool long_press) {
    const Callback& cb = long_press ? on_long_press_[(int)level] : on_click_[(int)level];
    if (cb) {
        cb();
    }
}

AdcButton::Level AdcButton::SampleLevel() {
    int raw = 0;
    if (adc_oneshot_read(adc_handle_, channel_, &raw) != ESP_OK) {
        return Level::kReleased;
    }
    if (raw <= th_.up_max) {
        return Level::kUp;
    }
    if (raw >= th_.down_min && raw <= th_.down_max) {
        return Level::kDown;
    }
    if (raw >= th_.ok_min && raw <= th_.ok_max) {
        return Level::kOk;
    }
    return Level::kReleased;
}

void AdcButton::PollTask() {
    Level stable = Level::kReleased;
    Level pending = Level::kReleased;
    Level pressed_level = Level::kReleased;
    int same_count = 0;
    TickType_t press_start = 0;
    bool pressed = false;
    bool consumed_long = false;

    while (true) {
        Level now = SampleLevel();

        if ((int)now == (int)pending) {
            if (++same_count >= kStableTimes && now != stable) {
                stable = now;
                if (stable == Level::kReleased) {
                    if (pressed) {
                        pressed = false;
                        if (!consumed_long) {
                            Fire(pressed_level, false); // click 用按住的档位
                        }
                        consumed_long = false;
                    }
                } else {
                    pressed = true;
                    pressed_level = stable;
                    consumed_long = false;
                    press_start = xTaskGetTickCount();
                }
            }
        } else {
            pending = now;
            same_count = 1;
        }

        if (pressed && !consumed_long &&
            xTaskGetTickCount() - press_start >= pdMS_TO_TICKS(kLongPressMs)) {
            consumed_long = true;
            Fire(pressed_level, true);
        }
        vTaskDelay(kPollPeriod);
    }
}
