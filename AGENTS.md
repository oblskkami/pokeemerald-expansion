# AGENTS.md — 本项目（PC 桌面版 / 宝可梦绿宝石改版）工作约定

> 本文件是给 AI 助手与协作者的现场手册。改动请保持"可验证"：写清文件路径 + 行号 + 现象 + 判读口径。
> 版本基准：pokeemerald-expansion **1.17.2**（`include/constants/expansion.h:5-7`），emerald，游戏文本已中文化。
> 工程根 = 仓库根：`D:\Game\lbs\pokeemerald-expansion\decp\pokeemerald-expansion`（会话工作目录是它的上层 `decp`，注意区分）。
> 当前开发主线：`ny2`（GBA 改版，游戏内容唯一真源）；PC 宿主版在 `multiplatform` = `ny2` + 宿主层，靠 `git merge ny2` 同步（见第九节）。

---

## 一、总目标与硬约束

1. **GBA 版必须始终能编译**（`pokeemerald.gba`，32MB ROM）。PC 版是附加目标，不允许为了 PC 破坏 GBA 构建。
2. 游戏内容（地图、文本、数值、learnset）以 GBA 侧为唯一真源；PC 版只做**宿主适配**，不改游戏逻辑。
3. 宿主适配相关代码一律进 `#ifdef PORTABLE`（或 `src/platform/`），并优先**对照参考分支**（见第八节）。
4. AI 助手**不能代编译**：`wsl.exe` 被沙箱拒绝（`Wsl/E_ACCESSDENIED`）。改动后由作者在 WSL 里编译并回传 `build/windows/crash.log`；助手可以直接读工作区文件（含 PE 符号表、ppm、bin）。

---

## 二、构建与分发

### 2.1 GBA（WSL Ubuntu-24.04）
```bash
cd /mnt/d/Game/lbs/pokeemerald-expansion/decp/pokeemerald-expansion
make -j8            # arm-none-eabi-gcc 13.2.1
```
产物 `pokeemerald.gba`。**不要**为 PC 版改动根 `Makefile`。

### 2.2 PC（Windows 原生 exe，交叉编译）
```bash
bash tools/build_windows.sh
# 等价于: make -f Makefile_pc -j8 WINDOWS=1 windows-executable
```
产物：`build/windows/pokeemerald.exe` + `build/windows/SDL2.dll`。运行目录就是 `build/windows`，存档 `pokeemerald.sav` 相对当前工作目录。

只查语法（快，不需要 SDL 运行库）：
```bash
make -f Makefile_pc -j8 host-audit
```

### 2.3 打包（Windows 侧脚本，WSL 里要带 `.exe`）
```bash
powershell.exe -ExecutionPolicy Bypass -File 'tools\package_windows.ps1'
```
输出 `dist/PokemonEmerald-PC-<yyyyMMdd>.zip`（含 exe / SDL2.dll / `docs/pc/操作说明.txt`，**绝不包含 .sav**）。

### 2.4 增量构建的坑
- 宿主编译走 `cpp | preproc | cc -x c -`（stdin），编译器产不出 `.d`；`Makefile_pc:101-102` 用
  `HOST_HEADERS := $(shell find include src -type f -name '*.h')` 让**所有 .o 依赖所有头文件**。改头文件后不重编的坑踩过（雌性立绘改完 exe 里还是旧数据）。
- 汇编同理：`Makefile_pc:142-144` 的 `HOST_DATA_DEPS`（`charmap.txt` + 全部头文件 + `data/`、`asm/` 下**所有文件**；2026-10-29 之前只匹配 `*.inc`/`*.s`/`*.json`）让所有 `data/*.o` 依赖这些。踩过的坑：改 `data/maps/<map>/scripts.inc` 的台词、编了 exe、进游戏还是旧台词（`event_scripts.o` 没重编）。
- **Porymap 存的地图/图块二进制也算素材**：`Makefile_pc:146-166` 的 `HOST_ASSET_STAMP`（`find data graphics`，约 2.9 万前置）让所有 .o 依赖整棵素材树。踩过的坑（m04549）：在 Porymap 里改完布局存盘，`data/layouts/<X>/map.bin` 变了、exe 也重新链接了，但 `data/maps.o` 没重编（宿主没有 `tools/scaninc`），进游戏还是旧地图；`src/data/tilesets/metatiles.h:9` 的 `INCBIN_U16("data/tilesets/primary/general/metatiles.bin")` 与 `src/data/tilesets/graphics.h` 的 `INCGFX_U16("...palettes/00.pal", ".gbapal")` 同理。第一次启用戳记会全量重编一次，属预期。
- **地图数据必须重新生成**：`Makefile_pc:145-174` 的 `HOST_MAP_GEN_STAMP` 会在编译前调用 `tools/mapjson`（groups / layouts / map / event_constants），把 `data/maps/*/map.json` 变成各图的 `events.inc`/`header.inc`、`data/layouts/layouts*.inc` 与 `include/constants/{map_groups,layouts,map_event_ids}.h`。没有这一步时，在 Porymap 里改 object 的 sprite（`map.json` 的 `graphics_id`，例如 `OBJ_EVENT_GFX_WINGULL` → `OBJ_EVENT_GFX_SPECIES(WINGULL)`）编完 PC 版看不到变化；GBA 侧一直由 `map_data_rules.mk` + `make generated` 负责。
- 素材改了要清对应缓存：`rm -rf build/assets/graphics/pokemon/<species>`；对象/链接可疑时 `rm -rf build/windows/native/objects build/windows/native/link`。
- 上一版 exe 还在跑会让最后一步 `mv` 报 `Permission denied`（`build_windows.sh` 里的 `taskkill.exe /IM pokeemerald.exe /F` 就是为此）。

