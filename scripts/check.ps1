$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
Push-Location $repo
try {
    & "$PSScriptRoot/build.ps1" -Target native
    & "$repo/.tools/python/cmake/data/bin/ctest.exe" --test-dir build/native -C Release --output-on-failure
    if ($LASTEXITCODE) { throw 'Native system integration failed' }
    & "$repo/build/native/Release/native.exe" --build-check
    if ($LASTEXITCODE) { throw 'Native build check failed' }
    $nativeTrace = & "$repo/build/native/Release/phoenix_simulation_tests.exe" --trace
    if ($LASTEXITCODE) { throw 'Native simulation trace failed' }
    & "$PSScriptRoot/build.ps1" -Target web
    Push-Location build/web
    try {
        & "$repo/.tools/emsdk/node/24.19.0_64bit/node.exe" phoenix_build_check.js
        if ($LASTEXITCODE) { throw 'WASM build check failed' }
        & "$repo/.tools/emsdk/node/24.19.0_64bit/node.exe" phoenix_content_check.js
        if ($LASTEXITCODE) { throw 'WASM content integration failed' }
        & "$repo/.tools/emsdk/node/24.19.0_64bit/node.exe" phoenix_simulation_check.js
        if ($LASTEXITCODE) { throw 'WASM simulation integration failed' }
        $wasmTrace = & "$repo/.tools/emsdk/node/24.19.0_64bit/node.exe" phoenix_simulation_check.js --trace
        if ($LASTEXITCODE) { throw 'WASM simulation trace failed' }
        if ($nativeTrace -cne $wasmTrace) { throw 'Native/WASM continuation differs in the same controlled fixture' }
    } finally { Pop-Location }
} finally { Pop-Location }
