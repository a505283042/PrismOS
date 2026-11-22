# FAKE POD NANO PRO S31 V1

460×460 CO5300 AMOLED、CST820 单点触摸、ESP32-S31、16 MiB Flash 和 16 MiB PSRAM。设备标识 `fakepod-s31`，应用构建目标 `riscv`。此板使用独立的 ESP-IDF 6.1 环境。资料中的 GPIO 对应 V1 接线。

## 构建

在固件仓库根目录加载 ESP-IDF 6.1 环境后执行。生产工程默认构建 Aster 系统，输出目录与鱼鹰 P4C5 分开。

```sh
idf.py --preview -C boards/fakepod_s31/idf -B "$PWD/build/fakepod-s31/system" -DIDF_TARGET=esp32s31 build
idf.py --preview -C boards/fakepod_s31/idf -B "$PWD/build/fakepod-s31/system" -p PORT flash
```

将 `PORT` 替换为下载接口的串口。设备端 Type-C 插头方向决定连接下载串口或原生 USB。USB 读卡器由板级选项 `CONFIG_ASTER_FAKEPOD_USB_STORAGE` 控制。主线默认关闭；**实验分支 `feat/fakepod-s31-usb-msc` 已开启该选项**。进入 FLUX 桌面「电源 → USB 读卡器」后，固件会先停止 APP／音频并卸载 TF 卡，再交由 Windows 独占访问，安全弹出后恢复设备访问。编译、串口直装和安全验证步骤见 [USB MSC 指南](USB_MSC_GUIDE.zh-CN.md)。

## 实机功能

- 首次联网准备 SD 字体与商城。设备运行 RISC-V AOT 应用，460×460 播放器通过商城或 SDK 串口安装。
- CS43131 有线输出支持 44.1、48、88.2 和 96 kHz 的立体声验证。系统播放器可播放本地 MP3、WAV 和 FLAC，具体位深和采样率取决于解码器与输出。
- 系统蓝牙支持手机向设备播放，以及设备向蓝牙音箱输出 SBC。播放器使用系统选定的输出。QMI8658 可用于摇动切换动态歌词。
- 上方实体键可短按熄屏或唤醒，持续按住约 3 秒关机；关机后用同一按键开机。熄屏期间可继续播放。

接线和适配器选择在 [板级注册](idf/aster_board.cmake)。诊断、已验证组合及仍待测项见 [Hi-Fi 设计与实施记录](../../docs/fakepod-s31-hifi-design.zh-CN.md)。


## HiFi 封面 JPEG 硬解＋PPA Blend（阶段 B，待实机验收）

固件在 S31 上新增可选的 `aster_img_render` V2 图像参数，**沿用已有 WASM 导入名**。V1 的 BMP/PNG/JPEG 软件渲染入口保持不变：

- 支持独立 JPEG、MP3 ID3 APIC、FLAC PICTURE 的 `src_offset + src_length` 精确读取（封面由 APP 定位，不解码整首音频）。
- JPEG 硬件引擎输出 MCU 对齐 RGB565 原图，缩放到不超过 460×460；用 PPA RGB565+A8 黑色蒙版生成压暗封面。PPA 不可用时原生 CPU 生成一次压暗图。
- **只在完整解码后**把原色和压暗两个结果从硬件专用 PSRAM 复制到受校验的 WASM 目标地址；不对 WASM 内存直接发 DMA。
- 单次硬件处理最大 2MiB 压缩 JPEG、210 万原始像素；超过限制、非基线/渐进式、硬件失败或 PSRAM 不足时，让 APP 原有 TJpgDec / V1 软件路径接管；绝不写坏旧封面。
- 对 SD 的 JPEG 读取每次最多 8KiB 并让出调度，避免长时间持有 TF 卡读取任务。UI 进度条刷新不执行解码或 Blend。
- 仅 `CONFIG_IDF_TARGET_ESP32S31` 链接 `esp_driver_jpeg`；其他板仍走 V1 软件路径。旧固件收到 V2 请求返回 `ASTER_EINVAL`，新版 APP 自动回退。

**升级次序：先备份原固件及 APP，编译/刷入新版 S31 固件，然后安装 HiFiPlayer 0.5.0-alpha.9。** 可单独将 APP 安装到旧固件验证软件回退；但未刷新版固件时不会启用硬件 JPEG/PPA。

验证命令（在 ESP-IDF 6.1 环境、固件根目录）：

```sh
python -m unittest discover -s tests -p test_hifi_cover_stage_b.py -v
idf.py --preview -C boards/fakepod_s31/idf -B "$PWD/build/fakepod-s31/system" -DIDF_TARGET=esp32s31 build
```

真机检查日志 `HiFi cover swap ... backend=JPEG_HW dim=PPA`、`hifi_cover_hw: JPEG ... PPA=yes`。单独测试普通 JPEG、MP3 APIC、FLAC PICTURE、渐进式 JPEG（回退）、无效/超大封面（回退）以及有线与蓝牙输出播放中连续切歌。**以上是代码实施记录，尚未宣称已通过板级编译、色彩、DMA 或连续音频实测。**
