param([string]$Toolchain = 'F:/Arm GNU Toolchain arm-none-eabi/14.3 rel1/bin',
      [string]$Python = 'D:/Program Files/Python313/python.exe', [switch]$Strict)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root
$suite = 'tools/reference/mgba-suite'
$libgba = 'tools/reference/libgba'
if ((git -C $suite rev-parse HEAD) -ne 'e6942030d25ffe3ba76c72b73a86da073ec857cc') { throw 'suite revision mismatch' }
if ((git -C $libgba rev-parse HEAD) -ne 'f0e44ce9ed57e1d21f148443223408fddeacfb9d') { throw 'libgba revision mismatch' }
$out = 'test/out/video'
New-Item -ItemType Directory -Force $out | Out-Null
& $Python test/video_assets.py "$suite/gfx/raw/degenerateObjTransform.bmp" "$out/degenerateObjTransform.h"
if ($LASTEXITCODE) { throw 'video fixture encoding failed' }
# Interactive navigation assets are unused and removed by section GC.
foreach ($name in @('font','expected','actual')) {
    "#pragma once`nextern const unsigned char ${name}Tiles[];`n#define ${name}TilesLen 0" | Set-Content "$out/$name.h"
}
$source = Get-Content "$suite/src/video.c" -Raw
$source += @'

bool gba_video_case(unsigned index, bool expected, bool refresh) {
    return expected ? videoTests[index].expected(refresh) : videoTests[index].actual(refresh);
}
'@
[IO.File]::WriteAllText((Join-Path $root "$out/video.c"), $source)
$cc = Join-Path $Toolchain 'arm-none-eabi-gcc.exe'
$flags = @('-mcpu=arm7tdmi','-mthumb','-mthumb-interwork','-O3','-fomit-frame-pointer','-ffast-math',
    '-ffunction-sections','-fdata-sections',"-I$suite/include","-I$libgba/include","-I$out")
$objects = @()
foreach ($path in @("$out/video.c",'test/suite_start.s',"$libgba/src/interrupt.c","$libgba/src/InterruptDispatcher.s",
    "$libgba/src/IntrWait.c","$libgba/src/CpuSet.c","$libgba/src/Compression.c")) {
    $object = "$out/$([IO.Path]::GetFileNameWithoutExtension($path)).o"
    & $cc @flags -x $(if ($path.EndsWith('.s')) {'assembler-with-cpp'} else {'c'}) -c $path -o $object
    if ($LASTEXITCODE) { throw "video compile failed: $path" }
    $objects += $object
}
$names = @('Basic Mode 3','Basic Mode 4','Degenerate OBJ transforms','Layer toggle','Layer toggle 2',
    'OAM Update Delay','Window offscreen reset')
$results = @()
for ($index = 0; $index -lt $names.Count; $index++) {
    $pixels = @{}
    $runs = @{}
    foreach ($kind in @('expected','actual')) {
        $isExpected = if ($kind -eq 'expected') {1} else {0}
        & $cc @flags "-DGBA_VIDEO_CASE=$index" "-DGBA_VIDEO_EXPECTED=$isExpected" -c test/video_adapter.c -o "$out/main.o"
        if ($LASTEXITCODE) { throw 'video adapter compile failed' }
        $base = "$out/$index-$kind"
        & $cc -mcpu=arm7tdmi -mthumb -mthumb-interwork -nostartfiles --specs=nosys.specs '-Wl,--gc-sections' '-Wl,-T,test/suite.ld' @objects "$out/main.o" -lc -lgcc -o "$base.elf"
        if ($LASTEXITCODE) { throw 'video ROM link failed' }
        & (Join-Path $Toolchain 'arm-none-eabi-objcopy.exe') -O binary "$base.elf" "$base.gba"
        if ($LASTEXITCODE) { throw 'video ROM encoding failed' }
        $output = (& build/windows/x64/release/gba-tests-full.exe --rom "$base.gba" 100) -join "`n"
        $runs[$kind] = [pscustomobject]@{Complete=($LASTEXITCODE -eq 0 -and $output -match 'video_magic=5649444f');Output=$output}
        Copy-Item test/out/last.ppm "$base.ppm" -Force
        $pixels[$kind] = [IO.File]::ReadAllBytes((Join-Path $root "$base.ppm"))
    }
    $a = $pixels.actual
    $b = $pixels.expected
    $header = [Text.Encoding]::ASCII.GetBytes("P6`n240 160`n255`n")
    if ($a.Length -ne $header.Length + 240 * 160 * 3 -or $a.Length -ne $b.Length) { throw 'unexpected video frame size' }
    $different = 0
    for ($position = $header.Length; $position -lt $a.Length; $position += 3) {
        if ($a[$position] -ne $b[$position] -or $a[$position + 1] -ne $b[$position + 1] -or $a[$position + 2] -ne $b[$position + 2]) { $different++ }
    }
    $pass = $runs.actual.Complete -and $runs.expected.Complete -and !$different
    $results += [pscustomobject]@{Case=$index;Name=$names[$index];Pass=$pass;DifferentPixels=$different;Pixels=38400;Runs=$runs}
    Write-Output "$($names[$index]): pass=$pass different_pixels=$different/38400"
}
$results | ConvertTo-Json -Depth 6 | Set-Content "$out/results.json"
if ($Strict -and @($results | Where-Object {!$_.Pass}).Count) { throw 'strict video comparison failed' }
