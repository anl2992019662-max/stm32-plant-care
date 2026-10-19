# 编译与烧录

## 已验证的构建环境

Windows，Arm Compiler for Embedded 6.24，C11，`-Oz -Wall -Wextra -Werror`，CMSIS Core 5.9.0，STM32F1 Device v4.3.5。本工程不依赖 STM32 HAL，也不要求从 CubeMX 重新生成文件。更换工具链后应重新运行测试与固件检查。

### 方法一：构建脚本

```powershell
python tools/build_keil.py --bin "C:\Keil_v5\ARM\ARMCLANG\bin"
```

输出位于 `build/keil/`，包括 HEX、BIN、AXF、连接映射和尺寸摘要。脚本不下载工具链、不修改全局设置。若安装目录不同，请替换 `--bin`。Arm Compiler 5 不支持本工程中的 Clang 属性/内联汇编。

### 方法二：Keil 工程

打开 `keil/PlantCare.uvprojx`，目标 `PlantCare`，Arm Compiler 6。安装 ST 的 STM32F1xx_DFP（参考配置为 2.2.0）后构建。不要再添加另一份启动文件或 HAL 的 `SystemInit`。工程使用自定义 scatter 文件；不要勾选自动生成布局覆盖它。输出在 `build/mdk/`。

## 内存和时钟

| 区域 | 范围 | 用途 |
|---|---|---|
| Flash 程序 | `0x08000000–0x0800F7FF` | 62 KiB |
| 配置 A | `0x0800F800–0x0800FBFF` | 1 KiB Flash 页 |
| 配置 B | `0x0800FC00–0x0800FFFF` | 1 KiB Flash 页 |
| 数据 RAM | `0x20000000–0x20003FFF` | 全局数据，未使用部分留空 |
| 栈 | `0x20004000–0x20004FFF` | 独立 4 KiB UNINIT，初始 SP=`0x20005000` |

主时钟使用内部 HSI/2×16=64 MHz，避免把某个核心板的外部晶振作为启动前提。APB1=32 MHz，APB2=64 MHz，ADC=64/6 MHz，TIM3=64 MHz；UART=9600，TIM3 50 Hz PWM。HSI 有误差，串口波形及单总线时序仍须实测。这里没有把芯片最高 72 MHz 当作实际配置。

启动文件先设置时钟，再由 microlib 完成 C 数据初始化。栈区不参与清零。程序空间按真正的 C8 64 KiB 限制设计，不依赖部分板子可能存在的额外 Flash。

## SWD 烧录

1. 断开水泵、灯、风扇和舵机的负载电源；BOOT0=0。
2. ST-Link：SWDIO→PA13，SWCLK→PA14，GND→GND，VTref→板上 3.3V，必要时连接 NRST。VTref 是检测参考，是否提供供电取决于探针型号；避免探针与外部电源冲突。
3. 在 STM32CubeProgrammer 或 Keil ST-Link 中识别芯片，核对是 STM32F103C8T6 及 64 KiB Flash。
4. 首次测试可使用整片擦除；这会清除旧配置。日常更新选择仅擦除涉及程序的页，以保留最后两页配置。
5. HEX 自带地址；BIN 的起始地址必须为 `0x08000000`。写入后执行校验，再复位。
6. 确认蓝牙输出 `BOOT PLANT 1.0.0 MANUAL OUTPUTS_OFF`，OLED/遥测出现数据后再按验收顺序连接负载。

`release/` 是软件已编译、尚未在实物验证的参考版本。`python tools/verify.py` 检查依赖哈希、源码与固件清单、HEX 校验和、向量及地址边界；它不能证明板上运行正确。

重新发布包的顺序：先构建，再运行 `tools/test.py`，然后 `python tools/package_release.py` 和 `python tools/verify.py`。打包工具会拒绝与源码哈希不匹配的旧构建/旧测试结果，不自动上传任何文件。

## RTC

优先启用板上 32.768 kHz LSE；约 1.5 秒未起振则回退 LSI（默认按 40 kHz 分频，误差较大）。用 `TIME <Unix秒>` 设置 UTC；未设定时 epoch=0。OLED 的 LSE/LSI 是所选时钟源提示，不是精度测量。LSE 配合正确的 VBAT 备份接线才可验证断电保持。LSI 回退不承诺断电继续计时；主时钟及 RTC 的实际误差都待实测。
