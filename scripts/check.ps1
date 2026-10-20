param([ValidateSet('all', 'native', 'web')][string]$Target = 'all')
$ErrorActionPreference = 'Stop'

$repo = Split-Path $PSScriptRoot -Parent
$nodeName = if ([Environment]::OSVersion.Platform -eq [PlatformID]::Win32NT)
{
	'node.exe'
}
else
{
	'bin/node'
}
$node = Join-Path $repo ".tools/emsdk/node/24.19.0_64bit/$nodeName"
& $node --test "$PSScriptRoot/../tests/system/web_launcher.mjs"
if ($LASTEXITCODE)
{
	throw 'HTML launcher integration failed'
}
Push-Location $repo
try
{
	if ($Target -ne 'web')
	{
		& "$PSScriptRoot/build.ps1" -Target native
		& "$repo/.tools/python/cmake/data/bin/ctest.exe" --test-dir build/native -C Release --output-on-failure
		if ($LASTEXITCODE)
		{
			throw 'Native system integration failed'
		}
		& "$repo/build/native/Release/native.exe" --build-check
		if ($LASTEXITCODE)
		{
			throw 'Native build check failed'
		}
		$nativeTrace = & "$repo/build/native/Release/phoenix_simulation_tests.exe" --trace
		if ($LASTEXITCODE)
		{
			throw 'Native simulation trace failed'
		}
		Set-Content -LiteralPath "$repo/build/native/continuation.trace" -Value $nativeTrace -Encoding utf8
		& "$PSScriptRoot/check-network-service.ps1"
	}
	if ($Target -ne 'native')
	{
		$nativeTrace = Get-Content -LiteralPath "$repo/build/native/continuation.trace"
		& "$PSScriptRoot/build.ps1" -Target web
		Push-Location build/web
		try
		{
			& $node phoenix_input_check.js
			if ($LASTEXITCODE)
			{
				throw "WASM input/semantics integration failed"
			}
			& $node phoenix_build_check.js
			if ($LASTEXITCODE)
			{
				throw 'WASM build check failed'
			}
			& $node phoenix_content_check.js
			if ($LASTEXITCODE)
			{
				throw 'WASM content integration failed'
			}
			& $node "$repo/tests/system/base_verifier.mjs" './phoenix_workshop_test.js'
			if ($LASTEXITCODE)
			{
				throw 'WASM production launch preflight failed'
			}
			& $node phoenix_simulation_check.js
			if ($LASTEXITCODE)
			{
				throw 'WASM simulation integration failed'
			}
			& $node phoenix_session_check.js
			if ($LASTEXITCODE)
			{
				throw 'WASM session integration failed'
			}
			& $node phoenix_file_check.js
			if ($LASTEXITCODE)
			{
				throw 'WASM file import integration failed'
			}
			& $node phoenix_replication_check.js
			if ($LASTEXITCODE)
			{
				throw 'WASM peer consensus integration failed'
			}
			& $node phoenix_transfer_check.js
			if ($LASTEXITCODE)
			{
				throw 'WASM packet/content transfer integration failed'
			}
			& $node phoenix_fleet_setup_check.js
			if ($LASTEXITCODE)
			{
				throw 'WASM fleet setup integration failed'
			}
			foreach ($gameplayCase in @(
					'phoenix_gameplay_tests.js',
					'phoenix_scenario_tests.js',
					'phoenix_gameplay_knowledge_tests.js',
					'phoenix_gameplay_flight_tests.js',
					'phoenix_scenario_world_tests.js',
					'phoenix_summary_bindings_tests.js',
					'phoenix_swept_collision_tests.js',
					'phoenix_workshop_test_tests.js',
					'phoenix_controller_runtime_tests.js',
					'phoenix_camera_tests.js'
				))
			{
				& $node $gameplayCase
				if ($LASTEXITCODE)
				{
					throw "WASM system integration failed: $gameplayCase"
				}
			}
			$wasmTrace = & $node phoenix_simulation_check.js --trace
			if ($LASTEXITCODE)
			{
				throw 'WASM simulation trace failed'
			}
			if ($nativeTrace -cne $wasmTrace)
			{
				throw 'Native/WASM continuation differs in the same controlled fixture'
			}
		}
		finally
		{
			Pop-Location
		}
	}
}
finally
{
	Pop-Location
}
