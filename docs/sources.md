# 设计依据与来源

以下为设计时核对的官方资料（2026-10-19访问）。资料可能更新，采购时再次核对具体型号、封装和版本。只保留链接，不把原厂手册整体重新分发。

- [STM32F103C8 产品页](https://www.st.com/en/microcontrollers-microprocessors/stm32f103c8.html)及[数据手册](https://www.st.com/resource/en/datasheet/stm32f103c8.pdf)：存储、引脚、电压与外设限制。
- [ST RM0008参考手册](https://www.st.com/resource/en/reference_manual/rm0008-stm32f101xx-stm32f102xx-stm32f103xx-stm32f105xx-and-stm32f107xx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)：RCC、GPIO、ADC、TIM、USART、RTC、Flash、IWDG寄存器。
- [ST CMSIS Device F1](https://github.com/STMicroelectronics/cmsis-device-f1/tree/v4.3.5)与[Arm CMSIS Core 5.9.0](https://github.com/ARM-software/CMSIS_5/tree/5.9.0)：头文件来源，逐文件哈希见vendor/manifest.json。
- [DS18B20 官方数据手册](https://www.analog.com/media/en/technical-documentation/data-sheets/DS18B20.pdf)：外部供电、单总线、12bit转换最长750ms、CRC与分辨率。
- [MH-Z19B 官方产品页](https://www.winsen-sensor.com/product/mh-z19b.html)及[手册](https://www.winsen-sensor.com/d/files/manual/mh-z19b.pdf)：停产状态、供电、预热。当前B型手册删去了部分UART详情，读取命令同时参考C型官方手册，B型实物协议仍待验证。
- [MH-Z19C 官方手册](https://www.winsen-sensor.com/d/files/manual/mh-z19c.pdf)：5.0±0.1V、UART9600/8N1、0x86查询与数据校验。项目统一保守预热180秒。
- [SSD1306 官方产品页](https://www.solomon-systech.com/product/ssd1306)：控制器类别；具体OLED成品板的供电、地址和上拉按所购模块原理图确认。
- [AO3400A 官方数据手册](https://www.aosmd.com/sites/default/files/res/datasheets/AO3400A.pdf)：2.5V门极条件下的导通参数与热限制，不以绝对最大电流作为面包板负载能力。
- [Tower Pro SG90](https://towerpro.com.tw/product/sg90-7/)：具体舵机和连杆需实测行程，克隆模块不能假定具有相同参数。
- [pySerial 3.5 API](https://pyserial.readthedocs.io/en/latest/pyserial_api.html)：超时读取可能返回半行，因此电脑端按LF重组。
- [Zig 官网下载清单](https://ziglang.org/download/index.json)：本机测试工具链0.15.2及校验值；工具链没有放进仓库。

原计划书仅用于提取功能需求，没有沿用其中未经测量的性能和商业结论，也未公开其中的个人身份信息。
