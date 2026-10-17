param([ValidateSet('native','web')][string]$Target = 'native', [switch]$ConfigureOnly)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
Push-Location $repo
try {
    $cmake = Join-Path $repo '.tools/python/cmake/data/bin/cmake.exe'
    if (!(Test-Path $cmake)) { throw 'Run scripts/bootstrap.ps1 first.' }
    if ($Target -eq 'native') {
        & $cmake -S . -B build/native -G 'Visual Studio 17 2022' -A x64
    } else {
        $env:PATH = "$repo/.tools/emsdk;$repo/.tools/emsdk/upstream/emscripten;$repo/.tools/python/bin;" + $env:PATH
        & $cmake -S . -B build/web -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_TOOLCHAIN_FILE=$repo/.tools/emsdk/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake" -DPLATFORM=Web
    }
    if ($LASTEXITCODE) { throw 'Configure failed' }
    if (!$ConfigureOnly) {
        & $cmake --build "build/$Target" --config Release --parallel 8
        if ($LASTEXITCODE) { throw 'Build failed' }
    }
} finally { Pop-Location }
