# ARM32 动态重编译与 NES 风格工程结构

本轮将模块状态和接口从单一大头文件拆分，并加入独立编写的 ARMv5TE/A32
机器码生成器。保留扫描线画面、原总线和设备调度；编译器不是另一个硬件模型。

## 工程与配置

`gba.h` 聚合 CPU、总线、PPU、APU、卡带、BIOS、定时器及状态模块头文件，
继续提供原有生命周期、ROM 和运行 API。模块通过 `gba_t` 前置声明引用实例；
私有内联工具按模块放在 `src/`，共享头只保留字节、整数和故障工具。
所有头文件可单独用于 C11/C++11。编译器适配统一使用 `GBA_INLINE`、
`GBA_FORCE_INLINE`、`GBA_ALIGN(n)` 和 `GBA_WEAK`；普通函数不强制展开。
日志级别默认 `GBA_LOG_LEVEL_NONE`，关闭的日志不计算参数。

配置按“编译定义 → 平台配置默认 → 核心补齐”生效。SDL2/SDL3 分别有
`gba_conf.h`；全部翻译单元必须使用同一组宏。`GBA_FAST_CORE=1` 选择既有
便携执行、等待证明和画面缓存，默认 0。纯核心选项重命名：

| 旧名称（兼容别名） | 核心名称 |
|---|---|
| GBA_PORT_BATCH_EVENTS | GBA_BATCH_EVENTS |
| GBA_PORT_FAST_RUN | GBA_FAST_RUN |
| GBA_PORT_FAST_IO | GBA_FAST_IO |
| GBA_PORT_IDLE_LOOPS | GBA_IDLE_LOOPS |

同时定义新旧宏且值不同将拒绝编译。原生 IO、旧等待实验仍默认关闭。
现有 xmake `--fast_core=y` 映射至统一宏。硬件状态字段顺序和 GAST v7 未改变。

## 启用 ARM32 后端

设置 `GBA_DYNAREC=1`，自动开启 CPU/事件执行基础并移除旧的
`GBA_FAST_BLOCKS` 缓存。第一版仅支持小端 A32，最低目标 ARMv5TE/ARM926；
Cortex-M、AArch64、Windows x64 不支持开启该后端。当前验收构建使用 `-marm`；
Thumb 宿主编译尚未验收。辅助函数调用通过 BLX 处理互操作，使用 Thumb 编译器时
仍需将 CP15 发布函数放在 A32 端口中。

```c
/* 编译全部核心和端口时使用同一 gba_conf.h：
 * #define GBA_DYNAREC (1)
 */
GBA_ALIGN(32) static uint8_t code[GBA_DYNAREC_CODE_SIZE];
GBA_ALIGN(32) static uint8_t metadata[GBA_DYNAREC_DATA_SIZE];

/* gba_init_static() 成功后、首次运行之前绑定。 */
gba_dynarec_memory_t native = {code, sizeof(code), metadata, sizeof(metadata)};
int result = gba_dynarec_bind(gba, &native);
```

默认机器码 256 KiB、元数据 64 KiB，包含页代次、两路缓存索引和统计；
缓存从调用方借用，不进入 `gba_memory_size()`，运行中不分配堆。
动态初始化可在现有 `gba_initex()` 钩子绑定平台资源，退出时在 `gba_deinitex()`
释放平台所有的资源。关闭宏时实例不保留绑定字段，编译器代码完全移除。
绑定检查对齐、长度、溢出及与实例、硬件内存和已绑定 ROM/BIOS 的重叠，
并调用端口验证代码内存可写和可发布；失败不替换原绑定。

平台必须实现 `gba_code_begin_write()` 和 `gba_code_publish()`，后者在代码完成后
维护 D/I Cache、写缓冲和执行权限。未实现端口返回 UNSUPPORTED，不使用
空操作伪装成功。`port/gba_arm926_cache.h` 提供特权 ARM926 的 CP15 维护示例；
只有明确保证 RAM 可执行时才能设置 `GBA_ARM926_EXECUTABLE_RAM=1`。
带 MPU/MMU、用户态或 W^X 权限的端口应实现各自映射切换，不直接套用该示例。

