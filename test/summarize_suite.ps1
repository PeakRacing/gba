param([string]$InputPath = 'test/out/suite/calls.log',
      [string]$OutputPath = 'test/out/suite/failures.json')
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
$names = @('shifter','carry','multiply-long','bios-math','memory','io-read','timing','timers','timer-irq','dma','misc-edge')
$context = @{}
$failures = @()
foreach ($line in Get-Content -LiteralPath $InputPath) {
    if ($line -notmatch '^suite=(\d+) test=(-?\d+) sub=(-?\d+) r1=(\w+) r2=(\w+) r3=(\w+)(?: s0=(\w+) s1=(\w+) s2=(\w+) s3=(\w+))? fmt=(.*?) label=(.*)$') { continue }
    $suiteIndex = [int]$Matches[1]
    if ($suiteIndex -ge $names.Count) { throw 'unexpected suite index' }
    $testId = [int]$Matches[2]
    $subtest = [int]$Matches[3]
    $registers = @($Matches[4],$Matches[5],$Matches[6])
    $stack = @($Matches[7],$Matches[8],$Matches[9],$Matches[10])
    $format = $Matches[11]
    $label = $Matches[12]
    if ($format -match 'test: %s') { $context[$suiteIndex] = $label }
    if ($format -notmatch 'FAIL') { continue }
    $failures += [pscustomobject]@{SuiteIndex=$suiteIndex;Suite=$names[$suiteIndex];Case=$context[$suiteIndex];
        TestId=$testId;Subtest=$subtest;Label=$label;Format=$format;R1=$registers[0];R2=$registers[1];R3=$registers[2];Stack=$stack}
}
$counts = @($failures | Group-Object Suite | ForEach-Object {
    [pscustomobject]@{Suite=$_.Name;Failures=$_.Count}
})
$cases = @($failures | Group-Object Suite,Case | ForEach-Object {
    [pscustomobject]@{Suite=$_.Group[0].Suite;Case=$_.Group[0].Case;Failures=$_.Count;
        Labels=@($_.Group.Label | Select-Object -Unique);Examples=@($_.Group | Select-Object -First 3)}
})
[pscustomobject]@{Reference='e6942030d25ffe3ba76c72b73a86da073ec857cc';TotalFailures=$failures.Count;
    Note='Raw ARM registers and four stack argument words; interpret each format against the original test.';Counts=$counts;Cases=$cases;Failures=$failures} |
    ConvertTo-Json -Depth 7 | Set-Content -LiteralPath $OutputPath
Write-Output "suite failure trace: $($failures.Count) comparisons, $($cases.Count) cases"
