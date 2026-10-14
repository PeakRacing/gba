# 移植与资源契约 / Porting contract

本工程使用 C11。所有编译单元、端口和应用必须使用同一组 `GBA_*` 配置；配置会改变公开结构的大小。不要将一个配置编译出的库与另一配置的头文件混用。核心只依赖标准整数类型及平台函数，不包含 SDL、RTOS 或线程调度。

## 生命周期与所有权

`gba_init_static(gba_t *, const gba_memory_t *)` 借用全部资源，成功后即可加载 ROM。它检查长度、实例与资源的对齐、资源范围重叠；不分配堆。初始化会清空实例和硬件 RAM，调用 `gba_initex()`。一个已初始化且仍在运行或拥有 ROM 的实例不能直接再次初始化；先卸载和释放。初始化返回失败时由调用方负责回收端口已创建的资源。

`gba_memory_bind()` 把一个至少 `gba_memory_size()` 字节、4 字节对齐的连续块拆分为各区；它只建立描述符。也可以手动填写 `gba_memory_t`，将不同区域放在内部 RAM、外部 SRAM/SDRAM；参见 `rtthread/example_static.c`。`gba_t`、栈和下面所有区必须同时可访问，缓冲间不能重叠。

| 区域 | 必需容量 | 用途 |
| --- | ---: | --- |
| EWRAM | 262144 B | GBA 外部工作 RAM |
| IWRAM | 32768 B | GBA 内部工作 RAM |
| VRAM | 98304 B | 背景、OBJ 与位图 |
| Palette / OAM | 各 1024 B | 调色板与对象属性 |
| save | `GBA_SAVE_CAPACITY` | 默认 131072 B；存档关闭可为 0 |
| framebuffer | `GBA_DRAW_SIZE` 个 `gba_color_t` | 240×160 或 240×80，16/32 位 |
| rom_cache | `GBA_ROM_CACHE_SLOTS * 8192` B | 仅流式配置必需 |

默认硬件资源 603136 B，关闭存档的半屏 16 位配置为 433664 B；这些数字不包含实例、调用栈、ROM、外部 BIOS、线程栈、端口音频/显示队列和文件系统缓存。实例含扫描线工作区、I/O、硬件状态、1024 B OAM 锁存及可选 PCM 暂存；当前 x64 MSVC 默认实例 6216 B、完整配置 7520 B。目标 ABI 和配置可能改变实例大小，应使用 `sizeof(gba_t)`。

动态 `gba_init()` 分配实例及一个硬件资源块，随后调用同一静态路径。`gba_deinit()` 只接受它创建的实例，静态实例使用 `gba_deinit_static()`。正常卸载/退出会先保存电池数据；失败返回错误并保留实例、ROM 和脏状态，以便重试。`gba_unload_rom_discard()` 明确丢弃状态，用于用户选择放弃或故障恢复；调用方仍拥有借用的资源。

## ROM、BIOS 与运行

`gba_load_rom()` 借用只读 ROM，接受 192 B–32 MiB；调用方保持其生命周期至卸载结束，且不要把 ROM 放在模拟器会写入的 RAM 中。`gba_set_bios()` 借用精确 16384 B 的外部 BIOS；传入 NULL/0 恢复 HLE。关闭 HLE 的配置需要外部 BIOS 才能启动。工程不提供 BIOS 数据。

流式模式启用 `GBA_ROM_STREAM=1`，调用 `gba_load_stream(g, size, reader, user)`。回调签名：

```c
size_t reader(void *user, uint32_t offset, void *data, size_t size);
```

回调须同步完成按偏移读取，返回实际字节数。默认 4 个 8 KiB 页，LRU 选择槽位；末页只读取 ROM 范围内的字节，完整成功才提交标签。跨页、DMA、CPU 和 HLE 都使用统一 ROM 路径。短读使 `fault=GBA_ERROR_IO` 并停止；不要在错误后继续运行，丢弃并重新加载。回调用户数据由调用方管理，核心不关闭它。`gba_load_file()` 则由核心管理文件或动态 ROM；因此“静态初始化 + 常驻文件加载”仍会分配 ROM，完全无堆的应用应借用 ROM 或使用自定义流式回调。

`gba_step()` 推进一个 CPU 指令、DMA 单元或 HALT 事件。CPU 指令期间可能插入多个 DMA 单元，返回周期包含总线阻塞时间；`gba_run_cycles()` 执行到至少指定周期，可能因指令越过预算，STOP、退出或错误提前停止；`gba_run_frame()` 推进到下一帧边界。返回值是实际模拟周期，停止后为 0。`gba_run()` 是连续循环，端口须负责输入、节拍和退出，避免 STOP 下空转。CPU/总线访问推进共享时钟，设备使用最近事件调度；精确时序限制见验证文档。

`gba_set_keys()` 使用十位按下位掩码；KEYINPUT 自动反相并更新 KEYCNT IRQ。回调内可设置按键和 `gba_quit`，不可重入 `gba_step()` 或修改生命周期/加载/存储。实例不具备线程安全性，多个线程或 ISR 访问时由端口串行化。

## 配置

