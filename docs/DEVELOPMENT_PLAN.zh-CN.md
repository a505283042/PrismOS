# PrismOS 开发方案

> 状态：规划阶段，尚未完成固件迁移、编译或真机验证。
> 基线：a505283042/aster-firmware @ 2ff46e11bb63f12b613fc7fc071a6014d243b832（S31 16-row LCD ping-pong 默认开启及回退）。
> 首发硬件：FakePod Nano Pro ESP32-S31 / ESP-IDF 6.1 / 460×460 CO5300 / 16 MiB PSRAM / RISC-V AOT。

## 1. 产品定位
PrismOS 是基于 Aster 的独立嵌入式系统发行版，不是重新开发 RTOS。保留成熟设备服务和应用兼容性，建立独立桌面、字体、主题资源及商城发布生态。品牌口号：One Core. Many Experiences.

## 2. 基线迁移与兼容性
完整导入指定 Aster Git 提交，保留历史、LICENSE、NOTICE、第三方许可与字体署名；原仓库只读，不修改。先验证原版构建，不全局替换 aster_*。保持 ABI 4.5.0、ASTP/ASTI/ASTR、应用 ID、RISC-V AOT、权限、TF 卡路径与 KV 兼容；品牌与内部契约分离。原有 SD 卡不得自动格式化。新路径若需要迁移必须版本化、可回滚。审计 CI 上游发布目标、证书、资源地址、凭据及外部依赖，禁止向上游发布。保留原仓库其他板型，首轮仅 S31 真机验收。

## 3. 分层架构
- Core：common/port、common/audio、platform、板级驱动、aster_runtime、aster_audio、aster_apps、aster_render。原则上不动。
- UI Host：共享 LVGL 9、LCD/PPA/DMA、字体基础加载、输入与 APP 屏幕归属、安装/配网/恢复。保留现有单 UI/APP worker。
- UI Shell：新建统一生命周期、主题注册、系统状态/动作、全局手势、电源控制、快捷面板、诊断接口。
- Theme Manager：内置 FLUX（保底）、Minimal（验证）、Classic/Grid（后续）。每套可改变布局和交互，不仅配色。
- Font Manager：TF 卡多字体家族、字体角色、字号、度量、LRU、缺字回退。
- Asset Manager：主题壁纸/图标/动画资源校验、版本与限额。
- Distribution：独立资源源及商城，先兼容现行初始化协议，再扩展资源目录。

## 4. 代码边界与具体入口
现有 systems/zephyr/ui/launcher/flux.c 同时包含系统行为和表现层，约 2291 行。先保留路径及视觉行为，新增 shell/ui_shell.[ch]、theme_manager.[ch]、ui_system_state.c、ui_system_actions.c、ui_power_controller.c；待两后端构建验证再考虑搬至 systems/shared/ui/，不要一开始大搬迁。
- systems/espidf/components/aster_ui/ui.c：将 launcher init/show/hide/pump、统计与 page 操作逐步经 Shell 转发；不重建 LVGL display。
- systems/espidf/components/aster_apps/apps.c：idle_wait 通过 Shell；APP 生命周期仍由原 worker 管理。
- systems/zephyr/ui/qpanel.[ch]：已有 count/index/name/apply/palette 接口，由 Theme Manager 接管；快捷面板、Wi-Fi、蓝牙、退出确认仍为公共 UI。
- flux.c：逐步移出实体键、息屏、亮度、注册表回调、启动引导、全局音量等；保留原绘图、滚动和动画。
- 两个构建系统的 CMake 同步接入；公共代码避免直接依赖 FreeRTOS/Zephyr 特有头文件。

主题 ops（拟定）：id/name、init、mount(parent)、unmount、tick、input、registry_changed、idle_wait_ms、palette、可选 diagnostics。挂载返回错误，root 由 Shell 所有；主题不得 clean 全局 screen，不得启动自己的常驻线程，不得直接控制音频生命周期。系统设置走统一 action，事件/状态快照只读。

## 5. 主题切换事务
仅在桌面空闲安全点执行：校验兼容性和资源 → 等触摸释放、停止动画、关闭覆盖层、排空显示提交 → 保存旧主题私有状态 → 销毁旧对象/缓存 → 创建新主题 → 首帧成功后提交 KV。失败恢复旧主题，仍失败回到内置保底 FLUX；启动失败不循环。APP 活跃、配网、安装、USB MSC、诊断时拒绝或延期，不停止后台播放。只激活一个主题；不额外常驻整帧缓冲。全局音量、亮度、息屏时间独立保存，迁移旧 FLUX KV 一次；主题用稳定 ID，不用易变索引。

