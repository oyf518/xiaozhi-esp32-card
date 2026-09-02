# FoloToy-Card (ESP32-C3)

78/xiaozhi-esp32 自定义板：在 FoloToy-Card（ESP32-C3, 8MB flash）上运行小智。

## 硬件规格

* MCU: ESP32-C3 (ESP32-C3FN4, 8MB flash, 无 PSRAM)
* 音频: ES8311 codec @ I2C 0x18, I2S 全双工
    * MCLK=6 BCLK=5 WS=3 DOUT=2 DIN=4
    * I2C: SDA=10 SCL=7
    * 功放使能脚未接 MCU(常通)
* 显示: ST7789P3 240x320 SPI(竖屏, 原生方向不镜像不换向)
    * MOSI=9 SCLK=8 CS=1 DC=20 RST=-1(软复位) BL=21
    * 需反色(INVON), 无 gap(控制器 RAM 全可见)
* 按键: 三键 ADC 分压共用 GPIO0
    * 上≈0mV / 下≈300mV / 确定≈595mV / 松开≈3300mV
    * 短按"确定" = 开关对话; 长按"确定" = 配网模式; 上/下 = 音量增减
* 电量计: CW2017 @ I2C 0x63 (SOC 寄存器 0x04)
    * ⚠ 芯片 SOC 积分引擎未配置电池 profile, 读数恒为复位值 0xFEEx(≈254%)。
      固件不写任何 CW2017 寄存器(写入曾导致芯片临时停止应答 I2C), 改用
      VCELL(可靠) 查 Li-Po 曲线表估算百分比, 状态栏显示"xx%"纯文本
      (font_awesome 电池字形在本板渲染异常, 已弃用图标仅保留数字)。
    * 若需精确 SOC: 需按电池实际容量向 REG 0x10~0x17 写入容量模型后重启芯片。
    * UI 约束: `battery_label_` 必须保持创建(可为空标签) —— 上游
      `LcdDisplay::SetTheme()` 对其不判空直接设样式, 删除会令主题刷新时崩溃。
      电量仅以 `battery_pct_label_`(纯文本 xx%) 展示在 top_bar_ 右上角,
      字体随主题刷新同步更新。
* 上电自检: I2C 全扫描 + CW2017 VERSION/VCELL/SOC 基线打印, 方便硬件排查

## 快速构建

```bash
idf.py set-target esp32c3
idf.py menuconfig   # Xiaozhi Assistant -> Board Type -> FoloToy-Card (ESP32-C3)
idf.py build
idf.py -p <PORT> flash monitor
```

或用仓库脚本:

```bash
python scripts/release.py build folotoy-card --zip
```

`config.json` 关键配置:
* Flash: 8MB, DIO @ 40MHz
* 分区表: partitions/v2/8m.csv (双 OTA + 2MB assets)
* 唤醒词: CONFIG_USE_ESP_WAKE_WORD=y ("你好小智")

## 注意事项

* C3 无 PSRAM, 内存紧张 —— 本板沿用官方 surfer-c3-1.14tft 的字体/表情档位(20 号字 + twemoji_32)。
* 若按键手感不对: 打开 ADC 调试页记下各键 raw 读数, 取相邻档中点修改 config.h 里 `BUTTON_LEVEL_*` 阈值。
