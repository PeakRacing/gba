# 存档、文件端口与恢复 / Storage and recovery

## 卡带与电池文件

核心识别 SRAM/FRAM、Flash 64/128 KiB、EEPROM 512 B/8 KiB 与 RTC 签名；用 `gba_set_save_type()` 在加载前覆盖无签名或误识别的设备。FRAM 与 SRAM 使用相同 32 KiB 存储模型。EEPROM 6/14 位地址按 DMA 协议长度识别。自动检测模式在重新加载时接受精确 512 B 或 8192 B 的原始 EEPROM 存档，完整读取成功后才提交设备长度；显式类型覆盖、其他长度和容量不足仍拒绝。加载后更换设备会重新识别设备及数据长度，脏存档会拒绝切换，避免在有数据时随意切换。

`gba_save_size()` 给出当前设备实际数据长度。无文件系统时使用 `gba_save_import/export()`：导入长度必须精确匹配，导出缓冲至少容纳该长度，文件和掉电保障由应用负责。该接口仅传递电池数据，不传递 CPU 或 RTC 协议状态。若需要无文件系统的 RTC 长期保存，应用须单独持久化 RTC 日期字段并在恢复后调用 `gba_rtc_rebase()`；目前没有独立公共 RTC blob 导入/导出 API。

文件模式以 ROM 路径作为基名：`game.gba.sav` 是原始设备字节，没有额外头；`game.gba.rtc` 是独立 RTC 文件。也可用 `gba_persist_set_path()` 指定基名。缺失文件使用初始擦除值，已有文件大小不符或损坏时返回错误并阻止自动覆盖；调用方应提示用户、备份并决定是否清除路径阻止状态。不要把格式错误当作“没有存档”。

写文件的顺序：写入同目录 `.tmp` → `gba_file_sync()` → `gba_fclose()` → `gba_file_replace()`。任何一步失败保留旧文件和脏状态，可调用 `gba_persist_save()` 重试。`.sav` 与 `.rtc` 分别原子替换，二者不构成跨文件事务。退出或换 ROM 的保存失败保留实例；确认放弃时才用 `gba_unload_rom_discard()`。

文件端口的函数约定沿用 stdio 的元素数、seek 和错误返回方式。`gba_fclose()` 无论成功或失败都消耗句柄，核心不会二次关闭。流式 ROM 关闭失败会先解除读取回调并锁定重置，需完成卸载后重新加载，避免再次访问已关闭的句柄。`gba_file_exists()` 返回 1 存在、0 不存在、负数错误。`gba_file_replace()` 必须在失败时保留旧目标；RT-Thread/FAT 上不能用“先删除旧文件再 rename”的方式实现。可以实现备份/恢复或文件系统提供的原子替换。Windows 默认端口采用 UTF-8 路径转换和 `MoveFileExW`，POSIX 用 rename；同步数据并不证明目录元数据/存储器在任意掉电时都已持久化，需要目标平台验证。

## RTC 文件 GART v1

固定 48 B，所有整数为小端 u32。

| 偏移 | 内容 |
| ---: | --- |
| 0 | ASCII `GART` |
| 4 | 版本 1 |
| 8–40 | 年(0–99)、月、日、星期(0–6)、时、分、秒、毫秒、控制字段，各 u32 |
| 44 | 前 44 B 的 CRC32 |

校验日期范围、闰年和月份天数。恢复后重建宿主 tick 基准；不保存关闭时间，不补算离线时间。GPIO 的命令、移位位数等瞬态仅进入即时存档。

RTC 内部小时始终为 0–23，串行读取按控制位编码为 24 小时或 0–11 加 PM 位；24 小时模式也输出 PM 位。12 小时模式的午夜/正午为 00h，正午带 bit7。日期/时间写入先校验全部字段；非法 BCD、非闰年 2 月 29 日等整笔拒绝，保留原时间和 tick 基准。Force Reset 清零年、星期、时间、毫秒及控制，月/日设为 1，并重新建立 tick 基准。控制 bit7 读后清除，保留本次返回的原值；未使用寄存器读 FFh、写入忽略。零参数命令在 CS 降低前不会重新解释后续时钟。

Force IRQ 产生 Game Pak IRQ 请求；IE 已启用此中断时解除 STOP，之后按正常 IF/IME 和 CPU IRQ 延迟处理。STOP 下就绪 DMA 不推进模拟周期，唤醒后继续传输。每分钟中断按平台 tick 模拟 30 秒有效期，IF 确认后有效期内可以重新请求，IE 启用时解除 STOP；关闭/暂停后重新建立基准，不补算离线事件。未知 duty/hold 控制位和实际电气脉冲不模拟。冷启动使用固定初始日历和 24 小时模式，不模拟实际电池短路后的全部芯片状态。