## 执行与有效性

ARM/Thumb 运算、比较、移位、乘法和分支生成 A32 指令。编译时统计整个混合块，选择四个驻留客体寄存器（允许非连续及 SP/LR），连续窗口仍用 LDM/STM 装卸。RAM 块最多 64 条并分别验证两个页的代次，末尾流水线取指页也受写保护；ROM 块最多 32 条，带普通访存时最多 16 条。条件不满足时可继续块内执行，满足时提交前缀退出；IWRAM 回跳可在事件边界内重复多轮，总执行计数保持在 255 条以内。

共享原生访存桩覆盖 RAM、常驻 ROM 和有明确事件依赖的 PPU/按键状态读取。支持条件访存、写回、寄存器偏移、Thumb 栈/块传输、有符号读取和奇地址 LDRSH。保护失败时在修改目标指令状态前返回前缀，由 C 路径执行。PC 相对 ROM 常量读取当前宿主内存，不把常量值固化在机器码中。RAM LDM/STM 先检查完整范围及代码页；SWI、模式、SPSR 和未覆盖的设备语义保留 C 执行。流式 ROM 不保留缓存槽指针，ROM 指令仍走共享取指/解释路径，RAM 代码可翻译。

IWRAM 完整块不能容纳于剩余预算时，可尝试附属的事件受限版本；每条指令副作用前检查实际前缀周期与下一条的保守上界。它不占独立缓存条目，空间不足或预算更小时退回 C；不能声称所有期限都能执行原生前缀。按块计算实际取指、WAITCNT/FIFO、访存和内部周期。CPU 可以跨 PCM 采样点，设备回放仍逐个采样；定时器、PPU、APU 序列器及 IRQ/DMA 截止点继续约束执行。默认仍按块返回 C；可选的跨块候选见下文，尚不是 gpSP 式完整引擎。

`gba_step()` 始终单步解释；`gba_run_cycles()`/`gba_run_frame()` 使用重编译。
TEST_MODE 保留逐条 C 跟踪；真实生成代码另由非跟踪验证覆盖，不能以单步跟踪证明原生执行。
`GBA_DYNAREC_STATS=1` 时统计分别记录 `native_instructions`、`memory_instructions`、`fallback_instructions`，
以及编译块数、缓存命中、回收次数、当前代码占用。统计默认关闭，关闭后一般只返回
当前代码占用；可选 DIRECT 候选另保留一个实际跳转诊断计数，见下文；正式速度数据必须同时关闭此统计和 TEST_MODE/PROFILE。
QEMU 验证脚本默认打开统计，并另用 `--no-stats` 验证生产路径。

代码块只在离开生成代码后编译和回收，先完成缓存发布再设置有效标签。
RAM CPU/DMA 写入、HLE 直接清零、BIOS IRQ 标志修改按物理页推进代次；
镜像共享页标识，回绕使全部标签失效。原生写入先检查 32 字节代码行（包含流水线前取），命中时退回共享总线，故自修改不会继续执行失效后缀。工作数据写入后逐字节比较未改变的代码，允许安全复用翻译。
复位、重新加载和成功恢复状态全部失效。外部直接修改借用 RAM 后应调用：

```c
gba_dynarec_invalidate(gba, guest_address, length);
/* 整体失效： */
gba_dynarec_invalidate(gba, 0, SIZE_MAX);
```

机器码、宿主指针、页代次和统计均不写入 GAST，恢复后重建；解释器和
重编译相同硬件配置的 GAST v7 可交换。流式读取失败不生成半成品块，
执行内存发布失败保留故障，必须修复平台后重新加载。

## 可选跨块候选（默认关闭）

