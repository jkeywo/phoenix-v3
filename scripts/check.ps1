param(
	[ValidateSet('all', 'native', 'web')][string]$Target = 'all',
	[switch]$Threaded,
	[ValidateRange(1, 1024)][int]$Jobs = 8,
	[string]$ContinuationOutputDirectory
)
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
& $node --test "$repo/tests/system/web_launcher.mjs" "$repo/tests/system/wasm_trace.mjs" "$repo/tests/system/wasm_continuations.mjs"
if ($LASTEXITCODE)
{
	throw 'HTML launcher or WASM trace capture integration failed'
}
Push-Location $repo
try
{
	if ($Target -ne 'web')
	{
		& "$PSScriptRoot/build.ps1" -Target native -Jobs $Jobs
		& "$repo/.tools/python/cmake/data/bin/ctest.exe" --test-dir build/native -C Release --output-on-failure
		if ($LASTEXITCODE)
		{
			throw 'Native system integration failed'
		}
		& "$repo/build/native/Release/native.exe" --build-check
		if ($LASTEXITCODE)
		{
			throw ("Native build check failed (exit code {0}, 0x{0:X8})" -f $LASTEXITCODE)
		}
		$nativeTrace = & "$repo/build/native/Release/phoenix_simulation_tests.exe" --trace
		if ($LASTEXITCODE)
		{
			throw 'Native simulation trace failed'
		}
		Set-Content -LiteralPath "$repo/build/native/continuation.trace" -Value $nativeTrace -Encoding utf8
		$missionNativeTrace = & ./build/native/Release/phoenix_missions_tests.exe --trace
		if ($LASTEXITCODE)
		{
			throw "Native mission continuation failed"
		}
		Set-Content -LiteralPath "$repo/build/native/mission-continuation.trace" -Value $missionNativeTrace -Encoding utf8
		$interdictionTrace = & ./build/native/Release/phoenix_interdictions_tests.exe --trace
		if ($LASTEXITCODE)
		{
			throw 'Native interdiction continuation failed'
		}
		Set-Content -LiteralPath "$repo/build/native/interdiction-continuation.trace" -Value $interdictionTrace -Encoding utf8
		$combatNativeTrace = & "$repo/build/native/Release/phoenix_combat_tests.exe" --trace
		if ($LASTEXITCODE)
		{
			throw "Native combat continuation trace failed"
		}
		Set-Content -LiteralPath "$repo/build/native/combat-continuation.trace" -Value $combatNativeTrace -Encoding utf8
		$commsNativeTrace = & "$repo/build/native/Release/phoenix_conversation_tests.exe" --trace
		if ($LASTEXITCODE)
		{
			throw 'Native Comms continuation trace failed'
		}
		Set-Content -LiteralPath "$repo/build/native/conversation-continuation.trace" -Value $commsNativeTrace -Encoding utf8
		$sensorNativeTrace = & "$repo/build/native/Release/phoenix_sensors_tests.exe" --trace
		if ($LASTEXITCODE)
		{
			throw 'Native sensor continuation failed'
		}
		Set-Content -LiteralPath "$repo/build/native/sensor-continuation.trace" -Value $sensorNativeTrace -Encoding utf8
		$medicalNativeTrace = & "$repo/build/native/Release/phoenix_medical_tests.exe" --trace
		if ($LASTEXITCODE)
		{
			throw 'Native Medical continuation failed'
		}
		Set-Content -LiteralPath "$repo/build/native/medical-continuation.trace" -Value $medicalNativeTrace -Encoding utf8
		$securityNativeTrace = & "$repo/build/native/Release/phoenix_security_tests.exe" --trace
		if ($LASTEXITCODE)
		{
			throw 'Native security continuation failed'
		}
		Set-Content -LiteralPath "$repo/build/native/security-continuation.trace" -Value $securityNativeTrace -Encoding utf8
		$awayNativeTrace = & "$repo/build/native/Release/phoenix_away_tests.exe" --trace
		if ($LASTEXITCODE)
		{
			throw 'Native away continuation trace failed'
		}
		Set-Content -LiteralPath "$repo/build/native/away-continuation.trace" -Value $awayNativeTrace -Encoding utf8
		$craftNativeTrace = & "$repo/build/native/Release/phoenix_craft_tests.exe" --trace
		if ($LASTEXITCODE)
		{
			throw 'Native craft continuation trace failed'
		}
		Set-Content -LiteralPath "$repo/build/native/craft-continuation.trace" -Value $craftNativeTrace -Encoding utf8
		$ftlNativeTrace = & "$repo/build/native/Release/phoenix_ftl_tests.exe" --trace
		if ($LASTEXITCODE)
		{
			throw 'Native FTL continuation trace failed'
		}
		Set-Content -LiteralPath "$repo/build/native/ftl-continuation.trace" -Value $ftlNativeTrace -Encoding utf8
		foreach ($supplyCaseName in @('logistics', 'docking', 'cargo', 'processors', 'store_effects', 'directed', 'abilities', 'rules', 'custom_properties', 'processes'))
		{
			$supplyNativeTrace = & "$repo/build/native/Release/phoenix_${supplyCaseName}_tests.exe" --trace
			if ($LASTEXITCODE)
			{
				throw "Native $supplyCaseName continuation failed"
			}
			Set-Content -LiteralPath "$repo/build/native/$supplyCaseName-continuation.trace" -Value $supplyNativeTrace -Encoding utf8
		}
		& "$PSScriptRoot/check-network-service.ps1"
		$psychicNativeTrace = & "$repo/build/native/Release/phoenix_psychic_tests.exe" --trace
		if ($LASTEXITCODE)
		{
			throw 'Native psychic continuation trace failed'
		}
		Set-Content -LiteralPath "$repo/build/native/psychic-continuation.trace" -Value $psychicNativeTrace -Encoding utf8
		$controllerNativeTrace = & "$repo/build/native/Release/phoenix_controller_runtime_tests.exe" --trace
		if ($LASTEXITCODE)
		{
			throw 'Native Controller action continuation trace failed'
		}
		Set-Content -LiteralPath "$repo/build/native/controller-continuation.trace" -Value $controllerNativeTrace -Encoding utf8
	}
	if ($Target -ne 'native')
	{
		if (!$ContinuationOutputDirectory)
		{
			$null = Get-Content -LiteralPath "$repo/build/native/continuation.trace"
		}
		$continuationOutput = if ($ContinuationOutputDirectory)
		{
			[IO.Path]::GetFullPath($ContinuationOutputDirectory, $repo)
		}
		else
		{
			Join-Path $repo 'build/web-continuations/local'
		}
		$webOptions = @{}
		if ($PSBoundParameters.ContainsKey('Threaded'))
		{
			$webOptions.Threaded = $Threaded
		}
		# The complete gate validates ordinary Release linking, even after local iteration.
		& "$PSScriptRoot/build.ps1" -Target web -Jobs $Jobs -DevelopmentLink:$false @webOptions
		Push-Location build/web
		try
		{
			& $node phoenix_input_check.js
			if ($LASTEXITCODE)
			{
				throw "WASM input/semantics integration failed"
			}
			& $node --import "../../tests/support/browser_blob_host.mjs" phoenix_browser_blob_tests.js
			if ($LASTEXITCODE)
			{
				throw 'WASM browser blob contract integration failed'
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
					'phoenix_processors_tests.js',
					'phoenix_store_effects_tests.js',
					'phoenix_directed_tests.js',
					'phoenix_rules_tests.js',
					'phoenix_custom_properties_tests.js',
					'phoenix_processes_tests.js',
					'phoenix_scenario_tests.js',
					'phoenix_gameplay_knowledge_tests.js',
					'phoenix_gameplay_flight_tests.js',
					'phoenix_scenario_world_tests.js',
					'phoenix_summary_bindings_tests.js',
					'phoenix_swept_collision_tests.js',
					'phoenix_workshop_test_tests.js',
					'phoenix_personnel_tests.js',
					'phoenix_medical_tests.js',
					'phoenix_logistics_tests.js',
					'phoenix_docking_tests.js',
					'phoenix_cargo_tests.js',
					'phoenix_security_tests.js',
					'phoenix_away_tests.js',
					'phoenix_craft_tests.js',
					'phoenix_ftl_tests.js',
					'phoenix_missions_tests.js',
					'phoenix_interdictions_tests.js',
					'phoenix_sensors_tests.js',
					'phoenix_psychic_tests.js',
					'phoenix_abilities_tests.js',
					'phoenix_controller_runtime_tests.js',
					'phoenix_combat_tests.js',
					'phoenix_conversation_tests.js',
					'phoenix_camera_tests.js'
				))
			{
				& $node $gameplayCase
				if ($LASTEXITCODE)
				{
					throw "WASM system integration failed: $gameplayCase"
				}
			}
			& $node "$repo/scripts/wasm-continuations.mjs" record "$repo/build/web" $continuationOutput
			if ($LASTEXITCODE)
			{
				throw 'WASM continuation recording failed'
			}
			if (!$ContinuationOutputDirectory)
			{
				& $node "$repo/scripts/wasm-continuations.mjs" compare "$repo/build/native" $continuationOutput
				if ($LASTEXITCODE)
				{
					throw 'Native/WASM continuation differs'
				}
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
