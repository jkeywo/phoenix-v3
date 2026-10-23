# Explicit smoke benchmark; no timing threshold is part of CI.
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path "$PSScriptRoot/../../..").Path
$previousPath = $env:PATH
Push-Location $repo
try
{
	$cmake = "$repo/.tools/python/cmake/data/bin/cmake.exe"
	$env:PATH = (@("$repo/.tools/emsdk", "$repo/.tools/emsdk/upstream/emscripten", "$repo/.tools/python/bin") + $env:PATH) -join [IO.Path]::PathSeparator
	function Invoke-BenchCommand([string]$Program, [string[]]$Arguments)
	{
		& $Program @Arguments
		if ($LASTEXITCODE -ne 0)
		{
			throw "Benchmark command failed: $Program"
		}
	}
	Invoke-BenchCommand $cmake @('-S', 'tests/manual/image-codecs', '-B', 'build/image-codec-bench/native', '-G', 'Visual Studio 17 2022', '-A', 'x64')
	Invoke-BenchCommand $cmake @('--build', 'build/image-codec-bench/native', '--config', 'Release', '--target', 'image_codecs', '-j', '8')
	Invoke-BenchCommand $cmake @('-S', 'tests/manual/image-codecs', '-B', 'build/image-codec-bench/web', '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', "-DCMAKE_MAKE_PROGRAM=$repo/.tools/python/bin/ninja.exe", "-DCMAKE_TOOLCHAIN_FILE=$repo/.tools/emsdk/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake")
	Invoke-BenchCommand $cmake @('--build', 'build/image-codec-bench/web', '--target', 'image_codecs', '-j', '8')
	& './build/image-codec-bench/native/Release/image_codecs.exe' encode content/base/assets build/image-codec-bench/corpus > build/image-codec-bench/native-results.json
	if ($LASTEXITCODE -ne 0)
	{
		throw 'Native encode/decode or pixel verification failed'
	}
	& node build/image-codec-bench/web/image_codecs.js encode content/base/assets build/image-codec-bench/corpus-web > build/image-codec-bench/web-encode-results.json
	if ($LASTEXITCODE -ne 0)
	{
		throw 'WASM encode/decode or pixel verification failed'
	}
	Invoke-BenchCommand './build/image-codec-bench/native/Release/image_codecs.exe' @('compare','build/image-codec-bench/corpus','build/image-codec-bench/corpus-web')
	& node build/image-codec-bench/web/image_codecs.js decode content/base/assets build/image-codec-bench/corpus > build/image-codec-bench/web-results.json
	if ($LASTEXITCODE -ne 0)
	{
		throw 'WASM decode or pixel verification failed'
	}
	Get-Content build/image-codec-bench/native-results.json, build/image-codec-bench/web-results.json
}
finally
{
	$env:PATH=$previousPath
	Pop-Location
}
