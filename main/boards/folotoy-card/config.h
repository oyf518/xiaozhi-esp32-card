#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

// ============================================================================
// FoloToy-Card (ESP32-C3, 8MB flash)
// 引脚唯一事实来源: 本项目 backup/src_esp32c3_orig/components/bsp/include/bsp_pins.h
// (用户实测验证过的硬件映射，与 xiaozhi 无关的项目同源)
//
// 音频: ES8311 @ I2C 0x18, I2S 全双工
// 显示: ST7789P3 240x320 SPI (RST 硬接 3V3 → 软复位; BL=GPIO21 LEDC PWM 调光)
// 按键: 三键 ADC 分压共用 GPIO0 (上=0Ω / 下=1k / 确定=2.2k / 松开=拉满)
// 电量计: CW2017 @ I2C 0x63
// ============================================================================

#define AUDIO_INPUT_SAMPLE_RATE  24000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

#define AUDIO_I2S_GPIO_MCLK     GPIO_NUM_6
#define AUDIO_I2S_GPIO_BCLK     GPIO_NUM_5
#define AUDIO_I2S_GPIO_WS       GPIO_NUM_3
#define AUDIO_I2S_GPIO_DOUT     GPIO_NUM_2   // 播放: MCU -> codec
#define AUDIO_I2S_GPIO_DIN      GPIO_NUM_4   // 录音: codec -> MCU

// #define AUDIO_CODEC_USE_PCA9557
#define AUDIO_CODEC_PA_PIN       GPIO_NUM_NC // 功放使能脚未接 MCU(常通)
#define AUDIO_CODEC_I2C_SDA_PIN  GPIO_NUM_10
#define AUDIO_CODEC_I2C_SCL_PIN  GPIO_NUM_7
#define AUDIO_CODEC_ES8311_ADDR  ES8311_CODEC_DEFAULT_ADDR

#define BUILTIN_LED_GPIO        GPIO_NUM_NC

#define DISPLAY_SPI_MOSI_PIN    GPIO_NUM_9
#define DISPLAY_SPI_SCLK_PIN    GPIO_NUM_8
#define DISPLAY_SPI_CS_PIN      GPIO_NUM_1
#define DISPLAY_DC_PIN          GPIO_NUM_20
#define DISPLAY_RST_PIN         GPIO_NUM_NC // 硬接 3.3V, esp_lcd_panel_reset() 走 SWRESET
#define DISPLAY_BACKLIGHT_PIN   GPIO_NUM_21
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_PCLK_HZ         (40 * 1000 * 1000) // 原项目实测值(BSP_LCD_PCLK_HZ)

// ST7789P3 控制器 RAM 240x320 全可见(gap=0)。竖屏使用(与原厂固件一致:
// MADCTL 不镜像不换向), 与小智表情资源 320_240 的适配方式同官方竖屏板。
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  320
#define DISPLAY_SWAP_XY false
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
#define DISPLAY_INVERT_COLOR true // 本屏出厂即需反色(无条件 INVON)

// ---- 按键: ADC 分压三键(GPIO0 / ADC1_CH0), 按 raw 12bit 计数分档 ----
// 理论 mV: 上=0mV 下≈300mV 确定≈595mV 松开≈3300mV(atten12db 满量程~3.1V)
// 若手感不对: 打开菜单里 Button 校准思路,按住各键记 ADC 读数取中点改阈值。
#define BUTTON_ADC_UNIT         ADC_UNIT_1
#define BUTTON_ADC_CHANNEL      ADC_CHANNEL_0   // GPIO0
#define BUTTON_LEVEL_UP_RAW_MAX     200    // <200        = 上键
#define BUTTON_LEVEL_DOWN_RAW_MIN   200    // 200..550    = 下键
#define BUTTON_LEVEL_DOWN_RAW_MAX   550
#define BUTTON_LEVEL_OK_RAW_MIN     550    // 550..3000   = 确定键
#define BUTTON_LEVEL_OK_RAW_MAX     3000
#define BUTTON_RELEASED_RAW_MIN     3000   // >3000       = 松开

#endif // _BOARD_CONFIG_H_
