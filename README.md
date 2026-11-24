# PrismOS

**一个系统核心，一套 APP 功能逻辑，多种完整视觉与交互体验。**

PrismOS 是基于 [Aster 固件](https://github.com/a505283042/aster-firmware) 的轻量嵌入式系统项目。保留原有核心服务、应用兼容性及硬件支持，通过公共 UI Shell 将桌面、系统界面与 APP 表现层逐步分离。目标是让用户无需重新烧录固件即可从服务器下载、安装和切换完整主题；支持的 APP 自动匹配独立界面分包，未适配的 APP 保留自带界面。

首发验证设备为 FakePod Nano Pro（ESP32-S31，460×460 CO5300），保留 ESP32-S3 Zephyr 和 ESP32-P4 ESP-IDF 原有平台支持。

> 当前版本已实现固件内 FLUX / Probe 静态主题切换及压力诊断。外部桌面、APP View 分包、自动下载和主题商城属于 v0.5 后续开发目标，尚未实现。

## 系统架构

| 层级 | 职责 |
| --- | --- |
| System Core | 原 Aster 设备、音频、网络、存储、运行时及 HAL |
| UI Shell | 公共显示调度、输入、电源、快捷面板、应用目录、主题生命周期及回退 |
| Theme / View | 独立桌面布局、系统视觉及 APP 界面、动画和交互 |
| APP Core | 各 APP 的业务状态、数据、动作和存档；自带完整默认 View |

```text
          静态 HTTPS 资源服务器（计划）
                     │
           下载 / 校验 / 安装管理
                     │
              TF 卡独立版本包
         Theme / View / Font / Sound
                     │
      UI Shell / Theme Runtime / View Resolver
             │                    │
     内置 FLUX / 外部桌面    APP Core + 默认 View
             └────────┬───────────┘
                Aster System Core
                 现有 HAL / BSP
```

主题不仅是换色。手绘/涂鸦、黏土拟态、像素、蒸汽波等风格可以拥有独立桌面和适配 APP 的独立布局、控件、动画及导航。具体首个外部主题风格待选择。

## 独立主题与 APP 分包（计划）

| 包 | 职责 |
| --- | --- |
| APP | 功能逻辑与默认界面；保留原 .ast 安装兼容 |
| Theme | 可下载桌面主题及资源，提供建议字体/提示音、View 依赖提示 |
| View | 绑定 Theme ID + APP ID + View API 的独立 APP 表现层 |
| Font / Sound | 可独立选择及更新的字体、提示音 |
| Asset | 按需共享图标、图片及动画，不要求单独包框架 |

例如 HiFi 的播放、索引、歌单、歌词、封面数据及频谱查询由同一 Core 提供；不同 View 可以实现完全不同的播放器布局。独立主题包与 View 包的目标是无需为每个主题重新编译 APP。现有旧 APP 不需要强制升级。

用户选择服务器主题后，设备下载并验证核心桌面包，按本机已安装 APP、Theme ID、APP ID、接口版本和硬件能力补齐兼容 View。无需下载未安装 APP 的适配包；没有匹配、下载失败或版本不兼容则使用 APP 自带 View。前台 APP 不热替换 View，下次启动采用新的匹配。断网、缺卡、主题损坏时内置 FLUX 仍作为救援桌面。下载不阻塞 UI，不抢占后台音乐资源。

外部桌面与 View 的执行方式（候选 WAMR AOT 或受限布局）须先通过技术验证；尚未确定二进制格式，不假定独立 AOT 模块可以共享 C 指针或线性内存。所有可执行分包必须校验可信来源、权限、版本及资源完整性。

## 开发路线

| 阶段 | 内容 | 状态 |
| --- | --- | --- |
| P0 | 原版基线验证 | S31 用户确认通过 |
| P1.1 | 公共 UI Shell 转发 | 通过 |
| P1.2.1–P1.2.4 | 生命周期、输入、电源、应用目录、公共设置 | 通过 |
| P1.3 | 静态主题注册及安全切换 | 功能及性能回归通过 |
| P1.3.1 | 主题及内存诊断 | 命令验证通过 |
| P1.3.2 | 生命周期隔离、音频资源保护及压力测试 | 140 次切换及内存回归通过 |
| **P1.3.3** | **外部 Theme / APP View 分包架构设计** | **下一阶段** |
| P1.4.1 | 外部桌面运行技术验证 | 待开发 |
| P1.4.2 | 本地主题包解析、注册、选择及卸载 | 待开发 |
| P1.4.3 | 第二套正式可安装主题 | 待开发 |
| P1.5.1 | HiFi Core / 默认 View 分离 | 待开发 |
| P1.5.2 | 版本化 APP View 接口 | 待开发 |
| P1.5.3 | 外部 View 加载与 ID 绑定 | 待开发 |
| P1.5.4 | 默认回退及独立 HiFi 主题 View | 待开发 |
| P2 | 独立字体包 | 待开发 |
| P3 | 独立提示音包 | 待开发 |
| P4.1 | 统一资源安装、版本管理及回退 | 待开发 |
| P4.2 | 静态服务器主题及 View 索引 | 待开发 |
| P4.3 | 主题及 APP View 依赖自动下载 | 待开发 |
| P4.4 | 主题商城及个性化设置 | 待开发 |
| P4.5 | 完整系统验收 | 待开发 |

详细边界、接口候选、实施任务、安全约束及验收矩阵见 [开发计划 v0.5](docs/DEVELOPMENT_PLAN.zh-CN.md)，为当前唯一实施计划。

## 当前实机状态（2026-11-24）

- [整合基线 8664f26](https://github.com/a505283042/PrismOS/commit/8664f26f23fb911f01aa4a6f186d01d8e1e9c3c1) 包含 Aster 主线/F1（2ff46e1）、G1（9655a48）、H1（e97f82f）最终优化，准备工作及 P1.1/P1.2 按原顺序保留。H2 封面优化未实施。
- P1.3（b204f47）：S31 24 次切换成功，FLUX benchmark 18.3→18.2 FPS，PPA/LCD 错误为零，生产 LCD PIPE 恢复 ON。
- P1.3.1（ca6157e）：内存及主题统计命令实机通过。
- P1.3.2（c043bb6）：累计 140 次切换成功，纯桌面 100 次 internal_free 133091→133119 B、PSRAM 14669664 B 不变；FLUX hide 净释放 577584 B PSRAM、12 个分配块；最后按后台播放测试流程执行的 20 次切换，内部 RAM 97915 B、PSRAM 13597776 B 保持不变；原 player start -16 未复现。
- 以上为用户提供的 S31 实机日志结论，不能等同全平台或全部异常路径通过。后台音乐听感、重启恢复、完整 APP 返回及主机测试/其他板型仍待补充确认；早期内存下降的具体分配来源未完全追踪。

## 已实现诊断命令（ESP-IDF）

```text
aster ui theme status
aster ui theme flux
aster ui theme probe
aster ui theme stats
aster ui mem
aster ui theme trace on
aster ui theme trace off
aster ui theme stress 100
aster ui theme stress stop
aster ui bench 8
aster ui pipe status
```

stress 从空闲 FLUX 开始，支持 2–200 的偶数切换次数，100 次即 50 次往返，每 10 次报告内存；停止时保留当前主题。测试期间不要操作桌面或启动 APP。trace 默认关闭，记录 hide/init/show/frame/KV 阶段采样，不是分配调用栈追踪。比较相同主题、页面及播放负载的预热后当前空闲量、连续块、对象及定时器数。原生 DAC 独占时跳过旧 UI 提示音，保留硬件仲裁；主题切换不触发 APP 返回音。

## 开发原则

- 保留 Aster ABI 4.5.0 旧应用兼容、原 APP 安装路径、WAMR、现有 HAL/BSP 和 LCD 16 行乒乓流水线。
- 单一 UI 所有者、单一活动桌面、无每主题常驻全屏缓冲；后台音乐状态不随主题切换重置。
- 外部代码受权限、版本及资源约束，可信签名/清单校验后方可执行；不加载任意本机代码，不建通用插件框架。
- 复用现有存储、下载、安装、字体和音效基础设施；本地可用先于网络发布，不强制新服务器或新 SDK。
- 下载暂存、校验、原子提交，运行版本不可覆盖；串行有界下载，音频优先，失败不影响默认界面。
- 保持首次初始化 11 项协议及旧默认下载源兼容，不格式化用户 TF 卡；无网络亦可启动。
- 每阶段独立中文提交、测试及回退，不同时重构核心、HAL、实时音频及下载协议。

## 开发文档

- [PrismOS 开发计划 v0.5：独立 APP 功能与可下载主题生态](docs/DEVELOPMENT_PLAN.zh-CN.md)
- [原始 Aster 项目说明](docs/ASTER_UPSTREAM_README.md)
- [现有平台适配接口](docs/platform-adapter-api.md)
- [现有首次开机流程](docs/first-boot-setup.md)
- [现有下载源配置](docs/distribution-config.md)

项目新增说明、开发记录及测试报告统一使用中文，技术标识和命令保留原文。

## 编译与开发基线

参考版本：[Aster 提交 2ff46e11](https://github.com/a505283042/aster-firmware/commit/2ff46e11bb63f12b613fc7fc071a6014d243b832)。

按各板卡原有构建说明及 SDK 版本编译；独立发布配置完成前不触发继承的 firmware-v* 固件发布工作流。

## 版权与许可

PrismOS 保留原始 Aster Git 历史、许可证及版权声明。详见 [LICENSE](LICENSE)、[NOTICE.md](NOTICE.md) 及第三方许可文件。主题、字体、音效及其他资源必须确认合法分发权并保留必要署名。
