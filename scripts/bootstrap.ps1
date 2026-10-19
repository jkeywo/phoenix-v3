$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
Push-Location $repo
try
{
	python -m pip install --target .tools/python cmake==3.31.6 ninja==1.11.1.3
	if ($LASTEXITCODE)
	{
		throw 'Build tool installation failed'
	}
	if (!(Test-Path .tools/emsdk))
	{
		git clone https://github.com/emscripten-core/emsdk.git .tools/emsdk
		if ($LASTEXITCODE)
		{
			throw 'SDK clone failed'
		}
	}
	git -C .tools/emsdk checkout 1c9a998ee5dd2e38aec0c5abbf23dffdb8f10d57
	if ($LASTEXITCODE)
	{
		throw 'SDK revision unavailable'
	}
	$sdk = if ([Environment]::OSVersion.Platform -eq [PlatformID]::Win32NT)
	{
		'./.tools/emsdk/emsdk.bat'
	}
	else
	{
		'./.tools/emsdk/emsdk'
	}
	& $sdk install 6.0.12
	if ($LASTEXITCODE)
	{
		throw 'SDK installation failed'
	}
	& $sdk activate 6.0.12
	if ($LASTEXITCODE)
	{
		throw 'SDK activation failed'
	}
}
finally
{
	Pop-Location
}
