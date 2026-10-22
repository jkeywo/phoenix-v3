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
		foreach ($supplyCaseName in @('logistics', 'docking', 'cargo'))
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
					'phoenix_sensors_tests.js',
					'phoenix_psychic_tests.js',
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
			$combatNativeTrace = Get-Content -LiteralPath "$repo/build/native/combat-continuation.trace"
			$combatWebTrace = & $node phoenix_combat_tests.js --trace
			if ($LASTEXITCODE -or ($combatNativeTrace -join "`n").Trim() -ne ($combatWebTrace -join "`n").Trim())
			{
				throw 'Native/WASM combat continuation differs'
			}
			Write-Output 'PASS native/WASM combat projectile, refill and team-travel continuation'
			$controllerNativeTrace = Get-Content -LiteralPath "$repo/build/native/controller-continuation.trace"
			$controllerWebTrace = & $node phoenix_controller_runtime_tests.js --trace
			if ($LASTEXITCODE -or ($controllerNativeTrace -join "`n").Trim() -ne ($controllerWebTrace -join "`n").Trim())
			{
				throw 'Native/WASM Controller action continuation differs'
			}
			Write-Output 'PASS native/WASM Controller action and coordination continuation'
			$commsNativeTrace = Get-Content -LiteralPath "$repo/build/native/conversation-continuation.trace"
			$commsWebTrace = & $node phoenix_conversation_tests.js --trace
			if ($LASTEXITCODE -or ($commsNativeTrace -join "`n").Trim() -ne ($commsWebTrace -join "`n").Trim())
			{
				throw 'Native/WASM Comms continuation differs'
			}
			Write-Output 'PASS native/WASM Comms conversation continuation'
			$psychicNativeTrace = Get-Content -LiteralPath "$repo/build/native/psychic-continuation.trace"
			$psychicWebTrace = & $node phoenix_psychic_tests.js --trace
			if ($LASTEXITCODE -or ($psychicNativeTrace -join "`n").Trim() -ne ($psychicWebTrace -join "`n").Trim())
			{
				throw 'Psychic native/WASM continuation differs'
			}
			$sensorNativeTrace = Get-Content -LiteralPath "$repo/build/native/sensor-continuation.trace"
			$sensorWebTrace = & $node phoenix_sensors_tests.js --trace
			if ($LASTEXITCODE -or ($sensorNativeTrace -join "`n").Trim() -ne ($sensorWebTrace -join "`n").Trim())
			{
				throw 'Native/WASM sensor continuation differs'
			}
			Write-Output 'PASS native/WASM sensor traversal and partial acquisition continuation'
			$medicalNativeTrace = Get-Content -LiteralPath "$repo/build/native/medical-continuation.trace"
			$medicalWebTrace = & $node phoenix_medical_tests.js --trace
			if ($LASTEXITCODE -or ($medicalNativeTrace -join "`n").Trim() -ne ($medicalWebTrace -join "`n").Trim())
			{
				throw 'Native/WASM Medical continuation differs'
			}
			Write-Output 'PASS native/WASM Medical continuation'
			foreach ($supplyCaseName in @('logistics', 'docking', 'cargo'))
			{
				$supplyNativeTrace = Get-Content -LiteralPath "$repo/build/native/$supplyCaseName-continuation.trace"
				$supplyWebTrace = & $node "phoenix_${supplyCaseName}_tests.js" --trace
				if ($LASTEXITCODE -or ($supplyNativeTrace -join "`n").Trim() -ne ($supplyWebTrace -join "`n").Trim())
				{
					throw "Native/WASM $supplyCaseName continuation differs"
				}
			}
			Write-Output 'PASS native/WASM storage, docking and physical cargo continuation'

			$securityNativeTrace = Get-Content -LiteralPath "$repo/build/native/security-continuation.trace"
			$securityWebTrace = & $node phoenix_security_tests.js --trace
			if ($LASTEXITCODE -or ($securityNativeTrace -join "`n").Trim() -ne ($securityWebTrace -join "`n").Trim())
			{
				throw 'Native/WASM security continuation differs'
			}
			Write-Output 'PASS native/WASM security continuation'
			$awayNativeTrace = Get-Content -LiteralPath "$repo/build/native/away-continuation.trace"
			$awayWebTrace = & $node phoenix_away_tests.js --trace
			if ($LASTEXITCODE -or ($awayNativeTrace -join "`n").Trim() -ne ($awayWebTrace -join "`n").Trim())
			{
				throw 'Native/WASM away continuation differs'
			}
			Write-Output 'PASS native/WASM away mission continuation'
			$craftNativeTrace = Get-Content -LiteralPath "$repo/build/native/craft-continuation.trace"
			$craftWebTrace = & $node phoenix_craft_tests.js --trace
			if ($LASTEXITCODE -or ($craftNativeTrace -join "`n").Trim() -ne ($craftWebTrace -join "`n").Trim())
			{
				throw 'Native/WASM craft continuation differs'
			}
			Write-Output 'PASS native/WASM craft, sortie and transport continuation'
			$ftlNativeTrace = Get-Content -LiteralPath "$repo/build/native/ftl-continuation.trace"
			$ftlWebTrace = & $node phoenix_ftl_tests.js --trace
			if ($LASTEXITCODE -or ($ftlNativeTrace -join "`n").Trim() -ne ($ftlWebTrace -join "`n").Trim())
			{
				throw 'Native/WASM FTL continuation differs'
			}
			Write-Output 'PASS native/WASM FTL reserve and departure continuation'
			$missionNativeTrace = Get-Content -LiteralPath "$repo/build/native/mission-continuation.trace"
			$missionWebTrace = & $node phoenix_missions_tests.js --trace
			if ($LASTEXITCODE -or ($missionNativeTrace -join "`n").Trim() -ne ($missionWebTrace -join "`n").Trim())
			{
				throw 'Native/WASM mission continuation differs'
			}
			Write-Output 'PASS native/WASM objectives, events, losses and frozen reports continuation'
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
