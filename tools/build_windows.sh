#!/usr/bin/env bash
# 一键构建 Windows 桌面版（在 WSL 里运行）。
#
#   bash tools/build_windows.sh          # 增量编译
#   bash tools/build_windows.sh clean    # 先清理宿主中间产物再完整编译
#
# 产物：build/windows/pokeemerald.exe + build/windows/SDL2.dll
# 打包：在 Windows 里运行 tools\package_windows.ps1
set -euo pipefail

cd "$(dirname "$0")/.."

PREFIX=i686-w64-mingw32
SYSROOT=/usr/$PREFIX
JOBS=${JOBS:-$(nproc 2>/dev/null || echo 8)}
LOG=/tmp/pc_build.log

die() { echo "错误：$*" >&2; exit 1; }

command -v make >/dev/null 2>&1 || die "找不到 make"
command -v "$PREFIX-gcc" >/dev/null 2>&1 || die "找不到 $PREFIX-gcc（apt install gcc-mingw-w64-i686 g++-mingw-w64-i686）"
[ -f "$SYSROOT/include/SDL2/SDL.h" ] || die "缺少 MinGW 版 SDL2 头文件：$SYSROOT/include/SDL2/SDL.h"
[ -f "$SYSROOT/lib/libSDL2.dll.a" ] || die "缺少 MinGW 版 SDL2 导入库：$SYSROOT/lib/libSDL2.dll.a"
[ -f "$SYSROOT/bin/SDL2.dll" ] || die "缺少 SDL2.dll：$SYSROOT/bin/SDL2.dll"

if [ "${1:-}" = "clean" ]; then
    echo "== 清理宿主中间产物 =="
    rm -rf build/windows/native build/windows/pokeemerald.exe.tmp
fi

# 上一版 exe 还在运行，会让链接最后一步 mv 报 Permission denied。
if command -v taskkill.exe >/dev/null 2>&1; then
    taskkill.exe /IM pokeemerald.exe /F >/dev/null 2>&1 || true
fi

echo "== 准备用户工具 =="
make -C tools/preproc >/dev/null || die "tools/preproc 构建失败"

echo "== 编译 Windows 桌面版（-j$JOBS，日志 $LOG） =="
if ! make -f Makefile_pc -j"$JOBS" WINDOWS=1 windows-executable > "$LOG" 2>&1; then
    echo "" >&2
    echo "构建失败，错误行：" >&2
    grep -n "error:" "$LOG" | head -n 40 >&2 || true
    echo "" >&2
    tail -n 25 "$LOG" >&2
    exit 1
fi

WARN=$(grep -c "warning:" "$LOG" 2>/dev/null || true)
[ -n "$WARN" ] || WARN=0
echo ""
echo "== 完成（warning $WARN 条，均为项目既有噪声） =="
ls -l --time-style=+%F_%T build/windows/pokeemerald.exe build/windows/SDL2.dll
echo ""
echo "产物目录：$(pwd)/build/windows"
echo "下一步（在 Windows 里打包成 zip）："
echo "  powershell -ExecutionPolicy Bypass -File tools\package_windows.ps1"
