# FakePod S31：四种全局播放模式与后台统一切歌

**状态：未编译／未实机验收的实验分支。保留已通过实机验证的 USB MSC 固件作为回退。**

工作分支：\`feat/s31-hifi-global-playback\`；基于 \`feat/fakepod-s31-usb-msc\` 建立，后者已经验证 Windows USB 读卡器、文件读写、安全弹出后恢复桌面和 HiFiPlayer 安装。

与 \`a505283042/aster-apps\` 的 \`feat/hifi-s31-full-port\` 分支中 **v0.5.0-alpha.6** 配套使用。升级前保留经实测的 alpha.5 AST 和旧 USB 固件 build。

## 目标

- 所有歌曲浏览分类（全部／歌手／专辑／年代／文件夹）**不创建各自播放队列**，仅映射唯一的 Catalog Track ID。
- 固件成为本地歌曲自动下一首的唯一所有者；APP 不再抢 \`ENDED\`。
- 四种**全曲库**模式：\`0=顺序到末尾停止\`、\`1=全部循环\`、\`2=单曲循环\`、\`3=全局随机\`。
- 退出音乐 APP / 返回桌面后，固件仍处理自然 EOF；系统上一首、下一首也访问同一曲库。
- 蓝牙输入沿用手机 AVRCP 的 NEXT/PREV，不受本地队列模式影响。
- 已验证的 USB MSC 进入／安全弹出机制保持不变。

## 实现说明

- 新模块 \`hifi_playlist.c\` 只使用已经由 APP 双槽 CRC 检验发布的 \`HIF2\` Catalog（默认路径 \`/SD/aster/apps/aster.hifi/data/hifi_catalog_[ab].bin\`），读取最高有效 generation，并重新校验整份 payload CRC32。**支持目录深度与文件路径由 APP 曲库索引限定，固件不再用原本 root-only 的 \`readdir(/music)\`。**
- 文件存在性用 ESP-IDF VFS 路径 \`/SD/music/...\` 检查，播放命令仍用 Zephyr FS 路径 \`/SD:/music/...\`。
- 随机模式使用一份约 32 KB 的 \`uint16_t\` 曲目顺序表（PSRAM 优先）及 Fisher–Yates；完整一轮内不重复曲目、上一首按当前历史回退。随机顺序在固件运行会话内有效；**不保证断电/重启后保留随机历史**。队列顺序只来自全局曲库而不来自当前 APP 分类。
- 自动 EOF 在音频停止后才读索引；手动 NEXT / PREVIOUS 先停止旧音频会话并等待媒体 worker 退出，再由系统后台任务打开 Catalog。音乐播放中不做额外的播放队列整库扫描。
- 读取 Catalog 的每次操作使用 \`aster_power_activity_begin/end\` 保护，避免与 USB MSC SD 独占交接重叠。文件句柄在本次选歌结束立即关闭。
- 新增的可选 ABI 4.5 常量：\`ASTER_PLAYER_CAP_QUEUE_MODE=256\`、\`ASTER_PLAYER_SET_QUEUE_MODE=14\`、\`aster_player_info_t.reserved[3]\`。不增加 ABI 结构字段，不改变原请求大小。原 SDK 4.5.0 的 APP 使用 capability guard 和临时兼容常量。
- 模式通过 ESP-IDF NVS \`aster_hifi/queue_mode\` 存储（未播放、暂停或自然结束后才写，避免在实时音频中写 Flash）。蓝牙模式 UI 不提供本地循环切换。
- 无有效 HIF2 Catalog 时固件不猜测目录列表，自动下一首可能停止；先启动 HiFiPlayer 完成曲库索引构建。
- 设备只有一条本地播放会话。不会创建另一条 I2S 或 MP3 解码线程。本版不尝试修复既有的偶发音频卡顿。

## 本地构建（使用现有 IDF 6.1.0）

\`\`\`powershell
cd D:\ESP32\aster-firmware
git fetch origin feat/s31-hifi-global-playback
git switch -c feat/s31-hifi-global-playback origin/feat/s31-hifi-global-playback

$pio = "$env:USERPROFILE\.platformio"
$fw = "$pio\packages\framework-espidf"
$python = "D:\ESP32\aster-idf61-python\Scripts\python.exe"
$env:IDF_PATH = $fw
$env:IDF_TOOLS_PATH = "D:\ESP32\aster-idf61-tools"
$env:IDF_PYTHON_ENV_PATH = "D:\ESP32\aster-idf61-python"
$env:ESP_IDF_VERSION = "6.1.0"
$env:PATH = "D:\ESP32\aster-idf61-python\Scripts;$pio\packages\toolchain-riscv32-esp\bin;$pio\packages\tool-cmake\bin;$pio\packages\tool-ninja;$env:PATH"

& $python "$fw\tools\idf.py" --version

# 只构建，不刷机。隔离旧 USB MSC 的稳定 build 目录。
& $python "$fw\tools\idf.py" --preview -C boards/fakepod_s31/idf -B D:/ESP32/aster-firmware/build/fakepod-s31/hifi-global -DIDF_TARGET=esp32s31 build
```

手动检查开启 USB MSC：`CONFIG_ASTER_FAKEPOD_USB_STORAGE=y`；编译成功后验证产物尺寸／分区及 bootloader，再在备份旧固件后烧录。

## 建议升级顺序

1. 先使用旧 USB MSC 固件升级 HiFiPlayer APP 到 alpha.6；在旧固件下模式键会显示「固件」，表示尚不能切换四种全局模式，不会假装后台随机生效。
2. 备份可回退的 USB MSC 固件后，烧录经过 IDF 构建的本实验分支。
3. 重启后确认 FLUX「电源 → USB 读卡器」仍存在并正常退出；打开 HiFiPlayer，模式键依次显示「顺序」「全部」「单曲」「随机」。
4. 以包含嵌套文件夹的短歌单测试顺序与全部循环，然后单曲循环，最后随机下一首／上一首（同一轮不重复）。
5. 返回 FLUX 桌面，不要终止系统媒体服务，播放到自然 EOF，确认固件自动切歌且 APP 不重复发送 PLAY_FILE。
6. 音乐停止后检查再次进入 APP 能回显 NVS 模式。Windows USB 安全弹出后再次确认 APP 与曲库索引完整。

### 已知限制

- 需要 APP 先发布有效 HIF2 双槽索引；没有索引则不会再悄悄使用根目录替代全部曲库。
- 仅支持单机本地 PCM/MP3/FLAC/WAV 全局队列；手机蓝牙接收继续由手机控制。
- 目前模式与循环位置持久化，随机**历史**不跨系统重启。
- 代码提交与基础静态审查已完成，**尚未执行 ESP-IDF 6.1.0 build 和设备实际音乐测试**。不要以此文档代替真机验收，也不要在 USB 数据拷贝时烧录或断电。
