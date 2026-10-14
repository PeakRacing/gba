# 验证记录 / Validation record

日期：2026-10-14。主机 Windows x64、AMD Ryzen 9 5900X，xmake 3.1.1，MSVC（Visual Studio 2026），release/C11。交叉编译器 Arm GNU Toolchain 14.3.rel1（GCC 14.3.1），Cortex-M4 Thumb、`-Os -Wall -Wextra -Werror -fno-builtin -fstack-usage`。这份记录区分功能回归、硅片时序比较和桌面/交叉编译。用户已选择单一快速核心及扫描线精度；严格硅片比较仍未通过，快速精度门禁按逐项允许列表验收，见 [快速核心记录](fast-core.md)。

机器可读证据：[主机结果](validation-results.json)、[ARM 结果](arm-results.json)、[失败用例汇总](suite-failures.json)。重新运行脚本会在 `test/out/` 产生新记录；此处 JSON 是本次源码检查后的快照，不是未来变更的自动证明。

## 本地回归与配置矩阵

| 配置 | 断言 | 失败 | x64 实例 | 硬件资源 |
| --- | ---: | ---: | ---: | ---: |
| default：RGB565/整屏、声音与 FS 关闭 | 971 | 0 | 6216 B | 603136 B |
| full：32 位/整屏、声音/FS/状态开启 | 1102 | 0 | 7520 B | 679936 B |
| stream：RGB565/半屏、声音/FS/状态/流式开启 | 1116 | 0 | 7520 B | 597504 B |
| minimal：RGB565/半屏、存档/RTC/HLE 关闭 | 683 | 0 | 6216 B | 433664 B |
| skip：RGB565/整屏、声音/FS、交换字节/跳 2 帧 | 1029 | 0 | 7520 B | 603136 B |
| small-save：RGB565/整屏、存档容量限制 512 B | 13 | 0 | 6216 B | 472576 B |

覆盖 ARM/Thumb 的指令、标志、移位边界、银行寄存器/异常返回、总线镜像/宽度/非对齐、BIOS 读取保护、DMA/级联定时器/IRQ 确认、模式 0–5/OBJ/窗口/混合/GreenSwap、PSG/FIFO、HLE 运算/解压/仿射/SDK 声音驱动、Flash/EEPROM/RTC、静态重叠拒绝、动态分配回收、流式跨页与短读。存储覆盖短写、sync/close/replace 失败、损坏文件、恢复第二遍失败、非零 PCM 的重放一致、闰日与 RTC 离线不补时。

这些是有限合成测试，不代表 ARM 全部指令组合、所有图形场景或长时间任意游戏均正确。不同颜色深度比较先转换为相同 RGB555，避免 RGB565 的绿色精度造成伪差异。

新增 ARM/Thumb 定时器测量回归覆盖八种 WAITCNT 配置下的校准、NOP 和半字加载，共 48 个测量；恢复包含未消耗预取字和部分读取相位的状态后重放一致。新增回归还覆盖独立 DMA 锁存、ROM 固定/递减控制、CPU 总线接管和高优先级抢占后的非顺序周期。512 B 存档配置拒绝超限设备及 EEPROM 自动扩展，更大的后备缓冲描述不会改变配置上限。即时存档应用失败和已消费的流式句柄关闭失败都不能通过普通重置绕过；存档格式升级为 GAST v7，原始电池/RTC 格式不变。

RTC GPIO 回归通过真实卡带地址 `0x080000c4/c6/c8` 发送命令，覆盖两种命令位序、12/24 小时下全部 24 个小时、PM 位、闰日、非法 BCD/日期/星期的整笔拒绝、Power-Off 读清除、Force Reset、Force IRQ/STOP 唤醒、未使用寄存器 FFh、非法命令和 CS 内重复时钟、tick 回绕与世纪进位。状态回归恢复半笔写入及读到第 4 位的事务，续传结果一致；CRC 正确但 RTC 控制、位数、输出或字节超限的状态在应用前拒绝。音频块编码保留已有格式错误，不再把它覆盖为 I/O 错误。STOP 下已有就绪 DMA 也不推进模拟周期，唤醒后继续传输；Force IRQ 在 IE 未启用 Game Pak 中断时只设置 IF，不解除 STOP。周期 IRQ 的脉冲/保持行为与真实芯片仍未验收。