### 2.5 操作与运行环境（宿主版）
- 键位：`Z`=A、`X`=B、`Enter`=Start、`\`（反斜杠）=Select、`A`=L、`S`=R、空格按住加速、`+`（含小键盘）循环 1x~9x、`P` / `F1~F12`=手动帧快照。
- 手柄：A/B/Back=Select/Start/LB/RB/十字键/左摇杆。
- 窗口 240x160 逻辑分辨率，可缩放（`SDL_RenderSetLogicalSize`，最近邻）。

---

## 三、宿主适配层结构

| 路径 | 作用 |
|---|---|
| `include/platform.h` | 平台接口总声明（`Platform_Init`、`Platform_InitSave`、`HostMemoryIsReadable/Writable`、`HostLogf`、`HostBiosSelfTest`、`Platform_ObjTileChecksum`、`Platform_RequestFrameDump` …） |
| `src/platform/native_main.c` | `main` → 游戏线程 + 主循环（Pace/Present）、SEH 崩溃过滤器、`HostLogf`、优先级 |
| `src/platform/sdl2.c` | 窗口/渲染器/输入/音频设备、`VBlankIntrWait`、`Platform_PresentFrame`、帧诊断导出 |
| `src/platform/gba_easy_draw.c` | 软件光栅化 240x160（读 `VRAM_`/`OAM`/`PLTT`，含窗口/混合/马赛克/仿射） |
| `src/platform/bios.c` | BIOS 软件实现（`CpuSet`/`CpuFastSet`/压缩/除法…）+ 内存可读可写探测 |
| `src/platform/dma.c` | DMA 通道模拟（`RunDMAs`，NULL 源/目标跳过） |
| `src/platform/flash.c` | 存档：4096B × 32 扇区，写 `.tmp` 再替换 |
| `src/platform/{m4a_host,cgb_audio}.c` | 音乐/音效混音 |
| `src/platform/{rtc,gcn_multiboot}.c` | RTC / 多启动桩 |
| `Makefile_pc` | 宿主构建（GBA 的 `Makefile` 不动） |
| `tools/build_windows.sh` / `tools/package_windows.ps1` | 一键构建 / 打包 |

编译开关：`-DPORTABLE -DPLATFORM_SDL2 -DRENDERER_EASY_DRAW -DMODERN=1 -DTESTING=0 -DEMERALD -std=gnu17`（`Makefile_pc:77`）。

**内存模型**：`PORTABLE` 下 `IWRAM_DATA`/`EWRAM_DATA`/`COMMON_DATA` 是空宏，链接不用 `ld_script_modern.ld` → 真机上 256KB EWRAM / 32KB IWRAM / 32MB ROM 的限制在 PC 版**不存在**（实测 `.bss` 756KB、静态可写 ~1.7MB）。但：

- 新增缓冲区**仍然**包 `#ifdef PORTABLE`，否则 GBA 链接会溢出；
- 静态数组别硬塞 IWRAM（历史上把 `sSoarTilemapBuffer` 放 IWRAM 挤爆系统栈 → 开机闪退）；
- `MAX_SPRITES 64`（`include/sprite.h:5`）/`NUM_TASKS 16`（`include/task.h:8`）/OAM 128 项/`VRAM_SIZE 0x18000` 与 `PLTT_SIZE 0x400`（`include/gba/defines.h:75`、`:73`）与软件渲染器是连体的，扩容要一起改；
- 与 RAM 无关的真限制：`SaveBlock1` 只有 4 扇区 = 15,808 字节（`src/save.c` 有 `STATIC_ASSERT`）、`HEAP_SIZE 0x1C500`（`include/malloc.h`）、16 位 ID。**扩容 RAM 换不来更多内容槽位。**

---

## 四、改代码的约定

