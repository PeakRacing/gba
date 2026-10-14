# 单一快速核心 / Single fast core

2026-10-14，按用户更新的实施要求：速度和通用 MCU 资源优先，保持扫描线精度，不增加逐点渲染或快速/精确双模式。指令结果、内存和存档不能用精度取舍掩盖；HLE 微指令耗时、未定义乘法 C 及已分析的总线/IRQ 边界时序允许近似。

## 本轮实现

- 文本背景按 tile 行展开，普通仿射用坐标累加；1×1 马赛克的标量路径与批量路径在 64 个滚动、翻转、窗口和混合场景中一致。
- 每行筛选一次 OBJ，复用现有窗口工作区保存掩码，跳过无效果的混合计算；保留限额、优先级、半透明、窗口和 OAM 锁存。
- 连续运行直接进入内部执行循环，缓存相对硬件事件期限；寄存器写入、DMA 请求、IRQ 线变化、复位和恢复时失效。单步入口重新建立期限，外部行为不变。
- 定时器分频改为移位/掩码；PSG 相位未越过周期时省略除法。批量运行与单步运行的 CPU、事件、IO、RAM 和 PCM 相同，包含 reload 写入、级联、HBlank DMA 和 IRQ 确认。
- 常驻 ROM 的普通区域直接映射读取，GPIO、EEPROM、越界和流式访问继续走特殊路径。
- 修复自动 EEPROM 由 512 B 扩展到 8 KiB 后，重新加载拒绝自身存档的问题；只在完整读取成功后提交设备长度。显式类型、错误长度和容量不足仍拒绝。
- RTC 提供平台 tick 粒度的 30 秒有效期每分钟 IRQ 近似，支持 IF 确认后请求及 STOP 唤醒；不模拟电气脉冲，未知控制位不作额外解释。暂停存储后重建 RTC 基准。

公共函数不变。新增常驻状态只有一个派生期限 u32，不进入即时存档；GAST 仍为 v7。硬件缓冲大小不变，x64 full 实例由 7512 B 增至 7520 B，default 仍为 6216 B；ARM ABI 常驻空间增加 4 B。OBJ 筛选使用 512 B 调用栈工作区，ARM 最大单函数栈为 PPU 656 B、状态加载 2856 B；不能据此推断完整线程栈高水位。

## 无探针性能

四个 `gba-speed*` 目标强制 `GBA_TEST_MODE=0`、`GBA_TEST_PROFILE=0`，不等待显示节拍；生成声音的配置仍计算 PCM，输出端口消费后丢弃。每款预热 120 帧，再调用 1800 次 gba_run_frame，重复五次取中位数。输入为固定 Start/A 时间线，RTC tick 固定为 0，ROM 使用无旧存档的独立副本；加载、签名扫描和初始化不计入运行时间。长 HLE 操作可能使单次调用跨过帧边界，因此最终帧数为 1920 或 1921；各基线/优化组合的最终帧数和 PC 一致，完整工作负载身份见 [speed-workload.json](speed-workload.json)。

基线为本轮优化前冻结的可执行文件，主机 Windows x64、Ryzen 9 5900X、MSVC release。以下是各配置八款游戏中位耗时之和的下降比例：

| 配置 | 耗时下降 |
| --- | ---: |
| RGB565，声音开启 | 22.91% |
| 32 位颜色，声音开启 | 15.45% |
| RGB565，半屏流式，声音开启 | 15.77% |
| RGB565，声音输出关闭 | 24.97% |

32 个游戏×配置组合全部下降，单项范围 1.72%–38.06%，没有超过 5% 的退化。最小提升可能接近主机噪声，不能单独据此宣称优化收益。机器结果见 [speed-comparison.json](speed-comparison.json)、[基线](speed-baseline.json)、[优化后](speed-optimized.json)。主机仍可能有后台任务；这些测量不代表 MCU、SD 卡或显示总线的速度。

现有 `gba-bench` 保留性能探针，供定位模块和缓存命中率；其耗时不用于上述比较，模块计时有包含关系。生产 ARM 链接继续检查无测试探针和 libc 堆符号。

## 两种验收结果

`test/run.ps1 -FastAcceptance` 要求本地回归、常驻/流式/跳帧一致性通过，并运行完整严格套件及视频比较。严格的原始通过/失败计数保持不变。

[fast-approximations.json](fast-approximations.json) 逐项记录固定套件的 685 个差异及原因，匹配测试身份和原始结果值。20 个长乘法比较另读取栈参数，证明乘积、N/Z/V 相同，仅 C 不同。其他分组为 HLE 耗时、Game Pak/跨区 LDM/DMA 周期、IRQ 驱动的停止/读取相位及 HLE Halt 后的显示状态轮询；不会把它们宣称为硅片一致。

视频仍为 6/7 全像素一致。Layer toggle 2 的已记录 958 个差异属于扫描线启用与 HBlank 处理边界，保留完整 expected/actual 帧 SHA256。新的失败、旧结果值变化或差异图变化都会使快速门禁失败，须重新定位原因；脚本不能自动扩充允许列表。

`-StrictHardware` 继续返回失败，适用于要求完整硅片时序一致的场景。快速门禁不豁免崩溃、错误数据、保存恢复失败、FIFO/IRQ 功能错误或未知回归。

English: one fast core, scanline rendering, a derived event deadline, tile-row expansion and integer audio. Four uninstrumented host configurations reduce aggregate median time by 15.45%–24.97%. Strict differences remain visible and are accepted only by an exact, reviewed precision ledger. Neither host timing nor header/link checks establish MCU real-time performance.
