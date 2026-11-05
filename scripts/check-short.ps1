param(
	[ValidateSet('all', 'native', 'web')][string]$Target = 'native',
	[switch]$Threaded,
	[ValidateRange(1, 1024)][int]$Jobs = 8,
	[string]$NativeBuildDirectory = 'build/native',
	[string]$WebBuildDirectory = 'build/web'
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$nodeName = if ($IsWindows)
{
	'node.exe'
}
else
{
	'bin/node'
}
$node = Join-Path $repo ".tools/emsdk/node/24.19.0_64bit/$nodeName"
$profile = Get-Content -LiteralPath "$PSScriptRoot/validation-profile.json" -Raw | ConvertFrom-Json
Push-Location $repo
try
{
	& $node --test tests/system/web_launcher.mjs tests/system/wasm_trace.mjs tests/system/wasm_continuations.mjs tests/system/deployment_verifier.mjs
	if ($LASTEXITCODE)
	{
		throw 'Launcher and validation-tool integration failed'
	}
	if ($Target -ne 'web')
	{
		& "$PSScriptRoot/build.ps1" -Target native -BuildTarget phoenix_short_checks -BuildDirectory $NativeBuildDirectory -Jobs $Jobs
		$ctest = "$repo/.tools/python/cmake/data/bin/ctest.exe"
		$registered = & $ctest --test-dir $NativeBuildDirectory -C Release --show-only=json-v1 | ConvertFrom-Json
		if ($LASTEXITCODE)
		{
			throw 'Cannot enumerate native system checks'
		}
		foreach ($check in $profile.native.checks)
		{
			if ($check.test -notin $registered.tests.name)
			{
				throw "Missing short-profile check: $($check.test)"
			}
		}
		$names = $profile.native.checks.test | ForEach-Object { [regex]::Escape($_) }
		$pattern = '^(architecture[.].*|' + ($names -join '|') + ')$'
		& $ctest --test-dir $NativeBuildDirectory -C Release --no-tests=error --output-on-failure -R $pattern
		if ($LASTEXITCODE)
		{
			throw 'Short native system integration failed'
		}
		& "$NativeBuildDirectory/Release/native.exe" --build-check
		if ($LASTEXITCODE)
		{
			throw 'Native production build check failed'
		}
	}
	if ($Target -ne 'native')
	{
		$webOptions = @{}
		$cache = Get-Content -LiteralPath "$WebBuildDirectory/CMakeCache.txt" -Raw -ErrorAction SilentlyContinue
		# Keep Release linking, without forcing a no-op CMake regeneration on every check.
		if ($cache -notmatch '(?m)^PHOENIX_WEB_DEVELOPMENT_LINK:BOOL=OFF\r?$')
		{
			$webOptions.DevelopmentLink = $false
		}
		if ($PSBoundParameters.ContainsKey('Threaded'))
		{
			$webOptions.Threaded = $Threaded
		}
		& "$PSScriptRoot/build.ps1" -Target web -BuildTarget phoenix_short_checks -BuildDirectory $WebBuildDirectory -Jobs $Jobs @webOptions
		Push-Location $WebBuildDirectory
		try
		{
			foreach ($check in $profile.web.checks)
			{
				$arguments = @()
				if ($check.browser_host)
				{
					$arguments += @('--import', ([System.UriBuilder]::new('file', '', -1, (Join-Path $repo 'tests/support/browser_blob_host.mjs'))).Uri.AbsoluteUri)
				}
				$arguments += "$($check.target).js"
				& $node @arguments
				if ($LASTEXITCODE)
				{
					throw "Short WASM integration failed: $($check.target)"
				}
			}
			& $node "$repo/tests/system/base_verifier.mjs" './phoenix_base_verifier.js'
			if ($LASTEXITCODE)
			{
				throw 'WASM production preflight failed'
			}
		}
		finally
		{
			Pop-Location
		}
	}
	Write-Output 'PASS short validation. Full system cases, live service journeys and continuation comparisons require -Long.'
}
finally
{
	Pop-Location
}