1. **双构建**：新代码要能过 GBA 编译器（C11 / gnu17，无 `fmod`、无 `windows.h`、无 `printf`）。
2. **平台相关一律隔离**：`#ifdef PORTABLE ... #endif`，且尽量只在 `src/platform/` 与已标记的钩子点（`HostLogf`、`Platform_*`）出现。
3. **日志**：宿主版不要用 `printf`/`puts`（控制台会刷屏且不落盘）；用 `HostLogf(fmt, ...)`（PORTABLE，写 `crash.log`）。需要 gated 的用 `PE_TRACE`。
4. **内存**：统一 `Alloc`/`AllocZeroed`/`Free`（`include/malloc.h`）。注意 PORTABLE 下 `Free` 是宏，带 `__FILE__:__LINE__` 记录调用点——不要把 `Free` 当函数指针用。
5. **中文文本**：`COMPOUND_STRING(...)` / `_("...")`，注意行长与换行转义（`\n` 写成转义序列，不要真的换行）。
6. **数据表**：`#if`/`#else`/`#endif` 必须成对且**同族字段**（立绘与调色板、形态与性别）放进同一分支，见 8.5 最后两条。
7. **代码风格**：跟随仓库现状（4 空格缩进、`u8/u16/u32/s32`、`TRUE/FALSE`），不要跑 formatter 全文件重排（会污染 diff）。
8. 改完自检：花括号/圆括号/`#if` 深度平衡（末 0、最小 0），全仓库搜一遍 `<<<<<<<`/`>>>>>>>`。
9. **收尾清理（作者确认修好后必做）**：每个 bug 经作者确认解决后，把排查过程中加入的、**与修复无关**的改动全部还原——临时的 `HostLogf` 诊断日志、只为它们加的 `#include "platform.h"`、探测用的字段/函数/计数、「先加个日志看看」的签名或链接性改动（例：把 `static` 改成外部链接）、一次性开关与临时文件。只保留真正改变行为的最小修复。
   做法：`git diff <修复前的提交> --stat` 列出本次全部改动，逐条问「删掉它，这个 bug 还是修好的吗」，答案是不会坏就删；清理后 `git grep -n "HostLogf" -- <改过的文件>` 应为 0（该文件原本就有的除外），花括号/`#if` 平衡复验，并按 9.4 同步到另一条分支。作者没确认之前不要提前删——诊断是下一轮取证的依据。

---

## 五、常见任务速查

- **加 TM**：唯一映射源是 `include/constants/tms_hms.h` 的 `FOREACH_TM`；`STEM` 编号 = 位置，改说明改 `src/data/items.h` 的 `[ITEM_TMxx]`；可学性由 `tools/learnset_helpers/make_teachables.py` 生成 `src/data/pokemon/teachable_learnsets.h`（`Makefile` 会自动重建）。
- **道具说明翻译**：`python_tools/translate_items.py` 重跑时，xlsx 里"单独道具说明"为空则**跳过 description**，不会覆盖手写说明。
- **加技能机/招式**：招式数据 `src/data/moves_info.h`；`MOVE_*` 常量 `include/constants/moves.h`。
- **地图**：Porymap（`data/maps/`、`data/layouts/`）；改完 layout 要重新编译 `data/layouts/*/border.bin`。
- **存档结构**：`include/save.h`/`src/save.c`，动结构体先看 `STATIC_ASSERT`。

---

## 六、调试与取证（PC 版）

### 6.1 崩溃日志 `crash.log`
`src/platform/native_main.c` 的 `HostCrashFilter`（SEH）写：`code`/`addr`/`eip`/`esp`/`ebp`、`fault=<数据地址> access=<read/write/exec>`、6 个通用寄存器、`stack:` 前 8 个 DWORD、最多 16 层 EBP 帧链、**eip 对齐处 64 字节机器码** `code@xxxxxxxx:`、以及 `HeapLogCrashState()` 的堆快照。
文件为**追加**写，>2MB 时轮转成 `crash.log.1`（所以"上次的崩溃"也在，看 `[run] start` 分段即可）。

### 6.2 帧快照
`P` 键（或 `F1~F12`）抓一帧：写 `frame_N.ppm`（240x160 P6）+ 在 crash.log 打一批读数（DISPCNT/BLDCNT/WIN*/BGCNT/BGOFS/PLTT 各段、tilemap 采样、probe 行"每个像素由哪层画"、OAM 列表、堆汇总）。
- 自动快照**默认关闭**，`PE_AUTO_DUMP=1` 才开（三类：地图载入、野外 row80 空带、Navigator）；
- `PE_DUMP_BINS=1` 会额外导 `winners/bgmaps/pltt/vram/oam_*.bin`（约 150KB，**会让该帧卡 0.5~1 秒**，测卡顿时别开）。

