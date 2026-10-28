param(
	[ValidateSet('native', 'web')][string]$Target = 'native',
	[string[]]$BuildTarget,
	[ValidateRange(1, 1024)][int]$Jobs = 8,
	[switch]$Reconfigure,
	[switch]$ConfigureOnly,
	[switch]$Threaded,
	[switch]$DevelopmentLink
)
$ErrorActionPreference = 'Stop'
if ($Target -ne 'web' -and $PSBoundParameters.ContainsKey('DevelopmentLink'))
{
	throw '-DevelopmentLink applies only to web builds.'
}
$repo = Split-Path $PSScriptRoot -Parent
$originalPath = $env:PATH
Push-Location $repo
try
{
	$cmakeName = if ([Environment]::OSVersion.Platform -eq [PlatformID]::Win32NT)
	{
		'cmake.exe'
	}
	else
	{
		'cmake'
	}
	$cmake = Join-Path $repo ".tools/python/cmake/data/bin/$cmakeName"
	if (!(Test-Path $cmake))
	{
		throw 'Run scripts/bootstrap.ps1 first.'
	}
	$buildDirectory = "build/$Target"
	$buildFile = if ($Target -eq 'native')
	{
		'PhoenixV3.sln'
	}
	else
	{
		'build.ninja'
	}
	if ($Target -eq 'web')
	{
		$toolPaths = @("$repo/.tools/emsdk", "$repo/.tools/emsdk/upstream/emscripten", "$repo/.tools/python/ninja/data/bin", "$repo/.tools/python/bin")
		$env:PATH = ($toolPaths + $env:PATH) -join [System.IO.Path]::PathSeparator
	}
	$threadedRequested = $Target -eq 'web' -and $PSBoundParameters.ContainsKey('Threaded')
	$developmentLinkRequested = $Target -eq 'web' -and $PSBoundParameters.ContainsKey('DevelopmentLink')
	[string[]]$threadedArguments = @()
	if ($threadedRequested)
	{
		$threadedArguments += "-DPHOENIX_BROWSER_THREADS=$(if ($Threaded) { 'ON' } else { 'OFF' })"
	}
	if ($developmentLinkRequested)
	{
		$threadedArguments += "-DPHOENIX_WEB_DEVELOPMENT_LINK=$(if ($DevelopmentLink) { 'ON' } else { 'OFF' })"
	}
	# Generated builds already rerun CMake when tracked configuration inputs change.
	if ($Reconfigure -or $ConfigureOnly -or $threadedRequested -or $developmentLinkRequested -or !(Test-Path "$buildDirectory/CMakeCache.txt") -or !(Test-Path "$buildDirectory/$buildFile"))
	{
		if ($Target -eq 'native')
		{
			& $cmake -S . -B $buildDirectory -G 'Visual Studio 17 2022' -A x64
		}
		else
		{
			& $cmake -S . -B $buildDirectory -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_TOOLCHAIN_FILE=$repo/.tools/emsdk/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake" -DPLATFORM=Web @threadedArguments
		}
		if ($LASTEXITCODE)
		{
			throw 'Configure failed'
		}
	}
	if (!$ConfigureOnly)
	{
		# Stage verified native packs before linking WASM preload bundles. Copying
		# them only after the build left manual hosts embedding the previous pack.
		if ($Target -eq 'web')
		{
			foreach ($file in @('base.zip', 'base.zip.menu.json', 'test-missions.zip', 'test-missions.zip.menu.json', 'base.zip.clients', 'test-missions.zip.clients'))
			{
				if (Test-Path "build/native/$file")
				{
					$isFile = Test-Path "build/native/$file" -PathType Leaf
					if ($isFile -and (Test-Path "build/web/$file" -PathType Leaf) -and
						(Get-FileHash "build/native/$file").Hash -eq (Get-FileHash "build/web/$file").Hash)
					{
						continue
					}
					Copy-Item -LiteralPath "build/native/$file" -Destination "build/web" -Recurse -Force
					if ($isFile)
					{
						(Get-Item "build/web/$file").LastWriteTimeUtc = [DateTime]::UtcNow
					}
				}
			}
		}
		$buildArguments = @('--build', $buildDirectory, '--config', 'Release', '--parallel', $Jobs)
		if ($BuildTarget)
		{
			$buildArguments += @('--target') + $BuildTarget
		}
		& $cmake @buildArguments
		if ($LASTEXITCODE)
		{
			throw 'Build failed'
		}
	}
}
finally
{
	$env:PATH = $originalPath
	Pop-Location
}