`GBA_DYNAREC_CHAIN=1` 为 IWRAM 块保留一次 AAPCS 调用现场。同一客体
寄存器映射不重复装卸，映射变化时交换四个值；精确周期与流水线仍由 C
收尾。`GBA_DYNAREC_DIRECT=1` 依赖 CHAIN，可在完整 IWRAM 块后直接检查
缓存后继的 PC、宽度、epoch、两个源页世代、实时前取字节与剩余预算，
在原生代码中提交真实周期、流水线和总线状态并跳到后继；映射变化也由
原生代码处理。保护失败、部分执行或缺少后继时回到原 C 路径。ROM 代码
不使用此直接后继路径。执行内存不足 32 KiB 时保留原非跨块路径。

DIRECT 原型只在 `GBA_DYNAREC_STATS=0` 时执行，开启统计时使用 CHAIN
收尾，以免把不完整的计数当成完整覆盖率。DIRECT 仍增加独立诊断计数
`direct_links`，即使普通统计关闭也会保留；它与 CHAIN 默认都为 0，尚未
作为关闭全部探针的生产性能交付。使用 QEMU 的 `--chain --direct --no-stats`
验证真正执行该路径，测试会要求实际跳转计数非零，并对照完整机器状态。

ROM 内的 MUL 和寄存器移位通过逐指令内部周期标记参与 FIFO 回放，不再
一律退出编译块；条件不成立时标记为零。寄存器移位后写 PC 同样计算内部
周期，包括条件满足时的提前退出。机器码常量优先用 MOV/MVN 或 MOV+ORR，
可回填地址仍保留独立字面量。所有路径仍保持原周期模型，不虚增客体周期。

## 验证命令与限制

```powershell
python test/header_check.py
pwsh -File test/run.ps1 -SkipFetch -FastAcceptance
pwsh -File test/run.ps1 -SkipFetch -FastAcceptance -FastCore
python test/dynarec.py --references --instruction-compare --label instructions
python test/dynarec.py --references --stream --fast-core --label stream
python test/dynarec.py --references --rom-directory test/out/speed-roms --frames 900 --fast-core --label games
pwsh -File test/cross.ps1
```

默认工具路径可通过脚本参数覆盖。QEMU 裸机端口使用 ARM926 CP15 真正发布并
执行代码，比较 CPU、寄存器分组、硬件事件、RAM、framebuffer 和 PCM；
测试数据只存在忽略目录。商业 ROM 来自用户本地副本，未随源码分发。
文件端口用独立内存文件验证 GAST、短写、替换失败、损坏文件和重试。

**ARM32 全速目标尚未通过。** QEMU 只用于功能对照；Windows SDL2 使用解释器。F1C 项目已经进行 RAM 短窗口筛选，一个 Metroid 启动场景约 59.3 FPS，其他代表游戏仍远低于实时要求。这不是全游戏、实际游戏场景或三轮 60 秒验收。`F:/code/codeup/f1c_rtthread/docs/gba_gpsp_comparison_20261016.md` 和对应目录保存精确镜像、板测及恢复证据。生产测量必须关闭统计和 PROFILE；功能、性能、RAM 启动和 NOR 更新分别报告。未更新 NOR 应用和资源分区。

设计参考本地 notaz/gpsp `862e940b8c108fb0d29f9a1644be7a5360d7230d` 的
翻译缓存/代码失效思路；未复制 GPL 源码、宏、汇编桩或游戏地址表。

## 可选实验开关（默认全部为 0）

- `GBA_DYNAREC_SOFT_TIMERS`：CPU 可跨过不可观察的音频计时器溢出，设备仍逐事件回放；IRQ、级联和首次 FIFO DMA 请求保持硬边界。
- `GBA_DYNAREC_INTERIOR`：复用只读 ROM 块内部入口。板测早期有明显退步，未作为生产选择。
- `GBA_DYNAREC_NEGATIVE`：缓存精确头两条指令的译码拒绝；动态总线和预算失败不缓存。
- `GBA_DYNAREC_MULTI_INLINE`：RAM LDM/POP 的普通加载直接展开；整体范围验证失败、PC 和设备读取保留 C 路径。
- `GBA_DYNAREC_ROM_PREFIX`：为事件预算不足的 ROM 整块保留附属短前缀，沿用精确 FIFO/内部周期回放。
- `GBA_PPU_ORDERED`：没有半透明混合时按层顺序直接覆盖；透明场景回到原合成器。
- `GBA_PPU_READ_DEDUP`：每脏行分别去重连续相同的 VRAM/调色板依赖块。

