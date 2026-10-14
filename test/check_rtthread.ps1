param([string]$RTThread = 'F:/code/codeup/rt-thread',
      [string]$Toolchain = 'F:/Arm GNU Toolchain arm-none-eabi/14.3 rel1/bin',
      [switch]$NoHeap)
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
$out = 'test/out/rtthread'
New-Item -ItemType Directory -Force $out | Out-Null
@'
#pragma once
#define RT_NAME_MAX 16
#define RT_ALIGN_SIZE 4
#define RT_THREAD_PRIORITY_MAX 32
#define RT_TICK_PER_SECOND 1000
#define RT_USING_HEAP
#define RT_USING_CONSOLE
#define RT_CONSOLEBUF_SIZE 128
'@ | Set-Content "$out/rtconfig.h"
if ($NoHeap) {
    (Get-Content "$out/rtconfig.h") | Where-Object { $_ -ne '#define RT_USING_HEAP' } | Set-Content "$out/rtconfig-noheap.h"
    Move-Item -LiteralPath "$out/rtconfig-noheap.h" -Destination "$out/rtconfig.h" -Force
}
$cc = Join-Path $Toolchain 'arm-none-eabi-gcc.exe'
$flags = @('-std=c11','-Os','-mcpu=cortex-m4','-mthumb','-Wall','-Wextra','-Werror',
           '-Iinc','-Isrc','-Irtthread',"-I$out","-I$RTThread/include")
foreach ($source in @(Get-ChildItem src/*.c) + @(Get-Item rtthread/gba_port.c,rtthread/example_static.c)) {
    & $cc @flags -c $source.FullName -o "$out/$($source.BaseName).o"
    if ($LASTEXITCODE) { throw "RT-Thread interface compile failed: $source" }
}
Write-Output "RT-Thread headers: $((git -C $RTThread rev-parse HEAD)); compile only, no BSP link or execution."
