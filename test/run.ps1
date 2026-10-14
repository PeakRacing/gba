param([switch]$SkipFetch, [switch]$SkipSuite, [switch]$SkipVideo, [switch]$StrictHardware, [switch]$TraceSuite, [switch]$FastAcceptance,
      [string]$Toolchain = 'F:/Arm GNU Toolchain arm-none-eabi/14.3 rel1/bin',
      [string]$Python = 'D:/Program Files/Python313/python.exe')
$ErrorActionPreference = 'Stop'
if ($FastAcceptance) {
    if ($SkipSuite -or $SkipVideo) { throw 'fast acceptance requires the strict suites and video comparisons' }
    $TraceSuite = $true
}
Set-Location (Split-Path $PSScriptRoot -Parent)
New-Item -ItemType Directory -Force test/out | Out-Null
if (!$SkipFetch) { & "$PSScriptRoot/fetch.ps1" }
if ((git -C tools/reference/gba-tests rev-parse HEAD) -ne 'a7113b67e63f83a9b321696ddd7042ccfad6c881') { throw 'gba-tests revision mismatch' }
xmake f -m release -y
if ($LASTEXITCODE) { throw 'host configure failed' }
xmake build gba-tests gba-tests-full gba-tests-stream gba-tests-minimal gba-tests-skip gba-tests-small-save gba-bench > test/out/host-build.log
if ($LASTEXITCODE) { throw 'host build failed' }
$results = @()
foreach ($name in @('gba-tests','gba-tests-full','gba-tests-stream','gba-tests-minimal','gba-tests-skip','gba-tests-small-save')) {
    $output = (& "build/windows/x64/release/$name.exe") -join "`n"
    if ($LASTEXITCODE) { throw "unit tests failed: $name`n$output" }
    Write-Output "$name $output"
    $results += [pscustomobject]@{Kind='Unit';Name=$name;Output=$output}
}
& "$PSScriptRoot/state_matrix.ps1"
if ($LASTEXITCODE) { throw 'state configuration/damage matrix failed' }
$roms = Get-ChildItem tools/reference/gba-tests -Recurse -Filter '*.gba'
if ($roms.Count -ne 13) { throw 'expected the 13 ROMs from the pinned gba-tests revision' }
foreach ($rom in $roms) {
    $name = $rom.Directory.Name + '-' + $rom.BaseName
    $outputs = @{}
    foreach ($mode in @('full','stream')) {
        $directory = "test/out/roms/$mode"
        New-Item -ItemType Directory -Force $directory | Out-Null
        $path = "$directory/$name.gba"
        Copy-Item -LiteralPath $rom.FullName -Destination $path -Force
        foreach ($suffix in @('.sav','.rtc')) { if (Test-Path "$path$suffix") { Remove-Item -LiteralPath "$path$suffix" } }
        $output = (& "build/windows/x64/release/gba-tests-$mode.exe" --rom $path 5) -join "`n"
        $exitCode = $LASTEXITCODE
        $functional = $rom.Directory.Name -in @('arm','thumb','memory','bios','save','unsafe')
        $status = if ($exitCode -ne 0) {'FAIL'} elseif ($functional -and $output -match 'r12=0 ') {'PASS'} elseif ($functional) {'FAIL'} else {'SMOKE'}
        $outputs[$mode] = $output
        $results += [pscustomobject]@{Kind='ROM';Name=$name;Mode=$mode;Status=$status;Output=$output}
        if (Test-Path test/out/last.ppm) { Copy-Item test/out/last.ppm "$directory/$name.ppm" -Force }
    }
    $same = $true
    foreach ($field in @('hardware','cycles','pc','r12','fault','image','audio','steps')) {
        $pattern = "(?<=${field}=)[^ ]+"
        if ([regex]::Match($outputs.full,$pattern).Value -ne [regex]::Match($outputs.stream,$pattern).Value) { $same = $false }
    }
    $results += [pscustomobject]@{Kind='Comparison';Name=$name;Equal=$same}
    Write-Output "$name resident/stream equal=$same"
    New-Item -ItemType Directory -Force test/out/roms/skip | Out-Null
    $skipPath = "test/out/roms/skip/$name.gba"
    Copy-Item -LiteralPath $rom.FullName -Destination $skipPath -Force
    foreach ($suffix in @('.sav','.rtc')) { if (Test-Path "$skipPath$suffix") { Remove-Item -LiteralPath "$skipPath$suffix" } }
    $skip = (& build/windows/x64/release/gba-tests-skip.exe --rom $skipPath 5) -join "`n"
    $skipEqual = $true
    foreach ($field in @('hardware','cycles','pc','r12','fault','audio','steps')) {
        $pattern = "(?<=${field}=)[^ ]+"
        if ([regex]::Match($outputs.full,$pattern).Value -ne [regex]::Match($skip,$pattern).Value) { $skipEqual = $false }
    }
    $results += [pscustomobject]@{Kind='SkipComparison';Name=$name;Equal=$skipEqual;Output=$skip}
}
if (!$SkipSuite) {
    & "$PSScriptRoot/build_suite.ps1" -Toolchain $Toolchain > test/out/suite-build.log 2>&1
    New-Item -ItemType Directory -Force test/out/suite-run | Out-Null
    Copy-Item test/out/suite/suite.gba test/out/suite-run/suite.gba -Force
    if (Test-Path test/out/suite-run/suite.gba.sav) { Remove-Item -LiteralPath test/out/suite-run/suite.gba.sav }
    $previousPC = $env:GBA_TEST_LOG_PC
    $previousInfo = $env:GBA_TEST_INFO_ADDRESS
    try {
        if ($TraceSuite) {
            $symbols = & (Join-Path $Toolchain 'arm-none-eabi-nm.exe') test/out/suite/suite.elf
            $logSymbol = $symbols | Select-String 'savprintf$'
            $infoSymbol = $symbols | Select-String 'activeTestInfo$'
            if (!$logSymbol -or !$infoSymbol) { throw 'suite trace symbols missing' }
            $env:GBA_TEST_LOG_PC = '0x' + ($logSymbol.ToString() -split ' ')[0]
            $env:GBA_TEST_INFO_ADDRESS = '0x' + ($infoSymbol.ToString() -split ' ')[0]
        }
        $output = (& build/windows/x64/release/gba-tests-full.exe --rom test/out/suite-run/suite.gba 3000) -join "`n"
        if ($LASTEXITCODE) { throw 'suite runner faulted' }
    } finally {
        $env:GBA_TEST_LOG_PC = $previousPC
        $env:GBA_TEST_INFO_ADDRESS = $previousInfo
    }
    if ($TraceSuite) { & "$PSScriptRoot/summarize_suite.ps1" }
    $hardwarePass = $output -match 'suite_magic=53554954'
    $suiteMatches = [regex]::Matches($output,'suite\d+=(\d+)/(\d+)')
    if ($suiteMatches.Count -ne 11) { $hardwarePass = $false }
    foreach ($match in $suiteMatches) {
        if ($match.Groups[1].Value -ne $match.Groups[2].Value -or $match.Groups[2].Value -eq '0') { $hardwarePass = $false }
    }
    $results += [pscustomobject]@{Kind='Suite';Name='mGBA headless 11 suites';Pass=$hardwarePass;Output=$output}
    Write-Output $output
    if (!$SkipVideo) {
        & "$PSScriptRoot/video.ps1" -Toolchain $Toolchain -Python $Python
        foreach ($video in (Get-Content test/out/video/results.json -Raw | ConvertFrom-Json)) {
            $results += [pscustomobject]@{Kind='Video';Name=$video.Name;Pass=$video.Pass;DifferentPixels=$video.DifferentPixels}
            if (!$video.Pass) { $hardwarePass = $false }
        }
    }
}
$bench = (& build/windows/x64/release/gba-bench.exe tools/reference/gba-tests/arm/arm.gba 600) -join "`n"
if ($LASTEXITCODE) { throw 'benchmark execution failed' }
$results += [pscustomobject]@{Kind='Benchmark';Name='ARM test ROM 600 frames, profile enabled';Output=$bench}
$results | ConvertTo-Json -Depth 5 | Set-Content test/out/results.json
if ($FastAcceptance) {
    & $Python test/fast_acceptance.py
    if ($LASTEXITCODE) { throw 'new unreviewed strict difference; see test/out/fast-acceptance.json' }
}
Write-Output $bench
if (($results | Where-Object { $_.Status -eq 'FAIL' -or ($_.Kind -in @('Comparison','SkipComparison') -and !$_.Equal) }).Count) { throw 'ROM regression failed; see test/out/results.json' }
if ($StrictHardware -and !$SkipSuite -and !$hardwarePass) { throw 'Strict hardware validation FAILED; see mGBA suite results' }
if (!$SkipSuite -and !$hardwarePass) {
    if ($FastAcceptance) { Write-Output 'Strict silicon differences retained; the fast precision gate accepts only the reviewed records.' }
    else { Write-Warning 'mGBA strict hardware validation FAILED. Default exit status covers local regressions only.' }
}