在工程配置头 `gba_conf.h` 或统一编译定义中覆盖默认值。

| 宏 | 默认 | 说明 |
| --- | ---: | --- |
| `GBA_COLOR_DEPTH` | 16 | RGB565；32 为 `0x00RRGGBB` |
| `GBA_COLOR_SWAP` | 0 | 仅交换 16 位颜色字节 |
| `GBA_RAM_LACK` | 0 | 1 使用半屏缓冲 |
| `GBA_FRAME_SKIP` | 0 | N 表示每 N+1 帧提交一帧画面；硬件继续运行 |
| `GBA_ENABLE_SOUND` | 0 | PCM 输出；关闭仍推进 PSG/FIFO 和请求 DMA |
| `GBA_APU_SAMPLE_RATE` | 32000 | 8000–96000 Hz |
| `GBA_APU_BUFFER_SAMPLES` | 256 | 每次输出的双声道帧数 |
| `GBA_USE_FS` | 0 | 文件端口及电池文件存储 |
| `GBA_ENABLE_SAVE_STATE` | 0 | 需文件系统支持 |
| `GBA_ENABLE_SAVE` | 1 | 卡带存档设备 |
| `GBA_SAVE_CAPACITY` | 131072 | 可减至设备所需容量，容量不足的卡带拒绝加载 |
| `GBA_ENABLE_RTC` | 1 | GPIO RTC，2000–2099 年 |
| `GBA_BIOS_HLE` | 1 | 自主 HLE；未支持 SWI 明确停止 |
| `GBA_ROM_STREAM` | 0 | 回调/文件流式加载 |
| `GBA_ROM_CACHE_SLOTS` | 4 | 编译期可配置页数 |
| `GBA_TEST_MODE` | 0 | 指令探针与测试端口功能 |
| `GBA_TEST_PROFILE` | 0 | 独立模块计时与缓存计数 |

两个测试宏互不依赖，生产交叉链接检查确认关闭时没有对应探针符号。请根据最终 ELF 检查体积；它不等于未裁剪静态库中全部符号之和。

## 输出与时间

`gba_draw(x1,y1,x2,y2,pixels)` 使用包含端点的矩形，画面按行连续。整屏提交 `(0,0,239,159)`；半屏依次 `(0,0,239,79)`、`(0,80,239,159)`。半屏第二次提交会复用同一缓冲。`gba_frame()` 每个模拟帧仍调用，包含跳帧；端口负责缩放、显示节拍和输入。

`gba_sound_output()` 接收 S16LE、双声道、交错 PCM，字节数为样本帧数×4。显示与 PCM 指针只在本次回调内有效；异步 DMA/播放必须在返回前复制到端口队列或等待消费，返回非零会使实例故障。SDL 复制到播放队列，预填约 100 ms 后开始播放；同步保存/恢复时暂停设备，保存后继续原队列，恢复后清空旧队列并重新预填。存储结束后重新建立帧节拍基准，避免补跑存储耗时。

`gba_get_ticks()` 是可注入、单调递增的毫秒 tick，使用无符号 32 位差值处理回绕；不能用宿主日期直接补算 RTC。RTC 在运行帧/协议访问时同步；加载、重置和状态恢复会重新建立基准，关闭期间不补时。若应用暂停实例但保留 RAM，请在恢复时调用 `gba_rtc_rebase()`，排除暂停期间的 tick。裸机默认 tick 为 0，RTC 不自动走时，移植端应提供独立时间源。

## RT-Thread 与无堆验证

`rtthread/SConscript` 只编译核心和 RT-Thread 端口，避免同时编译默认 libc 端口。将 Kconfig 菜单接入工程，启用 `PKG_USING_GBA`，提供 `gba_board_draw/keys` 和可选 `gba_board_sound`；未绑定显示/声音的默认实现返回不支持。文件模式需要 BSP 提供全部文件函数及安全替换语义。未启用 `RT_USING_HEAP` 时分配函数返回 NULL，应使用静态初始化和借用/流式 ROM；动态初始化需要 RT-Thread 堆。

`test/check_rtthread.ps1 -RTThread <root>` 用指定真实 RT-Thread 头文件编译核心、端口与静态示例。它仅做接口编译，不链接 RTOS、不证明调度/驱动可用。`test/cross.ps1` 的 Cortex-M4 ELF 则链接无堆静态路径并检查符号，但没有向量表、板级启动代码或硬件执行。通用平台能力与具体板卡的帧率、栈高水位和音频连续性需要分别验收。

English: use one configuration for all translation units; static initialization borrows disjoint hardware buffers and performs no allocation. ROM and BIOS bindings are borrowed. Streaming reads must complete synchronously; short reads latch a fatal error. Draw and PCM callbacks must consume or copy their buffers before returning. Serialize access to each instance. Rebase runtime RTC after application pauses. Cross compilation proves interface/link portability only; board performance remains unmeasured.

连续运行的事件期限是派生工作状态，单步、寄存器写入、复位和恢复会失效；不进入即时存档。渲染仍为扫描线精度。速度与精度边界见 [fast-core.md](fast-core.md)。
