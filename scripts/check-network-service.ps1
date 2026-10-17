$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$nodeDirectory = "$repo/.tools/emsdk/node/24.19.0_64bit"
$gitDirectory = Split-Path (Get-Command git).Source -Parent
$openssl = Join-Path (Split-Path $gitDirectory -Parent) 'usr/bin/openssl.exe'
if (!(Test-Path $openssl)) { throw 'Git for Windows OpenSSL is required for the native WSS integration fixture' }
$testDirectory = Join-Path $repo 'build/tls-integration'
New-Item -ItemType Directory -Force $testDirectory | Out-Null
$certificate = Join-Path $testDirectory 'certificate.pem'
$key = Join-Path $testDirectory 'key.pem'
& $openssl req -x509 -newkey rsa:2048 -nodes -keyout $key -out $certificate -days 1 -subj '/CN=localhost' -addext 'subjectAltName=DNS:localhost,IP:127.0.0.1' *> "$testDirectory/generation.log"
if ($LASTEXITCODE) { throw 'TLS integration certificate generation failed' }
$names = @('PATH','PHOENIX_NATIVE_PROBE','PHOENIX_TEST_CERT','PHOENIX_TEST_KEY')
$previous = @{}
foreach ($name in $names) { $previous[$name] = [Environment]::GetEnvironmentVariable($name) }
try {
    $env:PATH = "$nodeDirectory;" + $env:PATH
    $env:PHOENIX_NATIVE_PROBE = "$repo/build/native/Release/phoenix_network_probe.exe"
    $env:PHOENIX_TEST_CERT = $certificate
    $env:PHOENIX_TEST_KEY = $key
    & "$nodeDirectory/npm.cmd" ci --prefix "$repo/service" --ignore-scripts
    if ($LASTEXITCODE) { throw 'Signalling service dependencies failed' }
    & "$nodeDirectory/npm.cmd" test --prefix "$repo/service"
    if ($LASTEXITCODE) { throw 'Signalling/relay/native WSS integration failed' }
} finally { foreach ($name in $names) { [Environment]::SetEnvironmentVariable($name,$previous[$name]) } }
