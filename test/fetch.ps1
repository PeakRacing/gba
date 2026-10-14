$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
New-Item -ItemType Directory -Force tools/reference | Out-Null
$references = @(
    @('gba-tests','https://github.com/jsmolka/gba-tests','a7113b67e63f83a9b321696ddd7042ccfad6c881'),
    @('mgba-suite','https://github.com/mgba-emu/suite','e6942030d25ffe3ba76c72b73a86da073ec857cc'),
    @('libgba','https://github.com/devkitPro/libgba','f0e44ce9ed57e1d21f148443223408fddeacfb9d')
)
foreach ($reference in $references) {
    $path = "tools/reference/$($reference[0])"
    if (!(Test-Path "$path/.git")) {
        git clone --no-checkout $reference[1] $path
        if ($LASTEXITCODE) { throw 'reference clone failed' }
        git -C $path checkout --detach $reference[2]
        if ($LASTEXITCODE) { throw 'reference checkout failed' }
    }
    if ((git -C $path rev-parse HEAD) -ne $reference[2]) { throw "reference revision mismatch: $path" }
    Write-Output "$($reference[0]) $($reference[2])"
}