### 6.3 帧时序 `frame_trace_<pid>.log`
每进程独立，最多 20000 帧。列：`n dtUs hofs1 vofs1 camX camY sprX sprY posX posY keys cpuUs`。用来把"卡顿"落到具体按键/坐标/帧间隔。

### 6.4 环境变量
| 变量 | 作用 |
|---|---|
| `PE_TRACE=1` | 打开渲染/事件/BIOS 的空指针告警与 slow loop 打印 |
| `PE_AUTO_DUMP=1` | 打开自动帧快照 |
| `PE_DUMP_BINS=1` | 快照时导出 .bin（慢） |
| `PE_PRIO=1` | 游戏线程/主线程提到 `THREAD_PRIORITY_HIGHEST` |
| `PE_PACE_SNAP=n` | 落后超过 n 帧才重新对齐节拍（默认 2） |
| `PE_VSYNC=0/1` | 垂直同步开关 |
| `PE_AUDIO_SAMPLES=n` / `PE_AUDIO_PREFILL=0/1` | 音频缓冲大小 / 预填 |

### 6.5 健康基线（正常时应看到）
```
[log] host cpus=6 timerFreq=10000000 pe_prio=0
[log] heap: init start=022ca9c0 size=115968
[log] selftest fill: fastfill16 03FF/03FF big 03FF/03FF (want 03FF) ...
[log] perf n=60 logicAvg=0.1ms logicMax=5ms waitAvg=16.7ms presentAvg=3ms audioQ=0KB
```
`selftest` 不对 → BIOS 填充坏了；`heap: Alloc saw bad block` / `Free (double free)` / `heap: crash snapshot list=BROKEN` → 堆被写坏，看前一条 `heap op #n ... loc=文件:行`。

### 6.6 PE32 符号化（拿到崩溃地址后必做）
1. 解析头：`e_lfanew@0x3C`；COFF `nSections@+6`、`PointerToSymbolTable@+12`、`NumberOfSymbols@+16`、`SizeOfOptionalHeader@+20`；`ImageBase = optStart+28`；段表 = `optStart+optSize`，每 40 字节取 `Name/VA@+12/rawPtr@+20`。
2. 符号 18 字节：`name@0/value@8/sectionNumber@12/type@14/storageClass@16/aux@17`；GNU 长名字是"前 4 字节 0 → 后 4 字节字符串表偏移"；名字常带前导 `_`。
3. **必须跳过段符号**（`storageClass===3 && value===0 && 名字属于段名`），否则静态函数解不出来。
4. 最近符号只是"候选"：确认调用者要在返回地址前 5~7 字节找 `0xE8` 反解 call 目标；`-O2` 会省 frame pointer，EBP 链可能缺帧。
5. 有机器码时直接手写反汇编（`0xc0000094` = 除零 `idiv/div`、`0xc0000005` + `fault=` = 野指针）。

### 6.7 与 GBA 对照
- mGBA 截图默认落在 **ROM 同目录**、文件名 `pokeemerald-N.png`（原生 240x160 RGB 索引）。想固定位置：mGBA「设置 → 路径 → 截图路径」。
- 对照流程：同一存档同一画面，先 mGBA 截图，再 PC 版按 `P` 抓帧，用 5bit 量化逐像素比对；差在"哪一层"看 probe 行与 `winners`。

---

## 七、性能与节拍

- 主循环：`Platform_PumpEvents()` → 若 `Platform_IsFramePending()` → `PaceHostFrame()` → `Platform_PresentPendingFrame()`（`SDL_RenderPresent` 之后才跑 VBlank 回调、放行游戏线程）。
- `PaceHostFrame` **不跳帧**：落后就连续快跑追 deadline。典型"补帧签名"：4 帧 16.7ms → 1 帧 ~100ms → 7 帧 5~6ms 循环（平均帧率仍 58fps，但观感是"卡一下再快放"）。
- 判读三段：`pump`（取消息）/ `present`（呈现）/ `logic`（游戏线程）/ `pace`。
  - `pump` 占大头 → 事件/输入层（曾定位到中文输入法 `SDL_TEXTEDITING`，已用 `SDL_StopTextInput()` 修；见 `src/platform/sdl2.c` 的 `Platform_Init` 与 `FOCUS_GAINED` 分支）；
  - `present` 尖峰 → 窗口被遮挡/交换链阻塞；
  - `logic` 尖峰 → 游戏逻辑（地图载入等，正常）；
  - `pace want=... got=...` → `SDL_Delay` 睡过头。
- 诊断文件写入本身会造成 0.5~1 秒卡顿（杀毒/磁盘），测卡顿时不要开 `PE_DUMP_BINS`。

---

## 八、multiplatform 与「先对照」规则（硬性）

### 8.1 规则
凡是 **PC 宿主版 / `PORTABLE` / `src/platform/`** 的修复，**先对照参考分支**再动手：