## 6. 多字体与资源
沿用 .cpfont 12/14/16/18/20/24/28/32/40/48px 格式和按需字形缓存。现有 /SD/aster/fonts/ui_*.cpfont 保留为默认回退及旧 APP ABI 路径。扩展目录 /SD/aster/fonts/families/<id>/font.json、ui_<px>.cpfont，清单记录版本、字号、度量、覆盖范围、SHA256、许可。缓存 key 为 family+size+glyph，保留全局内存上限，旧对象释放后才撤销字体引用。Body/Title/Caption/Numeric/Mono/Lyrics 角色，用户覆盖主题默认值；缺字回退默认字体。旧 APP 字体度量契约不得悄悄变化，HiFi 歌词个性化另行适配。避免 SD 冷读干扰播放，按需预热并限制 I/O。固定救援字体内置。主题资源目录 /SD/aster/themes/<id>/，初期只允许数据资源，不执行任意代码，清单校验路径、体积、版本、板型及哈希。

## 7. 自有资源发布
规划 prism-releases（尚未创建/发布）；候选入口 https://github.com/a505283042/prism-releases/releases/latest/download/aster-source.json。先建立可匿名读取的静态 Release，复用 aster-apps/tools/prepare-publication.py 和 aster-sdk/tools/aster.py distribute publish，锁定输入仓库及提交。现行 setup 固定 11 项（10 字体+商城 AST），保留 schema、board=fakepod-s31、target=riscv、460×460、ABI/能力检查、SHA256、Range、不可变版本 base_url、TLS 验证。商城 market_url 同步替换；绝不嵌入 Token。验证完整发布后才修改 config/distribution.json（ca_bundle 保持可信根）；未上线前不写死无效地址。已有设备用 aster ui setup-source URL，无需格式化；保存地址优先于编译默认。主题/字体另建版本化资源目录，不修改初始 11 项清单。先 NAS 局域网验证，再 GitHub 公网；发布先校验全部资源，再原子切换入口。SHA256 完整性不代替签名，后续评估签名目录和固件 OTA，默认不启用 OTA。

## 8. 开发里程碑（每阶段独立提交、可回退）
|阶段|工作|验收|
|---|---|---|
|P0|完整基线导入、许可证/CI 审核、原版构建与实机基准|S31 原功能正常，记录基线|
|P1|独立发布资源与商城、品牌入口|空卡初始化/断点续传/离线健康卡/旧卡换源通过|
|P2|UI Shell 转发，唯一主题 FLUX|行为和画面不变，原 bench 可用|
|P3|提取公共电源、输入、系统动作、注册表|熄屏/唤醒、快捷面板、APP 启停正常|
|P4|Theme Manager + Minimal 测试主题|切换、重启恢复、故障回退、内存无持续增长|
|P5|Font Manager + 两套字体|中英文/数字/缺字回退、无裁切、旧 APP 兼容|
|P6|Classic/Grid + 主题资源包|UI 定制、资源校验、卡缺失保底|
|P7|商城资源分类、发布自动化|主题/字体安装、更新、回滚|
|P8|后续优化与 OTA 评估|单独安全设计、签名和恢复验证|

## 9. 测试与性能门槛
P0 记录 aster ui bench 8、UI PERF、PPA、PIPE、PSRAM/内部 SRAM、LCD 错误、音频 underrun 和最大间隔。460×460 RGB565 单帧 423200 B；禁止每个主题私有常驻全屏 buffer。保留 16-row ping-pong runtime off 回退。相同板、固件配置与测试场景下比较 FPS、P95、内存和音频，目标不劣于基线；不虚报实测结果。
主机回归：python -m unittest discover -s tests -v；python tools/check_docs.py；python tools/export_contract.py。S31 构建：idf.py --preview -C boards/fakepod_s31/idf -B build/fakepod-s31/system -DIDF_TARGET=esp32s31 build（ESP-IDF 6.1）。
真机：有线和蓝牙 SBC 收发、后台播放中主题切换、APP 启停、反复切换 100 次、冷/热启动、触摸释放、息屏唤醒、注册表更新、低内存/损坏资源/拔卡、配网/安装冲突、断电恢复。测试时备份 SD；不得自动格式化。每阶段记录已做/未做测试及回退提交。

## 10. 当前任务
先完成源码完整迁移，再建立 P0 基线；之后逐项实施。当前文档不代表上述功能已实现。
