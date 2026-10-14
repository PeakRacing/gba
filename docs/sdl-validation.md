# SDL2 ROM 验证 / SDL2 ROM checks

日期：2026-10-14，Windows x64、MSVC release，SDL2 系统显示与音频驱动。按用户授权，从 `E:/Game/GBA汉化全集` 只读选取 ROM，复制到忽略目录 `test/out/sdl-games/`，存档与状态文件均使用副本路径。未修改游戏库内的 ROM 或旧存档，也不向工程分发商业 ROM。

每款运行到 600 模拟帧，自动输入 Start 和间歇 A；第 240 帧保存即时存档，第 360 帧恢复到 240，所以实际输出 720 帧。恢复后的第 241–360 帧逐帧比较 CPU、硬件事件、IO、EWRAM、IWRAM、VRAM、调色板、OAM、电池内容、RGB555 画面和 PCM，共 120 帧。只排除恢复后用于持久化的 dirty 标记；测试注入固定 RTC tick，避免真实时间影响重放，SDL 显示和播放节拍仍用真实时间。

| 游戏 | 结束帧 | 保存/恢复 | 重放一致 | 队列欠载/溢出 | 判定 |
| --- | ---: | --- | ---: | --- | --- |
| 黄金太阳 | 600 | 0 / 0 | 120 / 120 | 0 / 0 | SMOKE |
| 塞尔达传说：缩小帽 | 600 | 0 / 0 | 120 / 120 | 0 / 0 | SMOKE |
| 银河战士：融合 | 600 | 0 / 0 | 120 / 120 | 0 / 0 | SMOKE |
| 火焰纹章：烈火之剑 | 600 | 0 / 0 | 120 / 120 | 0 / 0 | SMOKE |
| 精灵宝可梦：绿宝石 | 600 | 0 / 0 | 120 / 120 | 0 / 0 | SMOKE |
| 超级马里奥 Advance | 600 | 0 / 0 | 120 / 120 | 0 / 0 | SMOKE |
| 逆转裁判 | 600 | 0 / 0 | 120 / 120 | 0 / 0 | SMOKE |
| F-Zero GBA | 600 | 0 / 0 | 120 / 120 | 0 / 0 | SMOKE |

八款退出码均为 0，无核心 fault，输出非零 PCM；准确文件名、SHA256、PC、画面/PCM hash 和运行时间见 [sdl-results.json](sdl-results.json)。人工检查黄金太阳、缩小帽的捕获帧，均已进入名称输入界面。汉化文件名不代表所有画面都已翻译。

首次测试有 1–13 次队列欠载。SDL 端口改为预填约 100 ms，同步存储时暂停播放并保留队列，保存后继续播放，恢复后清空旧队列并重新预填；存储结束后重建帧节拍。随后八款复测队列计数均为 0。该计数检查 PCM 回调时队列是否为空/过长，不是硬件扬声器波形或音频后端 xrun 统计；存储期间有意暂停播放，也不声称声音永不中断。

复现：先在 `sdl/sdl2/` 构建 `gba-test`，回到根目录运行：

```powershell
./test/sdl_roms.ps1 -ROMDirectory 'E:/Game/GBA汉化全集'
# 可选无真实设备版本，不作为系统显示/音频验收
./test/sdl_roms.ps1 -ROMDirectory 'E:/Game/GBA汉化全集' -Dummy
```

脚本生成各款 `.stderr.txt`、`.ppm`、状态/电池文件和 JSON，恢复全部测试环境变量。保存、恢复、重放或核心执行失败会返回非零。重放比较代码仅存在于 `GBA_TEST_MODE` 的桌面前端，生产核心和 MCU 链接均不包含。

SMOKE 证明有界运行和此次恢复重放；尚未验收完整关卡、游戏内电池存档、操作手感、长时间音频品质或所有汉化补丁兼容。严格 CPU/总线/定时器与视频验收的失败保留在 [validation.md](validation.md) 和 [remaining.md](remaining.md)。按用户要求当前不使用真机。

English: eight authorized local games complete bounded SDL2 system-driver startup/menu checks and 120-frame state replay with zero queue empty/overrun observations. ROM library files are unchanged; all outputs use ignored copies. The replay test holds RTC tick fixed and excludes persistence dirty flags. These checks do not establish complete-game compatibility or physical audio fidelity.

## 本轮深入验证

快速核心的生产路径另以无探针基准测量，SDL 测试前端用于可重放的功能与生命周期检查。`test/game-inputs.json` 为每款固定输入与最低帧数；`test/sdl_roms.ps1 -Deep` 逐款使用这些输入，120 帧状态重放后继续运行，并执行五次正常卸载/重新加载。每次重载比较电池设备长度和完整内容 hash，不能只检查返回值。

| 游戏 | 深入运行帧数 | 捕获场景 | 电池行为 |
| --- | ---: | --- | --- |
| 黄金太阳 | 3600 | 村庄开场避难剧情，人物与室内/室外背景 | 仍为 FF；剧情中保存受限，游戏内保存未覆盖 |
| 缩小帽 | 9000 | 主角醒来的房间 | EEPROM 8 KiB 数据生成、五次重载一致 |
| 银河战士融合 | 18000 | 角色站在太空站房间 | SRAM 数据生成、五次重载一致 |
| 烈火之剑 | 9000 | 琳的开场剧情/草地战场 | SRAM 初始化/游戏数据生成、五次重载一致 |
| 宝可梦绿宝石 | 至少 8000，保存续测到 8800 | 主角房间、菜单及保存确认 | 菜单保存后 Flash 128 KiB 有 56395 个非 FF 字节，五次重载一致 |
| 马里奥 Advance | 3600 | 第一关可操作角色 | EEPROM 512 B 数据生成、五次重载一致 |
| 逆转裁判 | 至少 7200，保存续测到 7800 | 初次法庭对话、记录确认 | 菜单记录后 SRAM 有 10593 个非 FF 字节，五次重载一致 |
| F-Zero | 3600 | 赛道、车辆与圈数计时 | SRAM 数据生成、五次重载一致 |

八款均完成 120 帧 CPU/硬件/RAM/画面/PCM 重放且无 fault，队列欠载/溢出计数为 0；非零 PCM 已记录。这是有限场景和软件队列观测，不证明扬声器波形、所有操作路线或整款游戏无问题。实际设备和 ROM 的完整身份见 [深入结果](sdl-deep-results.json)；续测的菜单保存证据见 [游戏内保存](game-save-results.json)。

黄金太阳的 Save 入口按 [任天堂游戏手册](https://www.nintendo.com/eu/media/downloads/games_8/emanuals/game_boy_advance_8/Manual_GameBoyAdvance_GoldenSun_EN_DE_FR_ES_IT.pdf) 操作；手册明确剧情/战斗期间不可进入保存暂停菜单。本轮未走出相应开场事件，不能声称游戏内保存已通过。全部游戏设备都有后备存储，本轮没有可标记为“无电池设备”的游戏。

测试前端支持固定 CSV 输入、加载诊断状态和保存最终诊断状态，代码只在 `GBA_TEST_MODE` 编译。诊断状态用于进入菜单和观察效果；它不能替代从游戏实际写入后备存储的证据。测试端口还修正了 CLI 步数耗尽却返回成功的问题，现在请求帧数未完成且没有套件完成标记会返回非零。

English: eight games reached bounded gameplay/story scenes, with deterministic state replay and five battery-content reload checks. Pokémon and Ace Attorney also executed their menu-save actions. Golden Sun remains inside its opening event, so in-game saving is explicitly unverified. Nonempty battery data in other titles proves game-generated persistence, not every save slot or completed-level reload.
