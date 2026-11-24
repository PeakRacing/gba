# 持久原生执行与普通扫描线快速路径

`GBA_DYNAREC_PERSISTENT=1` 是可选 ARMv5TE/A32 执行组织，默认关闭。
代码生成器和核心均使用 C11，不引入 gpSP 源码；gpSP 仅用于执行结构参考。
通用 C 路径、静态内存接口和 GAST v7 保持可用。

## 执行约定

一次 AAPCS 入口保留宿主 r4-r11/LR，保持真实栈及8字节对齐。
r4 指向实例、r5-r10 保留六个 guest 寄存器、r11 保留剩余周期。
ARM 映射 r0/r1/r6/r9/r12/r14，Thumb 映射 r0-r5。ARM/Thumb 桥接先保存完整旧映射再加载新映射。
普通直接边回填到 body 或 refill 入口；64项间接缓存使用完整 PC/模式键。
硬件寄存器、特殊映射及不支持的指令继续使用现有慢路径。

IWRAM 普通执行支持精确配置。ROM 持久执行仅在既有 FAST_BUS 配置下开启，
沿用同样的最低合法取指成本；没有增加 guest 周期或扩大 EVENT_SLIP。
ALU、乘法、寄存器移位、直接/间接分支及普通单次 RAM/ROM 访存可在原生执行中继续。
`GBA_DYNAREC_PERSISTENT_MULTI=1` 可将普通 ARM LDM/STM 和 Thumb PUSH/POP/LDM/STM
留在原生执行中，默认关闭。仅接受完整对齐且落在普通 IWRAM/EWRAM 的范围；
先验证整个范围及代码页保护，再提交任何 guest 写入。特殊 bank、MMIO、跨物理末端和
自修改写均退出慢路径。加载 PC 复用间接桥接；预算与 guest 周期计费保持一致。
其余复杂操作继续使用现有实现。

`GBA_DYNAREC_PERSISTENT_LITERALS=1` 可旁路已知 ROM 字面量地址的动态守卫，默认关闭。
保留实际 ROM 读取、原代码分配布局、预算和预取副作用；GPIO、未对齐和越界仍走慢路径。
间接缓存不仅在新编译时登记，还在 C 边界分派已有块时补入，避免碰撞后永久失去热目标。

`GBA_DYNAREC_PERSISTENT_TAIL=1` 用块内实际剩余的最坏固定成本替换每次访存的16次取指预留。
访存提交前仍预留整个剩余尾部，包含条件执行和乘法的内部周期；只减少保守退出，
不修改 guest 计费或事件上限。补写仅发生在本块发布前。

`GBA_DYNAREC_PERSISTENT_MAP_HELPER=1` 将六种读写宽度的映射守卫放入共享原生代码，
避免每条 guest 访存重复生成 RAM/ROM、GPIO、对齐、范围和代码页判断。
共享代码不调用C、不写guest内存、不计费，返回映射后仍由指令现场检查预算并提交。
保留r4-r12及真实栈，r14仅作宿主BL返回；guest LR仍按原映射保存。
超过相对分支范围时使用绝对BLX，因此不限制调用者提供的大代码区容量。
重置、WAITCNT、状态恢复及缓存回收撤销共享入口；发布失败不得使用。

`GBA_DYNAREC_PERSISTENT_READ_WAIT=1` 还要求 `GBA_FAST_WAIT=1`。
普通RAM/驻留ROM只读访存保留既有等待循环识别记录，写入继续撤销记录。
设备、GPIO、未对齐、开放总线等访问继续原慢路径。识别仍比较完整CPU及总线现场，
按既有下一事件和调用预算，只快进完整的稳定循环，不扩大近似窗口。
原生只读访问不更新读哈希；该哈希仅用于失败重试启发式，不参与成功循环的等价判定。
上述三个开关均默认关闭，通用C路径与GAST v7均不变。

基本块在事件前退出。地址/预算守卫失败时，尚未提交的当前指令交回原执行器，
不能作为永久的解码拒绝；只有无法解码的块头可以负缓存。
DMA数据可见性、IRQ、控制返回以及端口4096周期分段规则保留。

## 生命周期与内存

代码和元数据仍由 `gba_dynarec_bind()` 借用，不在热路径申请堆。
元数据区按容量分配旧块表和持久块表。代码区共用分配器，回收必须使两组派生入口失效。
持久块采用四路组相联查找，入边使用256个固定哈希链表；编译及回填不再遍历整个块表。
连续无条件普通ALU复用相同的末指令访存元数据，省去重复栈写；预算扣除和守卫位置不变。
持久块及流水线前瞻登记到共享物理代码行图，普通数据写先检查行图，不扫描代码注册表。
修改代码或替换持久槽位时撤销入边和间接缓存；复位、WAITCNT和状态加载撤销全部入口。
写入和回填只发生在生成代码停止执行时，统一调用 begin-write/publish 接口。
发布错误终止本次执行，不能继续回退并执行未验证代码。

