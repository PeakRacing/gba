# Copyright PeakRacing
# Licensed under the Apache License, Version 2.0.
# https://www.apache.org/licenses/LICENSE-2.0
# Distributed on an AS IS BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND.
param([string]$Label = 'current', [int]$Frames = 1800, [int]$Runs = 5,
      [string]$BinaryDirectory = 'build/windows/x64/release')
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
$results = @()
New-Item -ItemType Directory -Force test/out/speed-roms | Out-Null
foreach ($target in @('gba-speed565','gba-speed32','gba-speed-stream','gba-speed-silent')) {
    foreach ($rom in (Get-ChildItem test/out/sdl-games -Filter '*.gba' | Sort-Object Name)) {
        $path = Join-Path 'test/out/speed-roms' $rom.Name
        Copy-Item -LiteralPath $rom.FullName -Destination $path -Force
        $times = @()
        for ($run = 0; $run -lt $Runs; $run++) {
            $output = (& "$BinaryDirectory/$target.exe" $path $Frames) -join "`n"
            if ($LASTEXITCODE) { throw "$target $($rom.Name): $output" }
            $match = [regex]::Match($output, 'us=(\d+)')
            if (!$match.Success -or $output -notmatch 'test=0 profile=0') { throw 'invalid benchmark output' }
            $times += [long]$match.Groups[1].Value
        }
        $sorted = @($times | Sort-Object)
        $median = $sorted[[int][math]::Floor($sorted.Count / 2)]
        $results += [pscustomobject]@{Target=$target;ROM=$rom.Name;SHA256=(Get-FileHash $rom.FullName -Algorithm SHA256).Hash;Frames=$Frames;Runs=$times;MedianUS=$median;Output=$output}
        Write-Output "$target $($rom.Name) median_us=$median"
        $results | ConvertTo-Json -Depth 5 | Set-Content "test/out/speed-$Label.json"
    }
}
