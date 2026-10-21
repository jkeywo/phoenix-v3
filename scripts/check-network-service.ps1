$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$nodeDirectory = "$repo/.tools/emsdk/node/24.19.0_64bit"
$gitDirectory = Split-Path (Get-Command git).Source -Parent
$openssl = Join-Path (Split-Path $gitDirectory -Parent) 'usr/bin/openssl.exe'
if (!(Test-Path $openssl))
{
	throw 'Git for Windows OpenSSL is required for the native WSS integration fixture'
}
$testDirectory = Join-Path $repo ("build/tls-integration-" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $testDirectory | Out-Null
$certificate = Join-Path $testDirectory 'certificate.pem'
$key = Join-Path $testDirectory 'key.pem'
& $openssl req -x509 -newkey rsa:2048 -nodes -keyout $key -out $certificate -days 1 -subj '/CN=localhost' -addext 'subjectAltName=DNS:localhost,IP:127.0.0.1' *> "$testDirectory/generation.log"
if ($LASTEXITCODE)
{
	throw 'TLS integration certificate generation failed'
}
$names = @('PATH', 'PHOENIX_NATIVE_PROBE', 'PHOENIX_SESSION_PROBE', 'PHOENIX_FLEET_PROBE', 'PHOENIX_LOBBY_PROBE', 'PHOENIX_INFERENCE_PROBE', 'PHOENIX_INFERENCE_BRIDGE', 'PHOENIX_TEST_CERT', 'PHOENIX_TEST_KEY')
$previous = @{}
foreach ($name in $names)
{
	$previous[$name] = [Environment]::GetEnvironmentVariable($name)
}
try
{
	$env:PATH = "$nodeDirectory;" + $env:PATH
	# Run a private executable snapshot so another build can relink the original Windows binaries.
	$probeDirectory = Join-Path $repo ("build/network-check-" + [Guid]::NewGuid().ToString('N'))
	New-Item -ItemType Directory -Path $probeDirectory | Out-Null
	foreach ($probe in @('phoenix_network_probe', 'phoenix_session_probe', 'phoenix_fleet_probe', 'phoenix_lobby_tests', 'phoenix_inference_probe', 'phoenix_inference_bridge'))
	{
		Copy-Item -LiteralPath "$repo/build/native/Release/$probe.exe" -Destination "$probeDirectory/$probe.exe"
	}
	$env:PHOENIX_NATIVE_PROBE = "$probeDirectory/phoenix_network_probe.exe"
	$env:PHOENIX_SESSION_PROBE = "$probeDirectory/phoenix_session_probe.exe"
	$env:PHOENIX_FLEET_PROBE = "$probeDirectory/phoenix_fleet_probe.exe"
	$env:PHOENIX_LOBBY_PROBE = "$probeDirectory/phoenix_lobby_tests.exe"
	$env:PHOENIX_INFERENCE_PROBE = "$probeDirectory/phoenix_inference_probe.exe"
	$env:PHOENIX_INFERENCE_BRIDGE = "$probeDirectory/phoenix_inference_bridge.exe"
	$env:PHOENIX_TEST_CERT = $certificate
	$env:PHOENIX_TEST_KEY = $key
	& "$nodeDirectory/npm.cmd" ci --prefix "$repo/service" --ignore-scripts
	if ($LASTEXITCODE)
	{
		throw 'Signalling service dependencies failed'
	}
	& "$nodeDirectory/npm.cmd" test --prefix "$repo/service"
	if ($LASTEXITCODE)
	{
		throw 'Signalling/relay/native WSS integration failed'
	}
}
finally
{
	foreach ($name in $names)
	{
		[Environment]::SetEnvironmentVariable($name, $previous[$name])
	}
	if ($probeDirectory -and (Test-Path -LiteralPath $probeDirectory))
	{
		$resolvedProbeDirectory = [System.IO.Path]::GetFullPath($probeDirectory)
		$buildDirectory = [System.IO.Path]::GetFullPath((Join-Path $repo 'build')) + [System.IO.Path]::DirectorySeparatorChar
		if (!$resolvedProbeDirectory.StartsWith($buildDirectory, [System.StringComparison]::OrdinalIgnoreCase))
		{
			throw 'Refusing to remove a probe directory outside the build workspace'
		}
		Remove-Item -LiteralPath $resolvedProbeDirectory -Recurse -Force
	}
}
