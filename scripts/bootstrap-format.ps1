#Requires -Version 7.0
param([string]$Python = '', [string]$Npm = 'npm.cmd')
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
Push-Location $repo
try
{
	if (!$Python)
	{
		if (Get-Command python -ErrorAction SilentlyContinue)
		{
			$Python = 'python'
		}
		elseif (Test-Path '.tools/emsdk/python/3.13.3_64bit/python.exe')
		{
			$Python = "$repo/.tools/emsdk/python/3.13.3_64bit/python.exe"
		}
		else
		{
			throw 'Install Python 3.10+ or pass -Python with its executable path.'
		}
	}
	if (!(Test-Path '.tools/format/python/Scripts/python.exe'))
	{
		& $Python -m venv .tools/format/python
		if ($LASTEXITCODE)
		{
			throw 'Could not create the formatting Python environment.'
		}
	}
	& .tools/format/python/Scripts/python.exe -m pip install --disable-pip-version-check -r tools/format/requirements.txt
	if ($LASTEXITCODE)
	{
		throw 'Python formatter installation failed.'
	}
	& $Npm ci --prefix tools/format --ignore-scripts --no-audit --no-fund
	if ($LASTEXITCODE)
	{
		throw 'JavaScript formatter installation failed.'
	}
	$module = '.tools/format/modules/PSScriptAnalyzer/1.25.0/PSScriptAnalyzer.psd1'
	if (!(Test-Path $module))
	{
		Save-Module -Name PSScriptAnalyzer -RequiredVersion 1.25.0 -Repository PSGallery -Path .tools/format/modules -Force
	}
	if (!(Test-Path $module))
	{
		throw 'PowerShell formatter installation failed.'
	}
	Write-Host 'Formatting tools installed. Run ./scripts/format.ps1 or ./scripts/format.ps1 -Check.'
}
finally
{
	Pop-Location
}
