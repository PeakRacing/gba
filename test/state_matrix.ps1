# Copyright PeakRacing
# Licensed under the Apache License, Version 2.0. See LICENSE for details.
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
$full = 'build/windows/x64/release/gba-tests-full.exe'
$stream = 'build/windows/x64/release/gba-tests-stream.exe'
& $full --state-write test/out/full-config.state
if ($LASTEXITCODE) { throw 'full state generation failed' }
& $stream --state-reject test/out/full-config.state
if ($LASTEXITCODE) { throw 'stream accepted full configuration' }
& $stream --state-write test/out/stream-config.state
if ($LASTEXITCODE) { throw 'stream state generation failed' }
& $full --state-reject test/out/stream-config.state
if ($LASTEXITCODE) { throw 'full accepted stream configuration' }
$original = [IO.File]::ReadAllBytes((Join-Path (Get-Location) 'test/out/full-config.state'))
$random = [Random]::new(148)
for ($case = 0; $case -lt 32; $case++) {
    $data = [byte[]]$original.Clone()
    $position = $random.Next(32, $data.Length)
    $data[$position] = $data[$position] -bxor (1 -shl $random.Next(0, 8))
    [IO.File]::WriteAllBytes((Join-Path (Get-Location) 'test/out/damaged.state'), $data)
    $output = & $full --state-damaged test/out/damaged.state
    if ($LASTEXITCODE) { throw "damaged state applied: case=$case offset=$position $output" }
}
Write-Output 'State matrix: two cross-configuration rejections and 32 seeded damaged payloads; CPU/RAM unchanged.'