持久缓存、链接和派生调色板都不序列化，GAST v7 配置签名和验证顺序不变。
该模式与旧 CHAIN、ROM_TRACE、TRACE_LINKS、INTERIOR、FASTCALL 执行组织互斥。

## 普通扫描线

`GBA_PPU_SCANLINE_FAST=1` 提供 Mode 0 普通文本背景/OBJ合成，默认关闭。
输出直接使用目标格式调色板与8位优先级，不建立 second layer。
矩形窗口转换为行段；图块和调色板依赖按段登记。
半透明、混色、仿射、mosaic、OBJ窗口及其他模式保持通用渲染器。
可选 `GBA_PPU_SCANLINE_IDENTITY=1` 接受逐像素等价的退化效果：EVA 饱和且 EVB=0、
亮度系数为0，以及对应 MOSAIC 维度为1×1。真实效果仍走通用渲染器，默认关闭。
`GBA_PPU_SCANLINE_PALETTE_DEDUP=1` 在整条扫描线累计已登记的调色板依赖，
不再为每个图块清空位图。渲染期间没有guest写入，跨图块/图层/窗口段的依赖并集保持相同。
清零使用平台内存接口，保留无libc、无堆构建；该开关也默认关闭。
调色板写通知、外部失效、复位和状态恢复维护派生颜色。
禁止与绕过写通知的原生视频写配置混用。

## 验证边界

`test/dynarec.py --persistent --persistent-proof` 执行真实 ARM926 生成代码，
验证多块驻留、模式桥接、间接命中、访存、镜像失效和状态错误路径。
`--code-kb 64 --data-kb 32` 的小缓存测试还覆盖故意制造的四路冲突、
入边槽位重用、DMA改写代码、epoch回绕、WAITCNT及多次整缓存回收。
`--scanline-fast` 将渲染器加入真实运行差分，`--no-stats` 验证统计关闭版本。
完整精确矩阵和13个参考ROM与解释器比较；近似模式双副本只证明确定性和有限运行。
`--persistent-multi` 覆盖多寄存器执行、128种方向/写回/条件/镜像组合及真实连续 PUSH/POP 链。
`--identity-effects --scanline-fast` 将恒等效果路径加入原生执行回归。
统计打开时 `persistent_multi_instructions` 记录实际完成的多寄存器指令，
`persistent_multi_sites` 仅记录翻译位置，不能用后者代表真实运行工作量。
`--tail-proof` 扩展加载/MLA尾部预算差分；`--map-far-call` 强制执行绝对BLX路径，
可与64KiB代码/32KiB元数据及关闭统计组合，覆盖回收、发布失败和状态恢复。
`--fast-core --read-wait --read-wait-proof` 验证原生只读轮询保留等待识别，
关闭开关的对照仅快进52周期，开启后快进3538周期，二者精确CPU/内存/周期差分均一致。
正例另要求足量快进，避免把偶发标量循环命中当成原生保留的证明。

掌机测试入口、固定状态、原始UART及实机指标位于固件仓库本轮结果目录。
QEMU时间和连续执行指令数不代表 F1C 实机帧率。默认开关只能在完成实机目标验收后调整。

## 2026-10-17 热路径候选

`GBA_DYNAREC_PERSISTENT_INDIRECT_BITS` 支持6至10，默认6。扩大表会消耗调用者的
固定元数据容量，不能假定更大必然更快。`GBA_DYNAREC_PERSISTENT_BL_DIRECT` 只将
同块已知的Thumb BL前后半条连接为直接边；块边界的独立后半条保持原语义。
两者的碰撞/BL合成测试大幅减少C往返，但同场景板测Mario约0%、Emerald不到3%，
没有据此启用生产配置。

`GBA_DYNAREC_PERSISTENT_SHARED_EXITS` 将每个访存指令重复的拒绝处理移到块尾，
多站点共享PC和预算恢复；单站点保留专用出口。守卫执行前没有访存副作用，
首条指令的分支refill退款、NZCV和流水线恢复沿用原规则。
`GBA_DYNAREC_PERSISTENT_ROM_WINDOW` 要求原生mapper，以一次无符号范围检查
处理08/09主窗口中首4KiB之后的驻留ROM数据。GPIO、越界、非对齐和其他等待区
仍进入原路径；读取实时ROM数据，不把字面量永久折叠到生成代码中。
`persistent_rom_window_hits` 是实际原生mapper选中次数，发生在调用者预算守卫之前，
不能解释为已完成读指令数。精确测试检查端点、读宽度、镜像、GPIO、越界、预取与发布后修改。

`GBA_PPU_SCANLINE_ZERO_ROWS` 读取图块并登记依赖后跳过全透明4/8bpp行段；
保留OBJ选择预算及锁存，不跳过可见颜色。16/32位、颜色交换和缓存组合逐像素比较通过。
这些新候选均默认关闭，尚不能作为59.5FPS或完整板端功能验收证据。

### 条件分支超级块与Mode 1快速扫描线