- 仓库：`https://github.com/Sum-Light/pokeemerald-expansion`
- 提交：`d36f819f0b0454138e21991aa58a6cb806718e29`（expansion **1.16.4**；其父提交、即本项目中文化分支基线：`42ae60059ae64800376c83d5db210f880aa2689d`）
- 参考它的 `Makefile_pc`、`src/platform/`、`include/platform*`、`test/native/`、`tools/build_wsl_fast.ps1`、`docs/multiplatform-porting.md`，以及 61 个 `src/*.c` / 13 个 `include/*.h` 里的 `#ifdef PORTABLE` 段。

### 8.2 判断口径
| 差异落点 | 结论 | 做法 |
|---|---|---|
| 只在 `#ifdef PORTABLE` 段 | 移植层 | 照参考改（本仓库版本是 1.17.2，注意 API 演进：`InitWindows` 返回 bool、`gDecompressionBuffer` 已移除、`ScriptContext2_Enable`→`ScriptContext_Enable`、`LoadCompressedSpritePalette` 已移除…） |
| 落在非 PORTABLE 段 | 游戏层 | **不要照搬参考**，按游戏逻辑判断；参考分支是旧版（1.16.4/旧 pokeemerald），会带回旧代码 |
| 两边逐行一致却仍有 bug | 上游 latent bug | 按"GBA 静默、宿主必崩"的思路补守卫（见 8.5），并在提交说明里注明 |

### 8.3 取文件（网络受限环境的唯一通道）
本机 hosts 把 `github.com` / `raw.githubusercontent.com` / `api.github.com` 指向 127.0.0.1，`git clone` 与 `wsl` 调用都被沙箱拒绝。可用通道：

- `run_code` 里的 **Node `fetch`**，走 `https://gh-proxy.com/` + 原始 URL：
  `https://gh-proxy.com/https://raw.githubusercontent.com/<owner>/<repo>/<sha>/<path>`
- **不要用 `web_fetch` 下源码**：硬截断 100,000 字符，曾把 13 个文件截断导致三路合并产出乱码。

参考副本可放 `_pc_host_ref/<rel>`（该提交 70 个完整文件）、`_pc_host_ref/_base/<rel>`（父提交）、`_pc_host_ref/_orig/<rel>`（本项目改造前）。**这三个目录目前不在仓库里**（已被清理），需要时按上面的 URL 重新拉。

### 8.4 移植层已知坑（都是实际踩过的）

1. **宿主没有 MMU**：野指针在 GBA 上读总线垃圾、在 Windows 上直接 `0xc0000005`。
   - `Alloc` 在 PORTABLE 下会 `memset(pos->data, 0, size)`（`src/malloc.c`）；
   - `HostMemoryIsReadable/Writable()`（`src/platform/bios.c`，VirtualQuery 分区检查）用于 `CpuSet/CpuFastSet`、`LoadPalette*()`、`DoLoadSpritePalette()`、`CpuSet` 的兜底；
   - 兜底策略是"不崩"而不是"正确"：调色板填 0、精灵调色板填 0、地图层保留。
