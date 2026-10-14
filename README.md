# GBA

面向 MCU 的原创 C11 GBA 模拟器。工程沿用 NES 的模块划分、命名、移植函数和整屏/半屏绘制方式，静态资源与流式 ROM 接口参考 GBC。源文件使用 Apache 2.0。未引入其他模拟器核心，也未包含任天堂 BIOS 或商业游戏。

**当前采用用户选择的单一快速核心和扫描线精度。** ARM/Thumb、显示、音频、卡带、HLE 和存储已有实现与回归；已分析的硅片时序差异保留在精度允许记录中。速度、资源和精度边界见 [快速核心](docs/fast-core.md)，游戏实测见 [验证记录](docs/validation.md)。不据此声称任意游戏兼容或 MCU 实时运行。

## 工程

| 路径 | 内容 |
| --- | --- |
| `inc/` | 公共实例、内存绑定、配置、平台函数声明 |
| `src/` | ARM7TDMI、总线、DMA/定时器/IRQ、PPU、APU、卡带、RTC、BIOS HLE、存储 |
| `port/` | 通用配置、默认 libc/文件适配、无窗口输出端口 |
| `sdl/sdl2/`、`sdl/sdl3/` | 桌面端口的独立 xmake 工程，共用 `sdl/common/main.c` |
| `rtthread/` | Kconfig、SConscript、平台接口模板和分区静态内存示例 |
| `test/` | 合成回归、故障注入、固定版本参考测试、无堆链接检查 |
| `bench/` | 宏隔离的模块计时与 ROM 缓存计数 |
| `docs/` | 移植、存储格式、验证边界和后续正确性工作 |

## 构建与验证

在工程根目录，用已安装的 xmake 和 MSVC：

```powershell
xmake f -m release -y
xmake build
xmake run gba-tests-full
./test/run.ps1
./test/run.ps1 -TraceSuite -StrictHardware # 记录具名失败，当前严格验收未通过
./test/run.ps1 -SkipFetch -FastAcceptance # 功能回归及单一快速核心精度契约
./test/video.ps1 -Strict                 # 固定视频场景逐像素比较，当前 6/7
./test/sdl_roms.ps1 -ROMDirectory 'E:/Game/GBA汉化全集' # 已构建 SDL2 测试端口后运行
./test/sdl_roms.ps1 -Deep                 # 固定输入、至少 3600 帧、恢复及五次重载
./test/cross.ps1 -Toolchain 'F:/Arm GNU Toolchain arm-none-eabi/14.3 rel1/bin'
```

`test/run.ps1` 下载并验证固定版本的测试资源，在 `test/out/` 生成 JSON 结果、PPM 图片和日志。默认退出状态检查本工程回归、常驻/流式一致性及跳帧状态一致性，同时明确输出 mGBA 严格测试失败；`-StrictHardware` 会对这些硬件差异返回失败。`-SkipFetch`、`-SkipSuite` 和 `-SkipVideo` 可用于离线的局部回归。交叉链接产物是接口检查 ELF，未提供任何开发板的启动代码或固件镜像。

桌面端口：

```powershell
cd sdl/sdl2                # 或 sdl/sdl3
xmake f -P . -m release -y
xmake build -P .
xmake run -P . gba 'F:/games/demo.gba'
# 可选外部 BIOS；必须精确为 16384 字节
xmake run -P . gba 'F:/games/demo.gba' 'F:/games/gba_bios.bin'
```

方向键移动，Z/X 为 A/B，A/S 为 L/R，Enter/Backspace 为 Start/Select。F5 保存即时存档，F9 恢复，F6 保存电池存档，Esc 正常退出。保存失败后窗口保留实例，可重试；Ctrl+Q 丢弃当前实例，F10 丢弃当前状态并重新加载游戏。`--frames N` 用于有界启动验证，出错时返回非零。

## 嵌入式使用

默认 RGB565、整屏输出、关闭声音输出、文件系统和即时存档；保留 PSG/FIFO 的硬件行为、HLE、卡带存档与 RTC。`GBA_RAM_LACK=1` 使用 240×80 缓冲，依次提交上下两块。缓冲在回调返回后即可被复用，DMA 显示和音频队列必须先复制或等待消费。

`gba_init_static()` 借用调用方硬件内存，不分配堆；`gba_load_rom()` 借用只读 ROM。`gba_load_stream()` 使用按偏移读取的回调，默认 4×8 KiB 缓存，短读使实例停机并保留错误。动态 `gba_init()` 复用同一条绑定路径。资源最低约 424 KiB（关闭存档、半屏），默认约 589 KiB 加实例；小内存 MCU 需选择外部 RAM、裁剪配置或限制 `GBA_SAVE_CAPACITY`，不能依靠 ROM 流式加载消除 GBA 硬件 RAM 的需求。

详细契约和配置见 [移植说明](docs/porting.md)，文件和恢复语义见 [存储说明](docs/storage.md)。[English README](README_EN.md)。

SDL2 已用系统显示/音频驱动对八款本地游戏完成 3600–18000 帧有限场景、120 帧恢复重放及五次电池内容重载，宝可梦和逆转裁判另通过菜单保存。黄金太阳开场剧情中的保存仍未覆盖，见 [SDL2 验证](docs/sdl-validation.md)。测试输出保存在忽略目录，未修改原始游戏或存档。
