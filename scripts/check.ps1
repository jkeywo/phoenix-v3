$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
Push-Location $repo
try {
    & "$PSScriptRoot/build.ps1" -Target native
    & "$repo/.tools/python/cmake/data/bin/ctest.exe" --test-dir build/native -C Release --output-on-failure
    if ($LASTEXITCODE) { throw 'Native system integration failed' }
    & "$repo/build/native/Release/native.exe" --build-check
    if ($LASTEXITCODE) { throw 'Native build check failed' }
    & "$PSScriptRoot/build.ps1" -Target web
    Push-Location build/web
    try {
        & "$repo/.tools/emsdk/node/24.19.0_64bit/node.exe" phoenix_build_check.js
        if ($LASTEXITCODE) { throw 'WASM build check failed' }
        & "$repo/.tools/emsdk/node/24.19.0_64bit/node.exe" phoenix_content_check.js
        if ($LASTEXITCODE) { throw 'WASM content integration failed' }
    } finally { Pop-Location }
} finally { Pop-Location }
