param([string]$Toolchain = 'F:/Arm GNU Toolchain arm-none-eabi/14.3 rel1/bin')
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
$cc = Join-Path $Toolchain 'arm-none-eabi-gcc.exe'
$size = Join-Path $Toolchain 'arm-none-eabi-size.exe'
$nm = Join-Path $Toolchain 'arm-none-eabi-nm.exe'
$configs = [ordered]@{
    default = @()
    minimal = @('-DGBA_ENABLE_SAVE=0','-DGBA_ENABLE_RTC=0','-DGBA_BIOS_HLE=0','-DGBA_RAM_LACK=1')
    full = @('-DGBA_ENABLE_SOUND=1','-DGBA_USE_FS=1','-DGBA_ENABLE_SAVE_STATE=1','-DGBA_COLOR_DEPTH=32')
    stream = @('-DGBA_ENABLE_SOUND=1','-DGBA_USE_FS=1','-DGBA_ENABLE_SAVE_STATE=1','-DGBA_ROM_STREAM=1','-DGBA_RAM_LACK=1')
    swap = @('-DGBA_COLOR_SWAP=1','-DGBA_FRAME_SKIP=2','-DGBA_ROM_STREAM=1','-DGBA_COLOR_DEPTH=16')
    small_save = @('-DGBA_SAVE_CAPACITY=512')
}
$report = @()
foreach ($name in $configs.Keys) {
    $out = "test/out/cross/$name"
    New-Item -ItemType Directory -Force $out | Out-Null
    $flags = @('-std=c11','-Os','-Wall','-Wextra','-Werror','-mcpu=cortex-m4','-mthumb','-ffunction-sections','-fdata-sections','-fstack-usage','-fno-builtin','-Iinc','-Isrc','-Iport') + $configs[$name]
    $objects = @()
    foreach ($source in @(Get-ChildItem src/*.c) + @(Get-Item test/mcu_smoke.c)) {
        $object = "$out/$($source.BaseName).o"
        & $cc @flags -c $source.FullName -o $object
        if ($LASTEXITCODE) { throw "ARM compile failed: $name/$source" }
        $objects += $object
    }
    & $cc -mcpu=cortex-m4 -mthumb -nostdlib '-Wl,--gc-sections' '-Wl,-T,test/mcu_smoke.ld' @objects -lgcc -o "$out/smoke.elf"
    if ($LASTEXITCODE) { throw "ARM link failed: $name" }
    $symbols = & $nm "$out/smoke.elf"
    if ($symbols -match 'gba_test_instruction|gba_profile_ticks|\b(malloc|calloc|realloc|free)\b') { throw "production probes or libc heap found: $name" }
    $sizes = (& $size "$out/smoke.elf") -join "`n"
    $stack = @((Get-Content "$out/*.su") | ForEach-Object {
        $parts = $_ -split "`t"
        [pscustomobject]@{Function=$parts[0];Bytes=[int]$parts[1];Kind=$parts[2]}
    }) | Sort-Object Bytes -Descending | Select-Object -First 8
    $report += [pscustomobject]@{Config=$name;Size=$sizes;LargestFunctions=$stack}
    Write-Output "$name : $sizes"
}
$report | ConvertTo-Json -Depth 5 | Set-Content test/out/cross/results.json