`GBA_DYNAREC_PERSISTENT_BRANCH_SUPERBLOCK` 默认关闭。开启后，块内第一个条件直接分支
只生成taken侧边，fallthrough继续在同一个块中翻译；块尾再使用第二个边连接顺序后继。
遇到第二个需要双出口的控制流时在它之前结束块，避免覆盖第一条入边。
`GBA_DYNAREC_PERSISTENT_BLOCK_INSNS` 把扫描上限从16扩展到1至128；超出4096字节
生成缓冲的候选不再发布，也不把容量拒绝误报为设备故障，而是安全回退到标量执行。
条件分支前后仍逐条扣取指周期；预算、流水线、refill和自修改撤销语义未放宽。

快速扫描线现在接受Mode 1：BG0/BG1走文本路径，BG2走仿射路径。普通OBJ不再因仿射
标志退出，按锁定OAM矩阵、双尺寸、对象预算和优先级逐像素采样。文本BG的MOSAIC使用
对齐后的源像素，在一个mosaic单元内只解码一次图块行；对象mosaic、仿射BG mosaic、
非恒等混合和OBJ窗口仍保留通用渲染器。

验证结果：分支证明覆盖ARM/Thumb、taken/fallthrough、汇合点和1至257周期边界；
合成精确矩阵与13个参考ROM均零失败。视频矩阵覆盖916个场景、3566帧变体，
包含16/32位颜色、颜色交换、窗口、OAM锁存和缓存组合；四个固定游戏各18000帧的
画面、PCM与GAST摘要一致。QEMU结果不替代F1C板端帧率或音频验收。

### 本轮实机结论

固定Mario/Emerald场景中，最终安全配置为透明行、恒等效果、调色板整行去重和
`GBA_PPU_OBJ_INDEX=1`，4MiB代码/512KiB元数据；实测14.747/21.482FPS，
相对旧透明行基线为+11.7%/+0.9%。这仍远低于59.5FPS，不构成最终性能验收。

无内联的文本/仿射/mosaic扫描线函数配合普通OBJ内联，避免了整行像素回退，
Emerald从错误的14.7FPS恢复到20.9～21.5FPS。8MiB代码缓存只有约1.5%收益，
空闲循环记忆回退2.2%/4.2%，条件分支超级块只有约0.7%收益，均不提升默认。
生成代码统计显示平均约43个A32字/guest指令、每块约6条guest指令；
剩余差距需要新的执行组织或更大规模后端重构，不能靠继续缩短几个分支补齐。

### 2026-10-17 紧凑执行候选

`GBA_DYNAREC_PERSISTENT_BRANCH_SUPERBLOCK` 现在允许一个持久块保留最多六个直接出口，
连续的 taken 条件分支和 fallthrough 可以共享同一个块体。每个出口仍有独立的 PC、
模式、流水线和 refill 记录；链接、撤销及间接缓存都按全部出口处理。
`GBA_DYNAREC_PERSISTENT_BLOCK_INSNS=32` 是本轮筛选配置，生成缓冲按块上限分配，
不会越界写入调用者提供的代码区。

连续普通 ALU 的取指周期在块内合并提交，内存、分支和块尾之前仍按原位置扣减。
这保持精确差分中的预算边界，同时减少每几条 shift/ALU 重复执行的 `SUB cycles`。
compact refill 把 taken 边目标的 refill 守卫、目标预算检查、NZCV 保存和失败退出
放在目标块的一个入口中；预算失败不会先扣 refill，成功路径不会重复执行块体守卫。
`GBA_DYNAREC_PERSISTENT_DIRECT_BRANCH` 在 32 MiB 代码范围内用可原子回填的 ARM B
替代 `LDR pc,literal`；超出范围自动使用原绝对字面量路径。

`GBA_DYNAREC_PERSISTENT_PAGE_TABLE` 提供 32 KiB 页表映射，页类型放在对齐页基址的
低两位；写路径仍保留 32 字节代码行检查。该候选通过精确差分，但固定 Mario QEMU
副本比原共享 mapper 慢约 15%，因此不进入生产配置。

最终组合（32K event slip、4MiB 代码、1MiB 元数据、32 条多出口块、compact refill、
取指合并、直接分支边）的 QEMU ARM926 精确证明为
`assertions=0x184b`、`failures=0`；带统计版本为 `0x18c0`、`failures=0`。
13个参考 ROM 在源核心和固件导入核心上均为 `assertions=0x41`、`failures=0`。
Mario、Zelda、Emerald、Metroid 的固定状态各60帧副本在两种构建中得到相同
PC、周期、镜像和代码字节数。QEMU 墙钟只用于筛选；同环境基线约17.3秒，
紧凑候选约15.1秒，不能解释为实机 FPS。

板端最新有效筛选仍是 32K event slip 的 Mario 19.151FPS、Emerald
22.328FPS；本轮新候选尚未获得板测，不能宣称达到59.5FPS。
固件compact候选BIN SHA256为`83bf0e78009be23fc041038f6ff970a9ad7a42d71ced1bc327106f76f65b66d6`。
