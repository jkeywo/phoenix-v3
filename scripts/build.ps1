param(
	[ValidateSet('native', 'web')][string]$Target = 'native',
	[string[]]$BuildTarget,
	[ValidateRange(1, 1024)][int]$Jobs = 8,
	[switch]$Reconfigure,
	[switch]$ConfigureOnly,
	[switch]$Threaded,
	[switch]$DevelopmentLink,
	[switch]$Demo,
	[string]$BuildDirectory,
	[string[]]$CMakeArgument
)
$ErrorActionPreference = 'Stop'
if ($Target -ne 'web' -and $PSBoundParameters.ContainsKey('DevelopmentLink'))
{
	throw '-DevelopmentLink applies only to web builds.'
}
$repo = Split-Path $PSScriptRoot -Parent
$originalPath = $env:PATH
$originalEmccTemp = $env:EMCC_TEMP_DIR
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
	$buildDirectory = if ($BuildDirectory)
	{
		$BuildDirectory
	}
	elseif ($Demo)
	{
		"build/demo-$Target"
	}
	else
	{
		"build/$Target"
	}
	$packDirectory = if ($Demo)
	{
		'build/demo-native'
	}
	else
	{
		'build/native'
	}
	if ($Demo -and !$BuildTarget)
	{
		$BuildTarget = if ($Target -eq 'native')
		{
			@('native')
		}
		else
		{
			@('server', 'client', 'phoenix_base_verifier')
		}
	}
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
		if (!$env:EMCC_TEMP_DIR)
		{
			$emccTemporary = Join-Path $repo '.cache/emcc-temp'
			New-Item -ItemType Directory -Path $emccTemporary -Force | Out-Null
			$env:EMCC_TEMP_DIR = $emccTemporary
		}
	}
	$threadedRequested = $Target -eq 'web' -and $PSBoundParameters.ContainsKey('Threaded')
	$developmentLinkRequested = $Target -eq 'web' -and $PSBoundParameters.ContainsKey('DevelopmentLink')
	[string[]]$threadedArguments = @()
	if ($Demo)
	{
		$threadedArguments += @('-C', "$repo/cmake/demo.cmake", "-DPHOENIX_PACK_INPUT_DIRECTORY=$repo/$packDirectory")
		# Reuse pinned source downloads, while keeping dependency build outputs separate.
		foreach ($dependency in Get-ChildItem "$repo/build/native/_deps" -Directory -Filter '*-src' -ErrorAction SilentlyContinue)
		{
			$name = $dependency.Name.Substring(0, $dependency.Name.Length - 4).ToUpperInvariant()
			$threadedArguments += "-DFETCHCONTENT_SOURCE_DIR_$name=$($dependency.FullName)"
		}
	}
	if ($threadedRequested)
	{
		$threadedArguments += "-DPHOENIX_BROWSER_THREADS=$(if ($Threaded) { 'ON' } else { 'OFF' })"
	}
	if ($developmentLinkRequested)
	{
		$threadedArguments += "-DPHOENIX_WEB_DEVELOPMENT_LINK=$(if ($DevelopmentLink) { 'ON' } else { 'OFF' })"
	}
	$threadedArguments += $CMakeArgument
	# Generated builds already rerun CMake when tracked configuration inputs change.
	if ($Demo -or $CMakeArgument -or $Reconfigure -or $ConfigureOnly -or $threadedRequested -or $developmentLinkRequested -or !(Test-Path "$buildDirectory/CMakeCache.txt") -or !(Test-Path "$buildDirectory/$buildFile"))
	{
		if ($Target -eq 'native')
		{
			& $cmake -S . -B $buildDirectory -G 'Visual Studio 17 2022' -A x64 @threadedArguments
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
		$cache = Get-Content "$buildDirectory/CMakeCache.txt" -Raw
		$packSetting = [regex]::Match($cache, '(?m)^PHOENIX_PACK_INPUT_DIRECTORY:PATH=(.+)\r?$')
		if ($packSetting.Success)
		{
			$packDirectory = $packSetting.Groups[1].Value.Trim()
		}
		$bundledMissions = $cache -match '(?m)^PHOENIX_BUNDLE_TEST_MISSIONS:BOOL=ON\r?$'
		if ($Demo -and $Target -eq 'web')
		{
			foreach ($name in @('base.zip', 'base.zip.menu.json', 'base.zip.clients'))
			{
				if (!(Test-Path "$packDirectory/$name"))
				{
					throw 'Build the native demo packs before the browser demo.'
				}
			}
		}
		# Stage verified native packs before linking WASM preload bundles. Copying
		# them only after the build left manual hosts embedding the previous pack.
		if ($Target -eq 'web')
		{
			foreach ($file in $(if (!$bundledMissions)
					{
						@('base.zip', 'base.zip.menu.json', 'base.zip.clients')
					}
					else
					{
						@('base.zip', 'base.zip.menu.json', 'test-missions.zip', 'test-missions.zip.menu.json', 'base.zip.clients', 'test-missions.zip.clients')
					}))
			{
				if (Test-Path "$packDirectory/$file")
				{
					$isFile = Test-Path "$packDirectory/$file" -PathType Leaf
					if ($isFile -and (Test-Path "$buildDirectory/$file" -PathType Leaf) -and
						(Get-FileHash "$packDirectory/$file").Hash -eq (Get-FileHash "$buildDirectory/$file").Hash)
					{
						continue
					}
					Copy-Item -LiteralPath "$packDirectory/$file" -Destination $buildDirectory -Recurse -Force
					if ($isFile)
					{
						(Get-Item "$buildDirectory/$file").LastWriteTimeUtc = [DateTime]::UtcNow
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
	$env:EMCC_TEMP_DIR = $originalEmccTemp
	Pop-Location
}
