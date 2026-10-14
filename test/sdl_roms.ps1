# Copyright PeakRacing
# Licensed under the Apache License, Version 2.0.
# https://www.apache.org/licenses/LICENSE-2.0
# Distributed on an AS IS BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND.
param([string]$ROMDirectory = 'E:/Game/GBA汉化全集', [ValidateRange(360, 36000)][int]$Frames = 600,
      [switch]$Dummy, [switch]$Deep, [string[]]$Case)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root
$out = Join-Path $root $(if ($Deep) {'test/out/sdl-deep'} else {'test/out/sdl-games'})
if ($Deep -and $Frames -lt 3600) { $Frames = 3600 }
New-Item -ItemType Directory -Force $out | Out-Null
$roms = Get-ChildItem -LiteralPath $ROMDirectory -Recurse -Filter '*.gba'
$cases = [ordered]@{
    golden_sun='黄金太阳剧情100%'
    zelda_minish='1743 塞尔达传说-缩小帽'
    metroid_fusion='银河战士 - 融合\[Chrono'
    fire_emblem='火焰纹章 - 烈火之剑\[狼组'
    pokemon_emerald='GBA_1986_精灵宝可梦 绿宝石'
    mario_advance='超级马里奥Advance - 超级马里奥USA'
    ace_attorney='逆转裁判\[Jpgmfan'
    fzero='F-Zero GBA\[PGCG'
}
$environmentNames = @('SDL_VIDEODRIVER','SDL_AUDIODRIVER','GBA_TEST_SAVE_AT','GBA_TEST_LOAD_AT','GBA_TEST_PLAY','GBA_TEST_REPORT','GBA_TEST_REPLAY','GBA_TEST_INPUT','GBA_TEST_RELOADS','GBA_TEST_SAVE_FINAL','GBA_TEST_LOAD_INITIAL')
$previous = @{}
foreach ($name in $environmentNames) { $previous[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
$results = @()
if ($Deep -and (Test-Path (Join-Path $out 'results.json'))) {
    $results = @(Get-Content (Join-Path $out 'results.json') -Raw | ConvertFrom-Json)
}
$binary = Join-Path $root 'sdl/sdl2/build/windows/x64/release/gba-test.exe'
$binaryHash = (Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash
try {
    if ($Dummy) {
        $env:SDL_VIDEODRIVER = 'dummy'
        $env:SDL_AUDIODRIVER = 'dummy'
    }
    $env:GBA_TEST_SAVE_AT = '240'
    $env:GBA_TEST_LOAD_AT = '360'
    $env:GBA_TEST_PLAY = '1'
    $env:GBA_TEST_REPLAY = '1'
    $env:GBA_TEST_LOAD_INITIAL = $null
    foreach ($entry in $cases.GetEnumerator()) {
        if ($Case -and $entry.Key -notin $Case) { continue }
        $caseFrames = $Frames
        $rom = $roms | Where-Object {$_.BaseName -match $entry.Value} | Select-Object -First 1
        if (!$rom) { throw "representative ROM missing: $($entry.Key)" }
        $base = Join-Path $out $entry.Key
        $path = "$base.gba"
        Copy-Item -LiteralPath $rom.FullName -Destination $path -Force
        foreach ($suffix in @('.sav','.rtc','.state')) {
            if (Test-Path -LiteralPath "$path$suffix") { Remove-Item -LiteralPath "$path$suffix" }
        }
        $env:GBA_TEST_REPORT = $base
        if ($Deep) {
            $inputs = Get-Content test/game-inputs.json -Raw | ConvertFrom-Json
            if ($inputs.($entry.Key).MinimumFrames -gt $caseFrames) { $caseFrames = $inputs.($entry.Key).MinimumFrames }
            $starts = @($inputs.($entry.Key).StartFrames)
            $events = @()
            foreach ($frame in $starts) { $events += "$frame,4,8" }
            for ($frame = 180; $frame -lt $caseFrames; $frame += 60) {
                $skip = $false
                foreach ($range in $inputs.($entry.Key).SkipA) {
                    if ($frame -ge $range[0] -and $frame -lt $range[1]) { $skip = $true }
                }
                if (!$skip -and $frame -notin $starts) { $events += "$frame,4,1" }
            }
            foreach ($event in $inputs.($entry.Key).Directions) { $events += "$($event[0]),$($event[1]),$($event[2])" }
            foreach ($event in $inputs.($entry.Key).ExtraEvents) { $events += "$($event[0]),$($event[1]),$($event[2])" }
            $events | Set-Content "$base.input.csv"
            $env:GBA_TEST_INPUT = "$base.input.csv"
            $env:GBA_TEST_RELOADS = '5'
            $env:GBA_TEST_SAVE_FINAL = '1'
        } else {
            $env:GBA_TEST_INPUT = $null
            $env:GBA_TEST_RELOADS = $null
            $env:GBA_TEST_SAVE_FINAL = $null
        }
        $timer = [Diagnostics.Stopwatch]::StartNew()
        $start = @{FilePath=(Join-Path $root 'sdl/sdl2/build/windows/x64/release/gba-test.exe');
            ArgumentList=@('"' + $path + '"','--frames',"$caseFrames");WindowStyle='Hidden';PassThru=$true;
            RedirectStandardOutput="$base.stdout.txt";RedirectStandardError="$base.stderr.txt"}
        $process = Start-Process @start
        $timeout = ($caseFrames + 120 + $(if ($Deep) {900} else {0})) * 1000 / 50 + 30000
        $complete = $false
        while ($timer.ElapsedMilliseconds -lt $timeout) {
            if ($process.WaitForExit(1000)) { $complete = $true; break }
        }
        if (!$complete) { $process.Kill(); $process.WaitForExit() }
        $timer.Stop()
        $log = Get-Content -LiteralPath "$base.stderr.txt" -Raw
        $frameMatch = [regex]::Match($log,'test frames=(\d+)')
        $actualFrames = if ($frameMatch.Success) {[int]$frameMatch.Groups[1].Value} else {0}
        $status = if (!$complete) {'TIMEOUT'} elseif ($process.ExitCode -ne 0 -or $log -match 'emulation fault') {'FAULT'}
            elseif ($actualFrames -ne $caseFrames -or $log -notmatch 'test save=0' -or $log -notmatch 'test load=0' -or
                $log -notmatch 'test replay_frames=120 replay_failures=0' -or
                ($Deep -and $log -notmatch 'test reload=5 result=0')) {'FAIL'} else {'SMOKE'}
        $results = @($results | Where-Object {$_.Case -ne $entry.Key})
        $results += [pscustomobject]@{Case=$entry.Key;ROM=$rom.Name;SHA256=(Get-FileHash -LiteralPath $rom.FullName -Algorithm SHA256).Hash;
            BinarySHA256=$binaryHash;InputSHA256=$(if ($Deep) {(Get-FileHash "$base.input.csv" -Algorithm SHA256).Hash} else {'built-in v1'});
            Driver=$(if ($Dummy) {'SDL dummy'} else {'SDL2 system video/audio'});Status=$status;Frames=$actualFrames;
            Milliseconds=$timer.ElapsedMilliseconds;Exit=$process.ExitCode;Output=$log;Screenshot="$($entry.Key).ppm"}
        Write-Output "$($entry.Key): $status frames=$actualFrames time_ms=$($timer.ElapsedMilliseconds)"
        $results | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $out 'results.json')
    }
} finally {
    foreach ($name in $environmentNames) { [Environment]::SetEnvironmentVariable($name, $previous[$name], 'Process') }
}
Write-Output 'SMOKE confirms bounded SDL execution and file state recovery; it does not establish full game compatibility.'
if (@($results | Where-Object {$_.Status -ne 'SMOKE'}).Count) { throw 'SDL ROM execution or replay failed; see test/out/sdl-games/results.json' }