总线接管实验曾将 timing 提高至 1820，但使两个跨区 LDM 用例出现新的差异，因此未保留该实现。此记录的固定套件结果仍是 1814/2020，与上一基线一致；后续需要区分普通 CPU 访问、LDM 跨区和 DMA 接管时的在途预取，不能在所有接管处直接加等待周期。

运行中定时器的 reload 写入增加一个时钟的提交事件；该事件与溢出重合时，溢出先使用旧 reload，再提交 I/O 中的新值。依据固定 timer-IRQ 的 FFFF 连续四项计数差异建立此模型，并以本地四路合成测试覆盖溢出边界、32 位同时写 reload/start 和 pending reload 保存恢复。timer IRQ 从 65/90 提升到 69/90，其他固定分项一致；仍需真实硬件和不同 prescaler/级联/写入组合验收，不能把有限结果视为完整定时器一致性。每路增加 u32 提交延迟，实例增长 16 B；事件调度仍跳到最近事件，没有逐周期轮询。
## 固定 jsmolka/gba-tests

版本：[a7113b67e63f83a9b321696ddd7042ccfad6c881](https://github.com/jsmolka/gba-tests/tree/a7113b67e63f83a9b321696ddd7042ccfad6c881)。使用该提交提供的 13 个 ROM，每个运行 5 个模拟帧，常驻 full、流式 half16、skip2 分别运行。

| ROM | 判定 |
| --- | --- |
| arm、thumb、bios、memory、unsafe | 测试完成后的 r12 错误计数为 0，PASS |
| save-flash128、save-flash64、save-none、save-sram | r12 错误计数为 0，PASS |
| ppu-hello、ppu-shades、ppu-stripes | 运行/图像提交 SMOKE；没有硬件参考像素自动验收 |
| nes | 运行到 VRAM 代码且未故障，SMOKE；未作为 NES 游戏兼容验收 |

13/13 常驻/流式：CPU、周期、规范化硬件状态 CRC、图像 hash、PCM hash、步数一致。13/13 跳帧：CPU、周期、硬件状态 CRC、PCM hash、步数一致；图像提交次数/画面 hash 有意不相同。半屏是两次回调，整屏一次，不能直接比较 draw 次数。硬件 CRC 比较排除配置性的 frame_skip 计数。电池文件由各次运行的独立输出目录隔离；旧文件在测试前删除，不触碰用户 ROM/存档。

## 固定 mGBA suite

版本：[e6942030d25ffe3ba76c72b73a86da073ec857cc](https://github.com/mgba-emu/suite/tree/e6942030d25ffe3ba76c72b73a86da073ec857cc)。该仓库没有此处直接可用的预编译 ROM，使用 `test/build_suite.ps1` 构建无窗口适配 ROM。

| 分项 | 通过 / 总数 | 状态 |
| --- | ---: | --- |
| shifter | 140 / 140 | PASS |
| carry | 93 / 93 | PASS |
| multiply-long | 52 / 72 | FAIL：20 个未定义 C 行为差异 |
| BIOS math | 615 / 615 | PASS |
| memory | 1552 / 1552 | PASS |
| I/O read | 130 / 130 | PASS |
| timing | 1814 / 2020 | FAIL |
| timers | 505 / 936 | FAIL |
| timer IRQ | 69 / 90 | FAIL |
| DMA | 1244 / 1244 | PASS |
| misc edge | 5 / 12 | FAIL |

11 个分项执行结束，完成标记 `0x53554954`，323 帧、90927277 模拟周期，无核心 fault。**执行结束与无 fault 不表示测试通过。** 结果是 HLE 配置，不能替代外部 BIOS 验收。

核心修复包括每个 DMA 通道的独立锁存、无效源的锁存半字选择、ROM 源强制递增、启动事件和 CPU 总线边界仲裁；高优先级通道可以在传输单元之间抢占，恢复的低优先级 ROM 通道按非顺序周期重新访问。SRAM 宽读取保留字节地址，未映射区域按宽度截取；CpuSet 保留未对齐寻址并拒绝 BIOS 源区域，等待类 SWI 完成后恢复 BIOS 读取保护值。WAITCNT 三个 ROM 区默认顺序访问周期为 3/5/9，另有专门回归。

内存与 DMA 提升也包含测试适配修正：补回上游 main 的 32 B VRAM 初始数据和 SRAM 文本前缀，恢复 ActiveInfo 的 IWRAM 放置及初始化，日志从覆盖式环改回有界线性写入。上轮错误适配会把用于 SRAM 测试的字节改掉，或让 DMA0 从未经初始化的镜像区域读到 0。没有修改任何测试预期或 run/汇编序列。当前 memory 与 DMA 分项全部通过。进一步修正数据访问后的取指非顺序状态、预取关闭时内部周期触发的首访行为，以及预取仅在 Game Pak 总线空闲时推进；部分读取扣除已消耗相位，WAITCNT 变更清空预取。DMA 涉及内部内存时在结束前保留两周期总线释放，IRQ 异常进入避免重复计费。timing 从 505 提升到 1814，旧测试布局下 timers 曾从 467 提升到 481，misc-edge 曾从 4 提升到 5、新通过 Flip 2，HBlank 的 1 周期差异仍在。没有修改上游预期。

新增回归比较 ARM/Thumb 两种来源的 HLE 与自主合成外部 BIOS 中断分派：入口 r0=`0x04000000`、LR=`0x138`、IRQ 栈内容、用户寄存器/CPSR/PC 恢复及模拟周期一致。合成 BIOS 只包含文档定义的分派代码，不是完整 BIOS。另测试了 HALT 在 IME 关闭时的请求唤醒、重复 IRQ、定时器全局分频边界、保存启动/IRQ 剩余周期及旧状态版本拒绝。

进一步核对 devkitARM 默认链接脚本后，修正 headless `.data/.bss` 的内存放置：普通全局变量在 IWRAM，显式 `.ewram` 数据在 EWRAM；启动代码分别复制初始化区并清零 BSS。`IntrTable` 和 `irqCounter` 现在使用 IWRAM 总线，构建脚本检查这些符号和数据边界都位于 `0x03000000..0x03006fff`，防止再次漂移。依据为 devkitPro/devkitarm-crtls 提交 `1c0c10257c44bbb5a433453bb6bba91582825492` 的 [gba_cart.ld](https://github.com/devkitPro/devkitarm-crtls/blob/1c0c10257c44bbb5a433453bb6bba91582825492/gba_cart.ld)。核心没有因适配修正改变内存等待周期。

修正布局后、修改核心前：timers 439/936，misc-edge 4/12；旧布局的部分通过计数不再作为当前证据。随后增加 IRQ 在取指完成边界的采样：请求延迟在取指期间归零时丢弃当前指令，保存当前指令的返回地址并进入 IRQ；本次取指计费一次，向量补充取指在下一步计费。新增 ARM/Thumb、HLE/合成外部 BIOS 回归检查被中断的 ADD 未执行、LR/SPSR/PC 和单周期。当前 timers 505/936，其他当前布局分项保持一致。随后修复 DMA 开放总线锁存和硬件事件触发的三周期启动延迟，Read 已通过；Break 仍失败，期望 `0x10002a94`、实际 `0x10002900`。新增 ARM/Thumb 回归证明锁存在下一指令数据访问中有效、随后失效，且即时恢复保留该有效期。
适配边界：使用固定 suite 的原始 run 函数、预期数据与汇编序列；生成的测试 C 副本只去除 list/show UI 回调引用，自有启动/链接脚本和 headless 主函数在 IWRAM/EWRAM 运行原序列，采用上游 O3/fomit-frame-pointer/ffast-math 编译选项。ROM 辅助库 libgba 固定 [f0e44ce9ed57e1d21f148443223408fddeacfb9d](https://github.com/devkitPro/libgba/tree/f0e44ce9ed57e1d21f148443223408fddeacfb9d)，仅用于测试 ROM，不进入模拟器核心。工具链是本机 ARM GCC/newlib，未经原项目 devkitARM 完整 UI 构建等价性验收。视频场景另由 test/video.ps1 运行；SIO read/timing、字体/交互 UI 和硬件采集未运行。模拟 SRAM 日志限制 32 KiB，写满停止，不再覆盖开头；逐项计数为主要判定。

`test/run.ps1 -TraceSuite -Toolchain <bin>` 从 ELF 解析日志函数和 ActiveInfo 地址，主机指令探针在调用点直接读后备内存，生成 `test/out/suite/calls.log`；没有模拟总线访问、寄存器写入或额外模拟周期。日志记录 suite/test/subtest、原始格式、r1–r3 和字符串标签。开启/关闭探针运行结果一致。misc-edge 中预期/实际值的传参次序与 memory/DMA 不同，应对照原始测试，不能统一把 r2 当作实际值。该诊断仅用于常驻测试前端，生产不编译。`test/summarize_suite.ps1` 按原始标题汇总失败；本次 72 个用例共 685 个失败比较，与套件分项计数相符。

`test/run.ps1 -StrictHardware` 本次返回非零，原因是这些严格硬件差异。普通 `test/run.ps1` 仅在本地回归/常驻流式/跳帧一致性失败时返回非零，同时会明确 WARNING 严格验收失败。CI 若用于首版硬件门禁必须使用 `-StrictHardware`，不能用普通退出码声称全绿。

## 固定视频比较与 SDL2 游戏验证

`test/video.ps1` 保留固定提交 `src/video.c` 的场景函数、测试数据及预期，另加无 UI 适配器调用 expected/actual 两条路径，各等待五次 VBlank，比较全部 240×160 RGB 像素。退化 OBJ 的原始索引 BMP 由标准库脚本转换为 GBA tiles/调色板和 literal-only LZ77；不依赖 GRIT，不改变原始 palette indices。导航图标与字体由 section GC 排除；原上游 show 设置的 OBJ_ON/OBJ_1D_MAP 已在适配器补齐。视频完成标记与 CPU 套件不同，避免覆盖其 SRAM 日志。

| 场景 | 不同像素 / 38400 | 判定 |
| --- | ---: | --- |
| Basic Mode 3 | 0 | PASS |
| Basic Mode 4 | 0 | PASS |
| Degenerate OBJ transforms | 0 | PASS |
| Layer toggle | 0 | PASS |
| Layer toggle 2 | 958 | FAIL |
| OAM Update Delay | 0 | PASS |
| Window offscreen reset | 0 | PASS |

修复垂直窗口比较锁存后，结束位置 228 在 0–227 行间不会清除窗口，而位置 227 会在消隐期清除。背景启用保留三次扫描线边界延迟；OAM 在 HBlank 锁存供下一行使用，即使跳帧也推进这些状态。新状态进入 GAST v7，旧版本 1–6 明确拒绝；CRC 正确但锁存/延迟字段超限的文件在应用前拒绝。实例增加 1056 B，主要为 OAM 快照；硬件资源缓冲大小不变。Layer toggle 2 仍差行 65/132/141/146，未忽略边缘像素或修改预期。

八款用户授权的本地商业 ROM 使用 SDL2 系统 video/audio 驱动做启动及深入检查，详见 [SDL2 记录](sdl-validation.md) 和 [机器结果](sdl-results.json)。每款结束于 600 模拟帧，第 240 帧保存、第 360 帧恢复，因此实际生成 720 帧；恢复后的 120 帧 CPU/硬件/RAM/画面/PCM 与首次运行相同，队列 empty/overrun 计数均为 0。仅用于启动/菜单冒烟，不能替代完整关卡、游戏内电池存档或音频波形验收。当前按用户要求不使用真机。

## Cortex-M4 链接及栈

`test/cross.ps1` 编译所有核心文件，在静态无堆示例中保留对应配置路径，`-nostdlib` 链接 `libgcc` 和少量自有端口。没有 libc malloc/calloc/realloc/free，也没有关闭配置的指令/性能探针。动态 API 在 MSVC 回归中验证；无堆 ELF 经 section GC 排除未使用动态入口。

| 配置 | text | data | bss |
| --- | ---: | ---: | ---: |
| default | 26904 B | 0 | 609260 B |
| minimal | 19232 B | 0 | 439788 B |
| full32 | 32476 B | 0 | 687356 B |
| stream/half16 | 32796 B | 0 | 604924 B |
| stream/swap/skip2 | 27228 B | 0 | 642028 B |
| small-save / 512 B | 26868 B | 0 | 478700 B |

BSS 含 ARM ABI 的实例和示例资源，不含线程/调用栈；text 为示例可达核心和端口的链接结果。最新精确 text 保存在 JSON，避免将不同编译版本的大小混用。GCC `.su` 最大单函数栈：开启状态的 `gba_state_load_file` 2856 B，其余配置 `gba_ppu_render` 656 B。单函数栈不能当作调用链总量或 ISR 嵌套高水位；板端需测量完整线程栈并留余量。这是接口 ELF，没有向量表、板级启动，也未在 Cortex-M4 执行。

RT-Thread 接口编译另用实际本地头文件（提交 `e7f6a5d80ffe6e51ffea52b9257e8ff6272abdbe`），核心、端口、静态资源示例在启用/关闭 RTOS 堆两种条件下通过 ARM GCC `-Werror`。仅 default 半屏配置；没有链接 RTOS/BSP 或运行任务/驱动。公共 C++ 声明另通过 ARM G++ C++17 头文件编译检查。

## SDL 与主机基准

SDL2/SDL3 均用 MSVC release 编译生产与测试前端。dummy video/audio 下，以中文 UTF-8 路径加载 ARM 测试 ROM；第 30 帧保存、第 60 帧恢复，结束于第 120 帧，恢复后的 30 帧逐帧重放相同，二者 `test save=0`、`test load=0`、退出 0、队列 underrun/overrun 计数 0。dummy 驱动不提供真实声音/显示，因此不证明物理音频连续性、按键/窗口交互或屏幕观感。

600 帧 ARM 合成 ROM 的主机 profile 基准见主机 JSON：实例 7544 B，资源 635904 B，25278791 次缓存命中、2 次缺页，无 fault。CPU 时间包含在取指/访问内部触发的设备处理，模块统计有包含关系，不能相加；计时粒度和探针开销也会影响结果。该 ROM 主要验证执行，不能代表商用游戏负载或 MCU 性能。板端 FPS、音频 underrun、内存/栈高水位、保存恢复、退出重入和控件都待测。

## 依据与许可

实现行为参考 [Arm ARM7TDMI 技术手册](https://documentation-service.arm.com/static/5f4786a179ff4c392c0ff819) 和 [GBATEK](https://mgba-emu.github.io/gbatek/)。本轮 IRQ 请求的 7 周期模型和全局分频对齐还参照了 mGBA 提交 `143adf5aa2a60c6036810b0cd72445e13d74b0e6` 的 [IRQ 硬件模型](https://github.com/mgba-emu/mgba/blob/143adf5aa2a60c6036810b0cd72445e13d74b0e6/src/gba/gba.c) 与 [定时器硬件模型](https://github.com/mgba-emu/mgba/blob/143adf5aa2a60c6036810b0cd72445e13d74b0e6/src/gba/timer.c)，以本工程标量事件状态独立实现；该参考不是板端实测。原始核心为自主 C11 实现，Apache 2.0；固定测试及 libgba 仍受各自仓库许可约束，脚本下载到忽略目录，未将其他模拟器核心复制进 `src/`。工程未附带任天堂 BIOS/商业游戏，也没有修改 NES、GBC 或掌机固件，没有提交、推送或烧录。

English: six local host configurations pass their finite regression tests; all thirteen reference ROMs match resident/streaming and skipped-frame hardware state. Nine ROMs report zero errors, four are smoke tests only. The eleven adapted mGBA suites complete, but strict hardware validation fails, especially timer IRQ and timing. Cortex-M4 builds prove freestanding static linking; SDL dummy and RT-Thread header checks prove bounded interface behavior. Six of seven adapted video scenes pass exact pixel comparison, and eight bounded SDL2 game/replay checks pass. External BIOS, complete-game compatibility, hardware audio and MCU performance remain unverified.

本轮补充无探针速度矩阵、64 场景批量/标量画面一致、批量/单步事件一致、2 kHz PCM 方波测量、长度/包络、EEPROM 五次重载、分钟 RTC/STOP、双向跨配置拒绝及 32 个固定种子损坏状态。性能结果和快速精度契约见 [fast-core.md](fast-core.md) 与 [precision-policy.md](precision-policy.md)。