2. **`CPU_FILL` 把"值"当指针**：`include/gba/macro.h` 的 `CpuFastFill(x, ...)` 把 `x` 塞进栈变量再把 `&x` 当 src。所以 fill 分支**只能检查 2/4 字节可读**，不能按目标长度查——曾经按长度查导致 `CpuFastFill16(MAPGRID_UNDEFINED, ...)` 全写 0，备份地图边界余量变 0 → **地图边缘整条黑带、换图/重启都不恢复**（`src/platform/bios.c:263`、`:330`，并有 `HostBiosSelfTest()` 基线）。
3. **只有 Faded 调色板进显存**：`TransferPlttBuffer()` 搬的是 `gPlttBufferFaded`。只写 `gPlttBufferUnfaded` 的代码（如 pokenav 的 `Pokenav_AllocAndLoadPalettes`）在"没有下一次淡入"时会一直用旧颜色 → 第二次进菜单颜色错。修法：写入时同步 `CpuCopy16(data, &gPlttBufferFaded[index], ...)`。
4. **精灵调色板槽 / 图块表会互串**：`FreeSpritePaletteByTag(0)` / `FreeSpriteTilesByTag(0)` 之后，槽位与图块区可能被别人抢走，而精灵还指着旧值 → 颜色错、图块变乱码（"精灵""对战"字样跑到图标框里）。修法参照 `src/pokenav_main_menu.c:590` `EnsureSpinningPokenavResources()`：每帧/使用前校验 `IndexOfSpritePaletteTag` 与 `GetSpriteTileStartByTag`，不符就重建并改 `sprite->sheetTileStart` + `SetSpriteSheetFrameTileNum()`。
5. **MinGW 头文件撞名**：`winuser.h` 也有 `ScrollWindow`。`src/platform/sdl2.c` 用 `#define ScrollWindow PokeemeraldHostScrollWindow` 把 `#include "window.h"` 夹住，再 `#undef`。
6. **`InitWindows` 语义变了**：expansion 里传入指针会按 `-1` 扫描到 `.bg==0xFF`，必须用 `DUMMY_WIN_TEMPLATE` 结尾；返回 `bool` 而不是窗口 ID。
7. **C 枚举体被 GNU as 当成绝对符号**：`data/*.s` 会 `#include "constants/pokemon.h"`，枚举体每行变成 `SPIN_CW_SHORT,` → 汇编器生成 `SHN_ABS` 符号。ARM `ld` 容忍重复，MinGW `ld` 报 `multiple definition of SPIN_CW_SHORT`。修法：`Makefile_pc:62` `HOST_LD_DEDUP ?= --allow-multiple-definition`，并只在 **data 对象的中间链接**里用（`Makefile_pc:372-382` 的 `HOST_LINK_DATA_OBJ`），C 源的重复函数仍由最终链接兜住。
8. **改头文件必须重编对应 .o**（`Makefile_pc:101-102` 的 `HOST_HEADERS`）。GBA 侧由 `scaninc` 生成 `.d`，宿主侧没有，历史上因此"改了、编了、没生效"。
9. **一帧内的时序**：`Platform_PresentFrame()` 先 `DrawFrame()`（读 VRAM/OAM/PLTT），**之后**才 `REG_VCOUNT=160; RunDMAs(DMA_VBLANK); gIntrTable[4]()`（即 `VBlankCB_Field`）。所以一帧里 BG 滚动与 OAM 同属"上一逻辑帧"，二者一致，不会错位。
10. **DMA**：`src/platform/dma.c` 的 `RunDMAs` 遇到 NULL src/dst 会跳过并关通道（曾经在背包/箱子界面崩在 `_RunDMAs+0xc1`）；`DmaSet` 结尾立即 `RunDMAs(DMA_NOW)`。

### 8.5 已知上游 latent bug（GBA 静默、宿主必崩；修法都已落盘）

