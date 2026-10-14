param([string]$Toolchain = 'F:/Arm GNU Toolchain arm-none-eabi/14.3 rel1/bin')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root
$suite = 'tools/reference/mgba-suite'
$libgba = 'tools/reference/libgba'
$expected = 'e6942030d25ffe3ba76c72b73a86da073ec857cc'
if ((git -C $suite rev-parse HEAD) -ne $expected) { throw 'suite revision mismatch' }
if ((git -C $libgba rev-parse HEAD) -ne 'f0e44ce9ed57e1d21f148443223408fddeacfb9d') { throw 'libgba revision mismatch' }
$out = 'test/out/suite'
New-Item -ItemType Directory -Force $out | Out-Null
$cc = Join-Path $Toolchain 'arm-none-eabi-gcc.exe'
$flags = @('-mcpu=arm7tdmi', '-mthumb', '-mthumb-interwork', '-O3', '-fomit-frame-pointer', '-ffast-math', '-ffunction-sections', '-fdata-sections', '-Wno-format', "-I$suite/include", "-I$libgba/include")
$objects = @()
foreach ($name in @('shifter','carry','multiply-long','bios-math','memory','io-read','timing','timers','timer-irq','dma','misc-edge')) {
    # Remove UI callback references so --gc-sections omits the interactive UI.
    # The run functions, expected results and instruction sequences are unchanged.
    $source = Get-Content "$suite/src/$name.c" -Raw
    $source = $source -replace '\.list\s*=\s*\w+', '.list = 0' -replace '\.show\s*=\s*\w+', '.show = 0'
    [IO.File]::WriteAllText((Join-Path $root "$out/$name.c"), $source)
    & $cc @flags -c "$out/$name.c" -o "$out/$name.o"
    if ($LASTEXITCODE) { throw "compile $name failed" }
    $objects += "$out/$name.o"
}
foreach ($path in (@('test/suite_adapter.c', "$suite/src/shifter-impl.s", "$libgba/src/interrupt.c", "$libgba/src/InterruptDispatcher.s", 'test/suite_start.s', "$libgba/src/IntrWait.c", "$libgba/src/CpuSet.c") + @(Get-ChildItem "$suite/src/tests/*.s" | ForEach-Object { $_.FullName }))) {
    $name = [IO.Path]::GetFileNameWithoutExtension($path)
    & $cc @flags -x $(if ($path.EndsWith('.s')) {'assembler-with-cpp'} else {'c'}) -c $path -o "$out/$name.o"
    if ($LASTEXITCODE) { throw "compile $path failed" }
    $objects += "$out/$name.o"
}
& $cc -mcpu=arm7tdmi -mthumb -mthumb-interwork -nostartfiles --specs=nosys.specs '-Wl,--gc-sections' '-Wl,-T,test/suite.ld' '-Wl,-Map,test/out/suite/suite.map' @objects -lc -lgcc -o "$out/suite.elf"
if ($LASTEXITCODE) { throw 'link failed' }
& (Join-Path $Toolchain 'arm-none-eabi-objcopy.exe') -O binary "$out/suite.elf" "$out/suite.gba"
if ($LASTEXITCODE) { throw 'objcopy failed' }
Get-FileHash "$out/suite.gba" -Algorithm SHA256
$symbols = & (Join-Path $Toolchain 'arm-none-eabi-nm.exe') -n "$out/suite.elf"
if ($LASTEXITCODE) { throw 'suite symbol inspection failed' }
foreach ($symbol in @('IntrTable','irqCounter','__data_start','__bss_start','__bss_end')) {
    $entry = $symbols | Select-String " $symbol`$"
    if (!$entry) { throw "missing suite layout symbol: $symbol" }
    $address = [Convert]::ToUInt32(($entry.ToString() -split ' ')[0], 16)
    if ($address -lt 0x03000000 -or $address -ge 0x03007000) { throw "ordinary suite data outside IWRAM: $symbol" }
}
git -C $libgba rev-parse HEAD
