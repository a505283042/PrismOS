# 平台适配接口 v1

此接口属于固件内部，独立于 APP ABI 4.3。P4/IDF、lckfb/Zephyr 和桌面后端使用同一组 C 契约；外设引脚、厂商驱动和操作系统对象由适配器持有。

## 四个边界

- `common/port/aster_device.h`：显示、帧缓冲、方向和坐标、触摸与按键、音频、IMU、相机、Wi-Fi。`aster_device_ops.def` 列出全部 112 项已有硬件操作。未提供的状态操作返回不支持，布尔查询返回 false，指针查询返回 NULL；void 通知为空操作，调用可选 void 诊断前查询 `aster_device_has()`。驱动原有返回值保持兼容。
- `aster_os.h`：内存区域、时钟、睡眠、文件/目录/容量、递归互斥锁、计数信号量、任务启动与等待。公开句柄只含不透明指针；Zephyr、FreeRTOS、VFS 结构体不穿透接口。返回值为负 errno；读取、写入返回实际字节数。超时单位是毫秒，`ASTER_OS_WAIT_FOREVER` 表示无限等待。任务优先级 0 最高，栈大小单位为字节，core=-1 不绑定 CPU；不支持的亲和性必须报错。
- `aster_services.h`：设备资料和实际能力、应用注册表快照、启动/安装/卸载入口、代码目录、KV、存储状态和日志。注册表返回调用方拥有的快照，避免公共 UI 持有原生注册表中的易失指针。APP 启动/安装入口继续沿用原服务的结果约定。
- `aster_network.h`：41 项异步 TCP/UDP、HTTP(S)、MQTT、WebSocket 和网络事件操作。接口保留 owner、代际句柄、EAGAIN/EOF、部分读写和关闭归属的原有规则。公共代码不获取 socket/TLS 对象。证书校验必须保留，不能在缺少 TLS 实现时降级为 HTTP。

所有接口头只依赖标准 C、公共类型及 APP 契约头。公共 UI 的布局尺寸来自运行时显示适配器，不再引用设备树。当前 UI 支持的逻辑高度下限为 240 像素；更小的屏幕需要先调整下拉面板和手势布局。

## 复用与覆盖

每个模块都有 `derive(out, base, overrides)`、`bind(ops)` 和 `seal()`。这相当于 C 中的基础实现与方法覆盖：覆盖表的 NULL 成员继承基础方法；要禁用继承的方法，应在派生后显式清空该成员。版本、结构体大小与名称在派生和绑定时校验。

```c
static aster_device_ops_t board_ops;
const aster_device_ops_t overrides = {
    .size = sizeof(overrides),
    .version = ASTER_DEVICE_API_VERSION,
    .name = "my-new-board",
    .backlight_set = my_backlight_set,
};
int error = aster_device_derive(&board_ops, &family_ops, &overrides);
if (!error) error = aster_device_bind(&board_ops);
/* Check error before starting any worker. */
```

表及名称必须覆盖整个系统生命周期。只能在启动工作线程之前绑定；`seal()` 后再次绑定返回 EBUSY，禁止带着活动句柄切换后端。默认后端直接使用常量表，不复制到 RAM，也不为接口层新建常驻线程。

句柄必须零初始化，不能复制活动句柄。close/destroy 成功后清空句柄。销毁锁、信号量前必须确认没有持有者或等待者；task_join 超时保留句柄供重试，成功才释放任务记录。文件适配器按打开/关闭申请和释放底层句柄，不保留额外常驻文件缓存。

## 现有后端

- `systems/zephyr/port/`：绑定现有 ESP32-S3 HAL；文件、时间、内存、同步和任务使用 Zephyr 实现。
- `systems/espidf/port/`：绑定 P4 显示/触摸、C5 Wi-Fi 和音频；系统对象使用 FreeRTOS 和 IDF VFS。
- `systems/shared/port/`：两端已有服务的绑定，以及复用现有 DMA 安全读写的文件适配器。

原 HAL、服务头的位置保留转发头，现有板级驱动可以逐步迁移。Launcher、字体、Wi-Fi 页面、下拉面板、启动画面和流光绘制改用新接口；首次开机 UI 也通过显示/OS 接口访问底层。网络 ABI 桥及资源下载通过网络端口访问服务。

这一步没有把内核、WAMR、全部安装事务和协议工作线程合并成一个实现。它们仍是后端实现细节，部分文件仍保留在旧目录；接口隔离与源码物理搬迁是不同的工作。首次开机协调器继续使用现有系统适配和 PSA，桌面后端接入时应提供对应服务实现，不能仅返回成功来假装完成下载或安装。

## 新设备适配步骤

1. 同芯片、同系统先复用已有后端，增加板型资料、DTS/Kconfig 或 IDF 引脚和驱动配置。
2. 通过覆盖表实现有差异的显示、触摸、音频、背光等方法，保留坐标、像素格式和输入槽位约定；未配备的外设明确报告不可用。
3. 新增系统才需要实现 OS 和网络端口。SD 挂载、驱动初始化、中断和电源时序属于板级启动代码，在公共服务启动前完成。
4. 提供实际屏幕尺寸、内存、CPU/AOT 目标和能力；APP 仍按 CPU 架构构建，新增接口不会让 Xtensa 与 RISC-V AOT 互通。
5. 运行端口测试、目标构建，再实测字体、Launcher、下拉面板、商城下载安装和 APP 返回。

## 验证入口

`aster port selftest` 显式检查外部/内部内存、时钟、递归互斥锁、信号量、任务启动和等待、设备资料、显示查询、SD 文件读写/同步/改名/目录/容量。仅创建并删除自己生成的唯一测试文件，不格式化卡，不访问已安装 APP 的资源和存档。先停止活动 APP，再执行。

主机测试 `test_device_port.py`、`test_port_modules.py` 在无 Zephyr/IDF 头文件的环境中编译真实接口层，验证参数传递、继承覆盖、无效版本、缺失能力、冻结绑定和公共 UI 的依赖边界。原有协议、安装故障矩阵、字体和 UI 测试继续运行。

2026-11-14 验证记录：P4/IDF、lckfb/Zephyr 均已构建并烧录；`aster port selftest` 均通过 0xff，OBS 商城分别成功加载 7/5 个应用目录。用户确认返回桌面、上下滑动、下拉设置、打开商城的画面、文字与触摸正常。主机回归 153 项通过，3 项按环境跳过。桌面模拟器的适配实现保存在独立的 aster-sim 仓库，不加入实机固件构建。
