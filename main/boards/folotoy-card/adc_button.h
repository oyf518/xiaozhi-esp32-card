#ifndef _ADC_BUTTON_H_
#define _ADC_BUTTON_H_

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_adc/adc_oneshot.h>
#include <functional>

// FoloToy-Card 的三键共用一个 ADC 引脚(GPIO0), 靠分压电阻区分档位。
// xiaozhi 的 Button 类只能读 GPIO 电平, 这里用 10ms 轮询任务实现同语义接口:
//   OnClick(level)      : 按下后松开(未构成长按)
//   OnLongPress(level)  : 按住达到 kLongPressMs 时立即触发一次(不等松开)
class AdcButton {
public:
    enum class Level { kReleased, kUp, kDown, kOk };

    struct Thresholds {
        int up_max;        // raw <= up_max                -> 上键
        int down_min;      // down_min..down_max           -> 下键
        int down_max;
        int ok_min;        // ok_min..ok_max               -> 确定键
        int ok_max;
        int released_min;  // >= released_min              -> 松开
    };

    AdcButton(adc_unit_t unit, adc_channel_t channel, const Thresholds& t);
    ~AdcButton();

    void OnClick(Level level, std::function<void()> callback);
    void OnLongPress(Level level, std::function<void()> callback);

private:
    void PollTask();
    Level SampleLevel();
    void Fire(Level level, bool long_press);

    using Callback = std::function<void()>;
    Callback on_click_[4];     // 以 enum Level 为索引
    Callback on_long_press_[4];

    adc_oneshot_unit_handle_t adc_handle_ = nullptr;
    adc_channel_t channel_;
    Thresholds th_;
};

#endif // _ADC_BUTTON_H_
