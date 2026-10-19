# 第三方文件和许可

`vendor/manifest.json` 列出逐文件来源及 SHA-256，下载后保持原样。

| 文件 | 固定来源 | 许可 |
|---|---|---|
| `vendor/cmsis-device-f1/` | STMicroelectronics/cmsis-device-f1，标签 `v4.3.5` | 原 `License.md`：Apache-2.0 |
| `vendor/CMSIS/` | ARM-software/CMSIS_5，标签 `5.9.0` | 原 `LICENSE.txt`：Apache-2.0 |

ST 的原始启动汇编作为上游参考随附，实际工程使用 `firmware/src/startup.c`。没有使用第三方 OLED 字库或从原计划书复制姓名、照片、论文、电路截图。

固件链接使用本机 Arm Compiler 的 microlib；编译器和库本身不作为源码依赖打包分发。重建需要用户自己的有效工具链安装。本项目不更改或绕过工具链许可。电脑实物连接可选依赖 pyserial 3.5，按其自身许可安装，不将依赖包复制进仓库。测试使用的 Zig 工具链也不随仓库分发。

应用层代码、电脑工具和新增文档尚未指定统一开源许可证。公开可查看与授予特定开源许可是不同的事项；后续由项目权利人选择许可。