## 即时存档 GAST v7

需要 `GBA_USE_FS=1` 和 `GBA_ENABLE_SAVE_STATE=1`。v7 增加垂直窗口锁存、四路背景启用延迟、下一扫描线的 OAM 快照和 DMA 结束后的开放总线有效期。继续保存四路定时器 reload 提交剩余周期、RTC 串行事务、预取槽数/部分读取相位和下一取指顺序状态。明确拒绝 v1–v6，不自动转换；原始 `.sav` 和 GART v1 RTC 文件不受影响。当前是开发期格式，请勿作为长期兼容的发布格式。以后修改字段布局或恢复语义必须升级版本与迁移规则。

32 B 头，u32 小端：

| 偏移 | 内容 |
| ---: | --- |
| 0 | ASCII `GAST` |
| 4 | 版本 7 |
| 8 | payload 字节数 |
| 12 | 完整 ROM 的 CRC32 |
| 16 | 外部 16 KiB BIOS 的 CRC32，HLE 为 0 |
| 20 | 配置 CRC32 |
| 24 | payload 的 CRC32，包含各块头 |
| 28 | 保留，必须为 0 |

块头为 4 B ASCII 标签 + 4 B 小端长度，顺序固定：`MACH`、`IO  `、`EWRM`、`IWRM`、`VRAM`、`PAL `、`OAM `、`SAVE`、可选 `AUD `。MACH 将 `gba_machine_t` 中的 u32 标量按声明顺序逐个编码，包含 CPU/寄存器分组、流水线、IRQ 线及剩余延迟、事件相位、DMA 锁存/请求/启动剩余周期、定时器、启动与 reload 提交剩余周期、PSG/FIFO、卡带协议和 RTC；不将 native struct 原样写文件。其余 RAM 为原始字节，SAVE 保存配置容量；AUD 包含待输出 PCM 的帧数 u32 和整个暂存缓冲的 S16LE 数据，使半个音频块也能确定性恢复。

配置 CRC 涵盖颜色、整/半屏、声音、存档、RTC、HLE、跳帧、字节交换、采样率和 PCM 缓冲大小。文件长度也验证存档容量和硬件状态大小。ROM 身份计算使用同一流式路径，会读完整 ROM，可能耗时；在流式 I/O 失败时同样停机。

明确排除宿主指针、文件句柄、借用 ROM/BIOS、ROM 缓存、画面缓冲、宿主播放队列和性能计数。恢复后缓存失效并调用 `gba_state_loaded()`；平台须清空旧 PCM/显示队列，下一次画面生成重新提交。若在半屏中途恢复，未保存的上半屏不保证当帧完整，后续完整帧重新生成；一致性回归比较恢复后的完整运行帧。

保存采用与电池文件相同的临时文件协议。恢复有两遍：第一遍用 256 B 临时 I/O 块和一个候选硬件状态检查标签、长度、CRC、身份、字段范围及相互约束，不改 RAM；通过后 seek 重读并应用。应用阶段再次校验 CRC，短读、关闭错误或文件变化导致 `fault`，此时 RAM 可能已部分修改，必须丢弃并重新加载 ROM。该状态会锁定 `gba_reset()`，直到卸载游戏；不能用普通重置清掉错误后继续运行或覆盖电池文件。第一遍拒绝损坏文件则保持实例运行状态。

CRC 用标准 IEEE CRC32，多项式 `0xedb88320`、初始值及最终取反为全 1。CRC 用于发现损坏，不是认证。跨主机的文件格式是小端，但不同编译配置/硬件状态版本之间会明确拒绝，当前没有转换器。

本地故障注入覆盖短写、sync、close、replace、短读、损坏大小/CRC、RTC 非法日期和恢复第二遍失败；恢复重放包含非零 PSG PCM。真实 Flash/FAT 断电一致性、长时间 RTC、完整外部 BIOS 恢复及任意游戏存档仍未验收。

English: raw battery saves and versioned RTC metadata are separate files. Writes use temporary-file, sync, close, replacement; failures retain old data and dirty state. The close hook always consumes the handle. Save states use a little-endian, chunked, CRC-checked format and two-pass restore. A failure during application makes the instance unusable until the game is reloaded. Runtime RTC never adds offline elapsed time; persistence is not a substitute for board power-loss testing.