对应 `test/dynarec.py` 参数为 `--soft-timers --interior --negative-cache
--inline-multiple --rom-prefix --ordered-ppu --ppu-read-dedup`。开启新绘图测试时，
参考实例强制走旧合成器，避免两边同错。RGB565/32 位的缓存与绘图交叉组合
另由 F1C 主机像素测试覆盖。所有开关由可移植 C11 核心提供，平台只负责
可执行 RAM、cache 发布和输入输出；没有加入操作系统或图形库依赖。

编译与 QEMU 通过不代表性能通过。完整帧率目标尚未达成，实验默认关闭。


`GBA_DYNAREC_RAM_INLINE=1` emits guarded RAM reads selected by observed address
regions; changed effective addresses fall back before mutation.
`GBA_DYNAREC_ROM_CHAIN=1` extends the persistent call frame to immutable-ROM
successors; it requires CHAIN and excludes INTERIOR. It saves all pre-entry FIFO
state before branches can reset it. Neither option is a full gpSP-style native
ROM dispatcher; the shared continuation still commits each ROM block through C.
Both remain disabled by default.

`GBA_DYNAREC_EVENT_SLIP` defaults to 0 (exact events). A nonzero bound (up to 2048)
requires non-chained dynarec and permits a bounded block to finish before device
events commit. Instructions and their cycle charges still execute; CPU relative
IRQ/DMA/PPU/audio timing changes. This is an explicit accuracy tradeoff, not an
interpreter-equivalent mode. Test it using `--event-slip 1024 --fixtures-only
--replica-validation`; the report labels two independent engine replicas,
runtime/input/state roundtrip and determinism checks. Do not count those as
hardware accuracy or game compatibility acceptance. Frame APIs can complete an
atomic DMA spanning multiple vblanks; performance uses actual frame counters,
not API invocation counts. Full-speed target acceptance remains open.


## 2026-10-16: 近似时序与 ROM 衔接实验

以下均默认关闭，不能据此宣称精确模式或全部游戏兼容：

- `GBA_DYNAREC_EVENT_SLIP=1..2048`：在块执行边界观察设备事件；不会用空转周期增加模拟帧数。
- `GBA_DYNAREC_FAST_BUS=1`：要求事件放宽，ROM 取指使用 WAITCNT 下的合法单次成本下界，省略 FIFO 回放；每条真实执行的指令仍计取指成本。数据和内部成本保留，预取时序改变。
- `GBA_DYNAREC_WIDE_ROM=1`：要求 FAST_BUS、排除 INTERIOR；同一源页内扩大 ROM 块，并重复带普通 RAM 访问的短循环。重复次数和最坏周期均有限。
- `GBA_DYNAREC_STORE_INLINE=1`：要求 MULTI_INLINE；STM/PUSH 的整段 RAM/对齐/代码页守卫先完成，再执行展开的存储。
- `GBA_DYNAREC_PARTIAL_REGS=1`：单独处理已缓存的 ALU 操作数；精确计算对照通过，但当前掌机对照没有速度收益，保持关闭。
- `GBA_DYNAREC_RECENT_BITS=6..10`：最近入口缓存容量；大于 6 需至少 512 KiB 元数据。DIRECT 要求 6。1 MiB 元数据支持 16384 槽。
- `GBA_DYNAREC_COARSE_CHAIN=1`：要求 FAST_BUS、ROM_CHAIN、DIRECT，排除统计与 ROM_PREFIX。ROM 块保留宿主缓存寄存器直接进入已编译目标；设备回放必须在寄存器写回后。流水线/open bus 元数据在最终出口重建，零指令守卫出口也重建。

