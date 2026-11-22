# FakePod S31：免拔 TF 卡的 USB 读卡器

**状态：实验性固件分支。尚未在真机上以 ESP-IDF 6.1 编译及验证。**

分支：`feat/fakepod-s31-usb-msc`。主线固件默认不开启 MSC，回退到主线即可取消此功能。

## 先用零固件改动的安装方式（推荐反复更新 APP）

Aster SDK 4.5.0 的 `aster deploy` 已经能从 Windows 的 USB 串口上传 `.ast`、验证并安装。上传全程不需要 TF 卡读卡器。

```powershell
. "D:\ESP32\aster-sdk-4.5.0\env.ps1"
cd D:\ESP32\aster-apps

aster ports
aster probe --target riscv
aster build HiFiPlayer --target riscv --aot
aster deploy HiFiPlayer/dist/riscv/aster.hifi.ast --target riscv --port COM7 --run
```

把 `COM7` 改成 `aster ports` 列出的实际设备端口；使用 `aster probe` 验证 `serial=shared`。传输前务必关闭串口监视器（`idf.py monitor`、串口助手等），否则会报端口占用。这里的 `aster` 来自 **Aster APP SDK**，不需要编译或更新固件。

## USB U 盘模式：使用已经实现的板级 MSC 代码

这条分支只让 `CONFIG_ASTER_FAKEPOD_USB_STORAGE=y` 在 S31 默认构建中生效，复用仓库现有的 `usb_storage.c`、S31 Power callback、TinyUSB 2.x、SDMMC raw-sector reader、FLUX 系统电源菜单入口与恢复流程；不修改 APP／音频／I²S。

**构建固件需要单独安装/激活 ESP-IDF 6.1（esp32s31 preview target），Aster APP SDK 不能代替 ESP-IDF。**

```powershell
cd D:\ESP32\aster-firmware
git fetch origin
git switch --track origin/feat/fakepod-s31-usb-msc

# 下列命令必须在已经激活 ESP-IDF 6.1 环境的终端里执行。
# 选择独立 build 目录，避免以前固件 build 中的 sdkconfig 覆盖 MSC 开关。
idf.py --preview -C boards/fakepod_s31/idf -B build/fakepod-s31/usb-msc -DIDF_TARGET=esp32s31 build
```

构建时核查 `build/fakepod-s31/usb-msc/config/sdkconfig.h` 包含：
`#define CONFIG_ASTER_FAKEPOD_USB_STORAGE 1`，并确认产物与目标板无误。

固件烧录涉及设备可启动性，**没有做第一次 build 与目标板测试之前，不要覆盖目前工作正常的固件**。备份当前固件 / 分区信息，并准备可进入下载模式的 USB 串口。在独立成功构建、备份完成后，按设备原有刷机方式烧录 S31 固件。

## 电脑上怎么用

1. 设备正常进入 FLUX 桌面，进 **电源页面 → USB 读卡器**。没有这一菜单则说明当前固件并未开启 MSC。
2. 系统先退出前台 APP、停止音频会话和网络传输，卸载 `/SD`，USB 模式才接管 TF 卡；此期间不得继续通过 shell 或 APP 读取 SD。
3. 使用设备 Type-C 原生 USB 一侧连接 Windows。若只枚举到串口没有磁盘，按板级说明**翻转设备端 Type-C 插头方向**；注意这样会断开原有串口连接。
4. Windows 出现磁盘盘符后，拷贝 `aster.hifi.ast` 到 `<盘符>:\aster\install\aster.hifi.ast`；也可以增删 `music\` 里的歌曲、歌词、封面或在断电前备份 `aster\apps\aster.hifi\data\` 下的索引。
5. **Windows 右键磁盘 → 弹出**，确认安全弹出后再断开 USB。固件在 USB 设备停用或拔线后释放 TinyUSB / SDMMC，然后重新挂载 TF 卡。
6. 重返系统后，安装 APP 还需在串口 shell 执行：
   `aster app install /SD:/aster/install/aster.hifi.ast`；MSC 只是 USB 文件拷贝，**不会自动安装** `.ast` 包。为了只更新 APP，优先用前文的 `aster deploy`。

## 重要安全规则

- **不能让 PC 和固件同时挂载写入同一个 FAT 文件系统**。USB MSC 导出整卡，宿主电脑拥有独占权；MSC 运行时设备播放器、音乐库和索引读写必须停止。
- 不能直接拔线而跳过 Windows 弹出，尤其是 Windows 正在写入文件时。
- 不要点击 Windows「格式化磁盘」。若 Windows 无法识别，先退出 USB 模式，回到原生 SD 挂载检查，不要格式化。
- 功能首次验证请先拷贝一个小测试文件，安全弹出，确认电脑文件与设备端可读且原歌曲和缓存完整，再试 `.ast` 包。
- 在电脑上用文件管理器删除 `hifi_catalog_[ab].bin` 或 `hifi_groups_[ab].bin` 是完整重扫手段；必须在设备进入 MSC 模式、Windows 挂载卡之后操作，不能和设备端同时读写。
- 这是已有代码加开关的功能分支，不代表经验证的 USB MSC 传输性能、Windows 兼容性或回退可靠性。真实板载 Type-C 原生 USB 的枚举情况与 ESP-IDF 6.1 组件编译都还需要实机测试。

## 现成代码位置

- `boards/fakepod_s31/idf/main/usb_storage.c`：TinyUSB MSC 扇区 I/O、SCSI eject、SD 访问互斥
- `boards/fakepod_s31/idf/main/storage.c`：`/SD` 卸载、恢复、重新初始化 SDMMC
- `boards/fakepod_s31/idf/main/board_input.c`：Power capability 和 `usb_storage` 回调
- `systems/zephyr/ui/launcher/flux.c`：FLUX 电源页面「USB 读卡器」按钮
- `common/ui/aster_power_ui.c`：传输中的设备提示、安全弹出/返回
- `tests/test_fakepod_usb_storage.py`：扇区边界和 I/O 互斥基础测试