| 症状 | 站点 | 修法 |
|---|---|---|
| 战斗闪退 `0xc0000094`，双打 2VS1 | `src/battle_ai_util.c:426` `GetHealthPercentage()` 的 `hp*100/maxHP`，空战斗位 `maxHP==0` | `maxHP==0` 返回 100（空位按满血）；同型加固 `CanTargetFaintAiWithMod`、`battle_ai_main.c` 的 `EFFECT_STRENGTH_SAP` |
| 用无限之笛闪退 `0xc0000094` | `src/soar.c:401` `(1<<16)/(currScanline-32)`，扫描线 32 | `currScanline == 32` 时令 `lam = 0`（复现 GBA 语义） |
| 其它除零 | `battle_util.c` 的 `EFFECT_POWER_BASED_ON_USER_HP`/`TARGET_HP`/`EFFECT_ELECTRO_BALL`、`battle_script_commands.c` 的 `Cmd_hpthresholds`/`hpthresholds2`、`battle_arena.c`、`battle_interface.c`、`pokemon.c` 形态变化 | 逐处判 0（`SAFE_DIV` 在 `include/global.h:88` 仅 UBFIX 下带判 0，别指望它） |
| 战斗触发代欧奇希斯特性闪退 | `playanimation` 宏默认 `.4byte 0`，`PlayAnimation()` 无条件 `*argPtr` | `src/battle_script_commands.c:3076` 用 `animArg = argPtr ? *argPtr : 0`；`Cmd_playanimation_var` 判 `animIdPtr == NULL` |
| 招式详情/技能机学习闪退 | `sSpriteAnimTable_CategoryIcons[0] == NULL`，`CreateSpriteAt` 里 `SetSpriteSheetFrameTileNum()` 无条件读 `anims[0][0]` | `src/sprite.c:921/962/1448` 三处判 `anims == NULL || anims[animNum] == NULL`（`BeginAnim` 里要 `animBeginning = FALSE` 再 return，否则死循环） |
| 金属爪/龙爪闪退（`fault=0xFFFFxxxx`） | `s16 data[8]` 存指针两半，`battle_anim_normal.c` 拼地址时低半字被符号扩展（`&gSpriteCoordOffsetY=0x0236B3AA`） | `GetShakeTargetPtr()`（:945）两次 `(u16)` + `(u32)`；`battle_anim_mons.c:338` 同步加固。上游先例：`pokemon_animation.c:487-496` 的 `ANIM_SPRITE`（`MODERN` 下已修） |
| 战斗中使用强化道具闪退 | `CannotUseItemsInBattle(item, NULL)` → `GetMonData(NULL, MON_DATA_HP)` 读 `NULL+0x56` | `src/item_use.c:1208` 从 `gBattlerPartyIndexes[battlerTarget]` 取回出战成员；`src/pokemon.c` 的 `GetMonData3`/`SetMonData` 判 NULL 兜底 |
| 图鉴进详情闪退 | `UpdateSelectedMonSpriteId()` 先 `gSprites[spriteId]` 后判 `spriteId != 0xFFFF`（空槽就是 0xFFFF → 读 `gSprites[65535]`） | `src/pokedex.c:2610` 改成 `spriteId >= MAX_SPRITES` 先 continue、找不到返回 FALSE，两个调用点 :1537/:1758 处理 FALSE；顺带修 `SpriteCB_Scrollbar` 的 `/(count-1)` |
| 抓宠/命名相关偶发闪退（堆链表被写坏） | `AllocInternal` 解引用 `pos == NULL` | 已加整表校验 + 96 条 `heap op` 环形日志 + 双重释放拦截 + `HeapLogCrashState()`；另修 `DestroyMonSpritesGfxManager()` 不清 `sMonSpritesGfxManagers[managerId]`（`src/pokemon.c:5884`） |
| Navigator 选项条错色 | pokenav 只写 `gPlttBufferUnfaded` | 见 8.4 第 3 条 |
| Navigator 右上角图标变绿/变乱码 | `tag 0` 的调色板槽与图块表被选项条抢走 | 见 8.4 第 4 条 |
| 搭档战（`multi_2_vs_2`）选了后三只，进双打的却是队伍前三只（**不崩，逻辑错，GBA 同样中招**） | `src/battle_util.c` 的 `AreMultiPartiesFullTeams()` 用 `gBattleTypeFlags & BATTLE_TYPE_TRAINER` 判断"这场是不是训练家战"，但 `multi_do` 执行时战斗还没开始，该变量是**上一场**的残留（野战后被置 0：`src/battle_setup.c:413`/`:555`）→ 返回 TRUE → 脚本跳过 `ReducePlayerPartyToSelectedMons()` | 改成同时看 `TRAINER_BATTLE_PARAM.opponentA/B`（`SetMultiTrainerBattle()` 已写入）并做 id 合法性判断（`!= TRAINER_NONE`、`!= TRAINER_LINK_OPPONENT`、`< TRAINERS_COUNT`，2VS1 的 `opponentB = 0xFFFF` 也要挡住），difficulty 用 `GetTrainerDifficultyLevel()` 回退。诊断：`[log] multi parties: gBattleTypeFlags=… trainerA=… trainerB=… validA=… validB=…` |
| 图鉴里雌性宝可梦整只变色 | `src/data/graphics/pokemon.h` 的雌性立绘（`anim_frontf.png`/`backf.png`）**不在** `P_GBA_STYLE_SPECIES_GFX` 分支里，而雄性在 → 旧画索引 + GBA 重配色板 | 62 条雌性 front / 58 条 back（仅当雄性 GBA 立绘存在）改成 `#if P_GBA_STYLE_SPECIES_GFX` 用 `*_gba.png`，`#else` 用旧画。**不要试图自动重配色**：96 个物种里没有一对是纯排列，新旧立绘像素对应率 25~42% |
| 隐形物件挡路、按 A 触发别的地图的剧情（**不崩，GBA 同样中招**） | 跨图时旧地图的 object event 残留在 `gObjectEvents` 里（`mapGroup/mapNum` 仍是旧图、本图没有对应模板），而 `GetObjectEventIdByPosition()` 只判 `active`+坐标+高度，不看 `invisible` 与地图 → 隐形、挡路、能对话 | `src/field_control_avatar.c:398-409` 的对话判定加 `invisible` 与地图一致性；`src/load_save.c:243-250` 的 `LoadObjectEvents()` 丢弃别图 `active`；新增 `RemoveForeignObjectEvents()`（`src/event_object_movement.c:1715`），在 `InitObjectEventsLocal`/`InitObjectEventsReturnToField` 以及 `OverworldBasic()` 每 30 帧调用（克隆物件在本项目数据里 0 处，安全）；`GetObjectEventScriptPointerByLocalIdAndMap()` 判空 |

### 8.6 拿到一份新 crash.log 的排查顺序（SOP）
1. 按 `[run] start` 找到**最后一段**，确认这段里有几个 `[crash]`（老崩溃会被追加保留，别误判）。
2. 核对"作者测的是不是含修复的 exe"：比较 `pokeemerald.exe` 的 mtime / crash 时间 / 改动文件 mtime（踩过好几次）。
3. 看 `code`：`0xc0000005`=访问违例（看 `fault=` 地址：0/5/0xFFFFxxxx 各有含义）、`0xc0000094`=除零、`0xc00000fd`=栈溢出。
4. 符号化 `eip` + 帧链（6.6），必要时用 `code@` 的机器码手写反汇编。
5. 看 `heap: crash snapshot list=` 是否 ok，决定是"堆坏"还是"逻辑野指针"。
6. 对照 8.5 的表先猜；命中就套修法，不命中再读源码。
7. 修完复检：花括号/圆括号/`#if` 平衡、全仓库无冲突标记、相关文件行数变化合理。

