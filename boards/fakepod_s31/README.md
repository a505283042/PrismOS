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



## 阶段 C：PPA Fill 自检与 CPU / DMA2D 复制隔离测试（待实机测量）

**生产渲染路径未修改。** S31 当前 LVGL/APP 的大面积不透明矩形仍由现有 PPA Fill 处理，只有通过启动校验才注册 LVGL Fill；图片/帧拷贝仍用 CPU `memcpy`。新增的 DMA2D **仅供用户手动发起的诊断**，不向 WASM 内存执行 DMA，不参与播放和正常界面刷新。

### 先确认 Fill

S31 的 `aster_ui_lvgl_init` 注册 PPA Fill；启动校验由原先的 32×16 局部 RGB565 填充扩展为两步：

1. LVGL 缓冲局部红色填充：校验区域内/外全部像素；
2. 128×64 对齐 PSRAM 缓冲里执行 96×48＝4,608 像素的 APP 大面积品红色填充，同时验证未填充的蓝色边框；这真实跨过现有 4,096 像素门槛。

失败即保留软件绘制（不能将本次判为 PPA 成功）。串口应看到：

```text
I (...) aster_ppa: RGB565/cache self-tests passed (LVGL + 4608px APP); fill ready
```

桌面空闲时输入 `aster ui bench 8`，结束时会打印 `UI PPA fills=... app_fills=... pixels=... errors=...`。请同时查看 `UI PERF` 与是否有画面异常。**计数大于 0 表示该条件下实际命中加速；计数 0 只说明该场景未命中，不代表自检失败。**

### 复制速度比较（不含功耗推断）

设备退出 APP、停止本地音乐与蓝牙音频后，在 Aster 串口控制台执行：

```text
aster ui stagec
```

诊断独立启动低优先级任务，使用 64B 对齐 PSRAM 源/目标缓冲与 ESP-IDF 6.1 **公开接口** `esp_async_color_convert_install_dma2d`、`esp_color_convert_blocking`（同格式 `ESP_COLOR_FOURCC_RGB16`，不做颜色转换）。记录 64²、128²、256²、448² 四组连续 RGB565 复制，CPU 和 DMA2D 交替测量，记录每次耗时和最小值，测量区外逐字节验证。

预期输出格式：

```text
STAGEC BENCH begin fill_ready=1 dma2d=1 memory=PSRAM
STAGEC BENCH edge=256 bytes=131072 reps=25 cpu_avg_us=... dma2d_avg_us=... cpu_min_us=... dma2d_min_us=... dma2d_available=1 pixel_match=1
STAGEC BENCH done; production copies remain CPU
```

数值必须来自实机，本文不预填任何结果。DMA2D 不可用会显示 `dma2d=0`，仍记录 CPU 基线；任意 `pixel_match=0` 视为失败，禁止启用 DMA2D 生产拷贝。该基准**仅是 PSRAM 到 PSRAM、cache-warm、固定对齐、完整矩形**的复制对比，不代表非对齐局部刷新、WASM 线性内存或 LCD SPI 传输。

### 外部功耗比较

```text
aster ui stagec-power idle
aster ui stagec-power cpu
aster ui stagec-power dma2d
```

每条命令各输出 `STAGEC POWER begin`、8 秒采样窗口、`STAGEC POWER end`（耗时、复制次数、处理字节和像素正确性）。在这 8 秒内用**外置供电电流表或功耗计**测平均功率；ESP-IDF 日志只记录时间和工作量，不能直接量出瓦特或毫安。保持供电电压、屏幕亮度、Wi-Fi/BT、温度和是否连接 USB 一致；每组至少重复三次，互换 CPU/DMA2D 测试顺序。

计算单位数据能量：`(P_test - P_idle) × elapsed_seconds / transferred_MiB`（J/MiB；若只测电流，先乘供电电压换算 W）。省电需比较能量/数据与整机平均功率，而不能把耗时更短等同于功耗更低。

**验收决策：** 只有 RGB565 全部正确、无音频/触摸/刷新退化，且目标尺寸下 DMA2D 在多轮测试中持续明显快于 CPU，并且 J/MiB 不高于 CPU 时，才考虑针对该尺寸启用。小块、非对齐、WASM 目标和失败重试继续 CPU；不能证明收益时，阶段 C 以“保留当前实现”验收。绝不以单次缓存命中成绩决定生产切换。

固件测试与构建（先别刷机）：

```powershell
cd D:\ESP32\aster-firmware
git pull --ff-only origin main
python -m unittest discover -s tests -p "test_stage_c_copy.py" -v
python -m unittest discover -s tests -p "test_hifi_cover_stage_b.py" -v
idf.py --preview -C boards/fakepod_s31/idf -B D:/ESP32/aster-firmware/build/fakepod-s31/system -DIDF_TARGET=esp32s31 build
```

如 S31 IDF 6.1 构建或公开 DMA2D 接口与已安装框架有差异，以本地编译错误为准修复，**不得在未编译成功时烧录**。