`test/dynarec.py --replica-validation --fixtures-only --event-slip 1024` 显式区分近似运行检查与解释器精度对照；不能自动加入严格差异允许清单。`--fast-loop-proof` 覆盖有限循环计算与最低计费检查；COARSE_CHAIN 另带四块计算对照、零前缀守卫和非零真实衔接计数。主机测试采用与平台同一 ARM926 行范围发布/缓存钩子。板端性能与音频仍以独立镜像 SHA/完整 RAM 回读后的真实帧计数为准。

MULTI_PC（默认 0、要求 MULTI_INLINE）在整段 RAM 守卫后展开 ARMv4T LDM/POP PC，保持指令集并对齐 PC；失配原子回退。LONG_LOOPS（默认 0、要求 WIDE_ROM）去除 7 轮额外限制，仍限制最坏周期与实际指令数 255。有限计算测试覆盖最多 127 轮和 255 指令边界，板端收益须单独测量。COARSE_CHAIN 的目前短窗口实测没有收益，保持关闭。

COARSE_CHAIN 现支持正常条件跳转前缀及 IWRAM/ROM 跨区（RAM 保留完整守卫）。LINK_CACHE 默认 0，要求 COARSE_CHAIN，复用未使用的 prefix_offset 缓存已校验后继并可在 C 出口填充；每次仍校验有效性。TINY_BLOCKS 默认 0、要求 COARSE_CHAIN，允许单条原生块。BL_PREFIX 默认 0、要求 DYNAREC，将 Thumb BL 前半条保留在同一编译块，只按 PC 和有符号高偏移写 LR，后半条保持原语义；该改动本身不放宽时序。所有速度声明依赖独立板测。

REFILL_MATCH（默认 0、要求 COARSE_CHAIN）对跨块分支使用真实空流水线双取指费用，方便与非链式执行比较，393216 组总线对照通过。BULK_PREFILL（默认 0、要求 FAST_BUS 且 EVENT_SLIP>=64）直接装入普通 RAM/ROM 流水线，并把装入费用计入同一预算；不允许 GPIO/容量/区域边界越界，保留回调前寄存器写回。LINK_CACHE 开启时超预算 IWRAM 块直接标量回退，禁止 prefix_offset 再被旧短前缀机制使用；有修前失败和修后通过的复现。OBJ_SPANS（默认 0）对普通精灵按图块行段渲染，仿射/马赛克保留通用路径，颜色/对象预算/窗口/混色/缓存对照通过。


2026-10-16 后续实验均默认关闭。LINE_INVALIDATE 对 RAM 写只在命中受保护的
32 字节代码行（含流水线前看）时增加页代数。FIXED_MAP=1 固定 r0..r3，=2
在 ARM 改为 r0/r1/r6/r14。FASTCALL 采用 ARM 内部 caller-clobber 入口，移除
逐块 AAPCS 重复保存，由调用者编译器保存外围寄存器；要求
-fomit-frame-pointer，禁止 CHAIN/INTERIOR/ROM_PREFIX，不屏蔽 IRQ、使用真实线程栈。
ROM_TRACE 要求 FAST_BUS/WIDE_ROM/EVENT_SLIP>=256，排除 INTERIOR/ROM_PREFIX，
在不可变常驻 ROM 跨源页生成至多 120 条指令；检查代码与快照容量、ROM/GPIO、
实际周期和总执行数上限，跨页不融合原生回边。它仍是显式近似时序配置。
SORTED_BLEND 是独立精确渲染实验，按后到前维护顶层和第二层，保留同层对象替换。
这些开关均须独立目标板测量，不能因 QEMU 通过而宣称更快。


2026-10-16：EVENT_SLIP 必须保留 DMA startup 和已启用的 HBlank/VBlank 搬运硬边界，
包括帧末回绕；BULK_PREFILL 同样受限。旧近似候选在《黄金太阳》约 300 帧会提前
读取 DMA 尚未写入的函数指针，不能交付。96 组真实搬运/CPU读取对照及全部扫描线
事件距离检查覆盖该修复；修前冻结源码真实失败、修后通过。

