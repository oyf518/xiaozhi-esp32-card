#include "wifi_board.h"
#include "codecs/es8311_audio_codec.h"
#include "display/lcd_display.h"
#include "application.h"
#include "adc_button.h"
#include "config.h"
#include "i2c_device.h"
#include "assets/lang_config.h"

#include <esp_log.h>
#include <esp_lcd_panel_vendor.h>
#include <driver/i2c_master.h>
#include <driver/spi_common.h>
#include <esp_wifi.h>

#include "power_save_timer.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "FolotoyCard"

// CW2017 电量计 (读法与 backup/src_esp32c3_orig 的 bsp_battery.c 一致, 已实机验证)
#define CW2017_ADDR       0x63
#define CW_REG_VERSION    0x00
#define CW_REG_VCELL_H    0x02
#define CW_REG_SOC_H      0x04
#define CW_REG_CONFIG     0x08

class FolotoyCard : public WifiBoard {
private:
    i2c_master_bus_handle_t codec_i2c_bus_;
    i2c_master_dev_handle_t cw2017_dev_ = nullptr;
    AdcButton* adc_button_;
    LcdDisplay* display_;
    PowerSaveTimer* power_save_timer_;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;

    void InitializeI2c() {
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &codec_i2c_bus_));

        // 全总线扫描自检(仅诊断日志): 期望看到 ES8311@0x18 与可选的 CW2017@0x63。
        // 注: AUDIO_CODEC_ES8311_ADDR(0x30) 是 esp_codec_dev 的 8 位格式,
        //     probe() 需要 7 位地址, 别拿它来探测(会误报)。
        int found = 0;
        for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
            if (i2c_master_probe(codec_i2c_bus_, addr, 50) == ESP_OK) {
                const char* who = (addr == 0x18) ? " <- ES8311"
                                : (addr == 0x19) ? " <- ES8311(AD0=高)"
                                : (addr == 0x63) ? " <- CW2017"
                                : "";
                ESP_LOGW(TAG, "I2C 设备 @ 0x%02X%s", addr, who);
                found++;
            }
        }
        if (found == 0) {
            ESP_LOGE(TAG, "总线上无任何设备! (SDA=GPIO%d SCL=GPIO%d)",
                     AUDIO_CODEC_I2C_SDA_PIN, AUDIO_CODEC_I2C_SCL_PIN);
        }

        // 可选电量计: 不在位则静默跳过(不影响主功能)
        i2c_device_config_t cw_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = CW2017_ADDR,
            .scl_speed_hz = 100000,
        };
        if (i2c_master_bus_add_device(codec_i2c_bus_, &cw_cfg, &cw2017_dev_) == ESP_OK) {
            uint8_t ver = 0;
            uint8_t reg = CW_REG_VERSION;
            if (i2c_master_transmit_receive(cw2017_dev_, &reg, 1, &ver, 1, 100) == ESP_OK) {
                ESP_LOGI(TAG, "检测到 CW2017 VERSION=0x%02X", ver);
                // 上电自检(纯读): 打印 CONFIG/VCELL/SOC 基线供硬件排查。
                // ⚠ 不写任何寄存器 —— 对 CW2017 写入(含 Quick Start)曾导致芯片临时
                //   停止应答 I2C, 进而拖垮整个电量显示; SOC 引擎无效时由
                //   GetBatteryLevel 用 VCELL 查表兜底。
                uint8_t cfg = 0xFF;
                ReadCw(CW_REG_CONFIG, &cfg, 1);
                uint8_t vcell[2] = {0}, soc[2] = {0};
                if (ReadCw(CW_REG_VCELL_H, vcell, 2) && ReadCw(CW_REG_SOC_H, soc, 2)) {
                    uint32_t mv = (((uint32_t)vcell[0] << 8) | vcell[1]) * 3125UL / 10000UL; // raw*312.5uV
                    ESP_LOGW(TAG, "CW2017 自检: CONFIG=0x%02X VCELL=%lu mV SOC=%u.%02u%%%s",
                             cfg, mv, soc[0], soc[1] * 100U / 256U,
                             soc[0] > 100 ? " (SOC 引擎未配置 profile, 将用电压估算)" : "");
                }
            } else {
                i2c_master_bus_rm_device(cw2017_dev_);
                cw2017_dev_ = nullptr;
            }
        }
    }

    bool ReadCw(uint8_t reg, uint8_t* buf, size_t n) {
        if (!cw2017_dev_) return false;
        return i2c_master_transmit_receive(cw2017_dev_, &reg, 1, buf, n, 100) == ESP_OK;
    }

    // 电压兜底: Li-Po 近似曲线 -> 百分比(SOC 引擎未配置 profile, 恒为复位值时使用)
    int VoltToPercent(uint32_t mv) {
        static const uint16_t tbl[][2] = {
            {4200, 100}, {4080, 90}, {4000, 80}, {3920, 70}, {3840, 60},
            {3760, 50},  {3700, 40}, {3650, 30}, {3600, 22}, {3550, 18},
            {3500, 14},  {3450, 11}, {3400, 8},  {3350, 6},  {3300, 4},
            {3200, 2},   {3000, 1},
        };
        constexpr int N = sizeof(tbl) / sizeof(tbl[0]);
        if (mv >= tbl[0][0]) return 100;
        for (int i = 0; i < N; i++) {
            if (mv >= tbl[i][0]) {
                if (i == 0) return tbl[0][1];
                uint32_t v_hi = tbl[i-1][0], p_hi = tbl[i-1][1];
                uint32_t v_lo = tbl[i][0],  p_lo = tbl[i][1];
                return p_lo + (p_hi - p_lo) * (mv - v_lo) / (v_hi - v_lo);
            }
        }
        return 1;
    }

    void InitializePowerSaveTimer() {
        power_save_timer_ = new PowerSaveTimer(-1, 60, -1);
        power_save_timer_->OnEnterSleepMode([this]() {
            ESP_LOGI(TAG, "Enabling modem-sleep mode");
            GetDisplay()->SetPowerSaveMode(true);
            GetBacklight()->SetBrightness(1);
            esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
        });
        power_save_timer_->OnExitSleepMode([this]() {
            GetDisplay()->SetPowerSaveMode(false);
            GetBacklight()->RestoreBrightness();
            esp_wifi_set_ps(WIFI_PS_NONE);
        });
        power_save_timer_->OnShutdownRequest([this]() {
            GetBacklight()->SetBrightness(1);
        });
        power_save_timer_->SetEnabled(true);
    }

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_SPI_MOSI_PIN;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = DISPLAY_SPI_SCLK_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeButtons() {
        adc_button_ = new AdcButton(
            BUTTON_ADC_UNIT, BUTTON_ADC_CHANNEL,
            {BUTTON_LEVEL_UP_RAW_MAX, BUTTON_LEVEL_DOWN_RAW_MIN, BUTTON_LEVEL_DOWN_RAW_MAX,
             BUTTON_LEVEL_OK_RAW_MIN, BUTTON_LEVEL_OK_RAW_MAX, BUTTON_RELEASED_RAW_MIN});

        // 确定键: 短按 = 开关对话; 长按 = 进入配网模式
        adc_button_->OnClick(AdcButton::Level::kOk, [this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });
        adc_button_->OnLongPress(AdcButton::Level::kOk, [this]() {
            EnterWifiConfigMode();
        });

        // 上/下键: 音量增减
        adc_button_->OnClick(AdcButton::Level::kUp, [this]() {
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() + 10;
            if (volume > 100) {
                volume = 100;
            }
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
        });
        adc_button_->OnClick(AdcButton::Level::kDown, [this]() {
            auto codec = GetAudioCodec();
            auto volume = std::max(0, codec->output_volume() - 10);
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
        });
    }

    void InitializeSt7789Display() {
        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_SPI_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = 0;
        io_config.pclk_hz = DISPLAY_PCLK_HZ;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST, &io_config, &panel_io_));

        ESP_LOGD(TAG, "Install LCD driver ST7789P3");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RST_PIN;
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        panel_config.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io_, &panel_config, &panel_));

        esp_lcd_panel_reset(panel_);
        esp_lcd_panel_init(panel_);
        esp_lcd_panel_invert_color(panel_, DISPLAY_INVERT_COLOR);
        esp_lcd_panel_swap_xy(panel_, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel_, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
        display_ = new SpiLcdDisplay(panel_io_, panel_,
                                     DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                     0, 0, // gap: 240x320 控制器 RAM 全可见
                                     DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
    }

public:
    FolotoyCard() {
        InitializeI2c();
        InitializePowerSaveTimer();
        InitializeSpi();
        InitializeSt7789Display();
        InitializeButtons();

        GetBacklight()->RestoreBrightness();
    }

    virtual AudioCodec* GetAudioCodec() override {
        static Es8311AudioCodec audio_codec(
            codec_i2c_bus_,
            I2C_NUM_0,
            AUDIO_INPUT_SAMPLE_RATE,
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK,
            AUDIO_I2S_GPIO_BCLK,
            AUDIO_I2S_GPIO_WS,
            AUDIO_I2S_GPIO_DOUT,
            AUDIO_I2S_GPIO_DIN,
            AUDIO_CODEC_PA_PIN,
            AUDIO_CODEC_ES8311_ADDR);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }

    virtual bool GetBatteryLevel(int& level, bool& charging, bool& discharging) override {
        if (!cw2017_dev_) {
            return false;
        }
        // 纯读路径: 不对 CW2017 做任何写操作(CONFIG 写入会让部分芯片临时停止应答 I2C)
        uint8_t vcell[2] = {0}, soc[2] = {0};
        if (!ReadCw(CW_REG_VCELL_H, vcell, 2)) {
            return false;
        }
        uint32_t mv = (((uint32_t)vcell[0] << 8) | vcell[1]) * 3125UL / 10000UL;
        if (mv < 2500 || mv > 5000) {
            return false;
        }
        if (ReadCw(CW_REG_SOC_H, soc, 2) && soc[0] <= 100) {
            level = soc[0];                    // SOC 引擎正常时用真实值
        } else {
            // 该板 CW2017 的 SOC 引擎未配置电池 profile, 读数恒为复位值 0xFEEx。
            // VCELL 实测可靠 -> 查表估算, 误差约 ±5%, 足够日常参考。
            static bool s_warned = false;
            if (!s_warned) {
                s_warned = true;
                ESP_LOGW(TAG, "SOC 无效(%u%%), 电量改用电压估算(VCELL=%lu mV)",
                         soc[0], mv);
            }
            level = VoltToPercent(mv);
        }
        charging = false;                      // CW2017 无独立充电状态寄存器
        discharging = true;
        return true;
    }

    virtual void SetPowerSaveLevel(PowerSaveLevel level) override {
        if (level != PowerSaveLevel::LOW_POWER) {
            power_save_timer_->WakeUp();
        }
        WifiBoard::SetPowerSaveLevel(level);
    }
};

DECLARE_BOARD(FolotoyCard);
