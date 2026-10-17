param([ValidateRange(1, 65535)][int]$Port = 8080)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$webRoot = Join-Path $repo 'build/web'
if (!(Test-Path (Join-Path $webRoot 'index.html')))
{
	throw 'Run scripts/build.ps1 -Target web first.'
}
$python = Get-ChildItem (Join-Path $repo '.tools/emsdk/python/*/python.exe') -ErrorAction SilentlyContinue |
	Select-Object -First 1 -ExpandProperty FullName
if (!$python)
{
	$python = (Get-Command python -ErrorAction Stop).Source
}
Write-Host "Starting Phoenix web server at http://127.0.0.1:$Port/"
& $python -u -m http.server $Port --bind 127.0.0.1 --directory $webRoot
if ($LASTEXITCODE)
{
	throw "Web server exited with code $LASTEXITCODE."
}
