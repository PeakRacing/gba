# 限制与未覆盖项

2026-10-14，用户已将目标明确为单一快速核心、扫描线精度。原有严格时序差异保持原始结果，依据测试原序列分类到 [精度契约](precision-policy.md)；不再把逐点渲染、BIOS 逐指令耗时或未定义 C 一致作为本核心目标。新的未知差异仍阻止快速门禁。

| 项目 | 边界 |
| --- | --- |
| 全游戏兼容 | 八款只验证记录中的场景、输入、状态恢复和电池生命周期；未测试完整关卡/通关或全部汉化补丁 |
| 黄金太阳游戏内保存 | 本次仍处于开场避难剧情，Save 操作没有产生电池数据；保留未覆盖，不把 FF 文件重载算作保存成功 |
| 其他游戏的保存进度 | 宝可梦和逆转裁判明确执行菜单保存；其他游戏验证游戏生成的数据/初始化/自动保存及重载，不能等同全部保存槽和跨关卡进度验收 |
| 音频品质 | 方波频率、长度/包络、FIFO、PCM 重放和队列观测通过；真实波形品质、模拟滤波和所有商业声音驱动未验收 |
| RTC | 分钟 IRQ 为平台 tick/30 秒有效期近似；未知 duty/hold 位与电气波形不模拟 |
| BIOS | 外部 16 KiB BIOS 可绑定；未取得完整授权 BIOS 做启动与服务验收 |
| HLE 内部声音服务 | 未公开服务 0x20–0x24、0x2a 仍明确返回不支持，没有空实现伪装成功 |
| 实机 | 按用户要求当前不使用真机；MCU 帧率、线程栈高水位、显示/声音驱动和板端断电未验收 |

联机、Multiboot、无线与运动传感器仍在排除范围。MSVC、ARM GCC 无堆链接、SDL2/SDL3 和 RT-Thread 头文件检查不替代实机结果。所有商业测试使用忽略目录中的副本，原始游戏和存档未修改。

English: reviewed silicon timing differences are part of the selected fast precision contract, not hidden passes. Remaining coverage limits include full-game progress, Golden Sun's event-disabled saving, physical audio, a complete BIOS, undocumented sound services, and real hardware.