TRACE_LINKS（默认 0、要求 ROM_TRACE、禁止 CHAIN）继续是近似模式实验：将常驻
同一区域 ROM 的确定 B/BL 和完整 Thumb BL 调用连接为一段原生代码，快照旁记录
逐条源 PC 和提交出口。BX LR 只在匹配已连接调用的预期 LR 时继续，否则走普通
BX 提交退出；嵌套预测最多 16 层。真实分支 refill 计入每条连接的上界和消耗，
仍最多 120 条/段并保留 DMA 硬边界。条件边、内存守卫失配可在任意前缀退出；
未连接的连续循环保留原来的有界原生循环。F1C200S筛选中Mario约有5.9%收益，其他场景较小；未完成满帧验收。


TRACE_CONTEXT（默认0，要求TRACE_LINKS）仅对ROM入口的BX按实际目标寄存器值区分
元数据查找和热缓存。生成代码仍完整比较实际目标（含指令集位），失配执行原BX
退出，不消耗预测目标的任何指令。context存在未启用ROM_PREFIX的prefix_offset字段，
窄预算ROM入口直接返回，不经过IWRAM前缀机制；交替目标、LR变化、裸原生入口强制
目标/模式失配和窄预算均单独对照。尚无完整同轮实机速度结论。

FRONT_CULL（默认0）对无需第二层混色的行从前到后绘制对象/背景，已覆盖像素跳过
后层采样。alpha或半透明对象行仍用原合成器；对象预算先独立计算，窗口掩码已解析。
仿射/马赛克也必须保留覆盖检查。RGB565/32位颜色、缓存/对象行段/排序开关对照
像素一致，性能以独立目标板同源开关比较为准。


HARD_CACHE（默认0，要求EVENT_SLIP）把DMA/IRQ硬边界相对已提交设备时钟的距离
保留在私有native缓存中，每次只减cpu_pending。CPU运行入口、任何CPU/device同步、
全量代码失效/复位清有效位，已提交时钟变化也拒绝命中。RAM写和普通native块不改变
DMA配置，不需要逐块重扫四个通道；MMIO写先同步再修改配置，因此即使零周期同步
也必须失效。测试包括真实DMA读旧数据回归和开关两份实例的完整状态/内存/PCM比较。


GBA_PPU_OBJ_INDEX and GBA_PPU_OAM_DIRTY default to zero and are independent
portable C11 experiments (also available without GBA_DYNAREC). OBJ_INDEX
retains 20 eight-line OAM candidate bands, updates them on actual latch changes,
and preserves original ordering and evaluation budgets; initial live OAM uses
the original selector. Its first F1C200S screening was slower, so it stays off.
OAM_DIRTY tracks 256 words in eight bitsets and copies only modified words at
the original latch boundary. Initial/invalidated snapshots copy all OAM. CPU,
DMA and native video writes share the tracker; ignored byte stores stay ignored.
Reset, state restore and external gba_video_invalidate clear the derived cache.
Neither cache is serialized or changes GAST identity. Selection/full latch
differentials, pixel matrices and real ARM execution are separate from target
performance measurements. HARD_CACHE's same-cycle board screening improved
only about 0.7 to 1.9 percent; it also remains off by default.


F1C200S 408 MHz D176/D177 OAM_DIRTY on/off screening (OBJ_INDEX off,
no frameskip, raw 32 kHz stereo, probes off, 12 s warmup + about 30 s):
Mario 24.007/23.524 simulated FPS, Zelda 25.420/24.105, fixed Emerald room
11.873/11.565. The control Zelda presentation window includes one additional
frame (24.138 presented FPS). These small gains do not reach 59.73 FPS or
constitute three-round final acceptance. OAM_DIRTY remains opt-in. Exact ARM
reference, 900-frame game/state replay, 16/32-bit pixel matrices, strict
difference gate and minimal Cortex-M4 coverage passed. Original SPL/full NOR
and menu boot were restored; no NOR application/resources were updated.
Firmware evidence: F:/code/codeup/f1c_rtthread/tools/host_test/results/gba-dynarec-oamdirty-20261016.