---

## 九、分支模型与同步（2026-10-27 起）

### 9.1 模型
- `ny2`：GBA 改版开发分支，**游戏内容唯一真源**（地图、文本、数值、脚本、精灵、战斗逻辑）。
- `multiplatform`：`ny2` + 宿主适配层 —— `src/platform/`、`include/platform*`、`Makefile_pc`、`tools/*windows*`、`test/native/`，以及各 `src/*.c`、`include/*.h` 里的 `#ifdef PORTABLE` 段。
- 同步方向：**只有 ny2 → multiplatform**。不要直接在 multiplatform 上改游戏内容；宿主层修出的游戏层 bug 要回灌 ny2（见 9.4）。

### 9.2 出 PC 包前（每次都是这三步）
```bash
git status                      # ny2 上必须先 commit，工作区要干净
git checkout multiplatform
git merge ny2                   # 冲突口径见 9.3
bash tools/build_windows.sh
git checkout ny2
```

### 9.3 冲突口径（2026-10-27 首次合并实测）
- 冲突几乎固定落在 5 个文件：`.gitignore`、`src/task.c`、`src/battle_util.c`、`src/pokemon_storage_system.c`、`src/pokenav_main_menu.c`。原因是 ny2 版把 `#ifdef PORTABLE` 段整段删掉了（GBA 分支没有宿主头），multiplatform 版保留。
- 这 5 处**一律取 multiplatform 一侧**：`git checkout --ours -- <file>`。那一侧 = ny2 内容 + 宿主钩子，是超集。实测：除 `src/field_control_avatar.c` 多一行 `#include "task.h"`（ny2 自己的修复，保留）之外，合并后的树与合并前的 multiplatform 完全一致。
- 冲突若落在**别的**文件，说明 ny2 改到了宿主钩子附近，必须人工合并：**ny2 的逻辑改动与 multiplatform 的 `#ifdef PORTABLE` 段都要留**（通常两边各加几行），不要整文件取一边。
- 合并后自查：`git diff ny2 --stat` 应只剩宿主层文件；`PORTABLE` 的命中数不应减少。

### 9.4 反向：PC 侧的游戏层修复要回灌 ny2
- 在 multiplatform 上修的游戏层 bug（8.5 表）必须同步到 ny2，否则下次 merge 时 ny2 的旧代码会把它"合回去"——merge 以 ny2 内容为准。
- 做法：在 ny2 上 `git checkout multiplatform -- <files>`，再手工剥掉该文件里的 `#ifdef PORTABLE` / `HostLogf` / `platform.h` 段，最后**在 ny2 上 `make -j8`** 验证（`host-audit` 查不出 GBA 侧问题）。

### 9.5 本文件（AGENTS.md）
- 两条分支都要有。ny2 上若是未跟踪文件，切换分支时 git 会拒绝覆盖，所以**在 ny2 上 commit 一次**，之后合并自动带走。
- 改本文件后两边内容保持一致（在 ny2 改、merge 过去即可）。

## 十、禁止事项

1. 不要为 PC 版修改根 `Makefile`、不要改 `ld_script_modern.ld`、不要动 GBA 的 section 属性。
2. 不要把宿主专有头（`windows.h`、`SDL.h`）引进非 `PORTABLE` 段。
3. 不要在 `src/` 里用 `printf`/`puts`/`fprintf(stderr,...)` 做常驻日志（用 `HostLogf`，且注意量）。
4. 不要用 `web_fetch` 下载源码文件；不要试图 `git clone`（hosts 拦截 + 沙箱拒绝）。
5. 不要把大数组放静态 IWRAM（会挤爆系统栈导致开机闪退）。
6. 不要提交 `build/`、`dist/`、`*.sav`、`crash.log*`、`frame_trace_*.log`、`_tmp_*`。
7. 不要在没有对照参考分支的情况下"重写"移植层文件（`sdl2.c`/`gba_easy_draw.c`/`bios.c`/`dma.c`/`flash.c` 等）——它们的逻辑与参考分支是逐行对齐的。
8. 不要假设存在本文件之外的隐藏约定；有疑问先读代码，再问作者。
9. 不要把排查用的临时诊断（日志、探测字段、一次性开关）留在**已确认完成**的修复里——按第四节第 9 条清理干净再交付。诊断可以留，但要在作者确认之后立刻收尾。
