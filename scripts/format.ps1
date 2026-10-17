#Requires -Version 7.0
param([switch]$Check)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$node = 'node'
if (!(Get-Command $node -ErrorAction SilentlyContinue))
{
	$node = Join-Path $repo '.tools/emsdk/node/24.19.0_64bit/node.exe'
}
$formatArguments = @("$repo/tools/format/run.cjs")
if ($Check)
{
	$formatArguments += '--check'
}
& $node @formatArguments
if ($LASTEXITCODE)
{
	throw 'Formatting failed. Run ./scripts/format.ps1 to fix formatting drift.'
}
