param([switch]$Check)
$ErrorActionPreference = 'Stop'
$repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
Import-Module "$repo/.tools/format/modules/PSScriptAnalyzer/1.25.0/PSScriptAnalyzer.psd1" -Force
$paths = [Console]::In.ReadToEnd() | ConvertFrom-Json
$settings = Import-PowerShellDataFile "$PSScriptRoot/powershell.psd1"
$spaceSettings = @{
	IncludeRules = @('PSUseConsistentIndentation')
	Rules = @{
		PSUseConsistentIndentation = @{
			Enable = $true
			Kind = 'space'
			IndentationSize = 4
			PipelineIndentation = 'IncreaseIndentationForFirstPipeline'
		}
	}
}
$failed = $false
foreach ($path in $paths)
{
	$absolute = Join-Path $repo $path
	$source = [IO.File]::ReadAllText($absolute).Replace("`r`n", "`n")
	$formatted = Invoke-Formatter -ScriptDefinition $source -Settings $settings
	# PSScriptAnalyzer counts a single space as one tab in its indentation check.
	# Establish full indentation first so the tab pass can correct newly expanded blocks.
	$formatted = Invoke-Formatter -ScriptDefinition $formatted -Settings $spaceSettings
	$formatted = (Invoke-Formatter -ScriptDefinition $formatted -Settings $settings).Replace("`r`n", "`n").TrimEnd("`n") + "`n"
	$tokens = $null
	$parseErrors = $null
	$null = [Management.Automation.Language.Parser]::ParseInput($formatted, [ref]$tokens, [ref]$parseErrors)
	if ($parseErrors.Count)
	{
		throw "Invalid PowerShell syntax in ${path}: $($parseErrors.Message -join '; ')"
	}
	# Remove whitespace left by block expansion without changing strings or comments.
	$formatted = [regex]::Replace($formatted, '[ \t]+(?=\n|$)', {
			param($match)
			foreach ($token in $tokens)
			{
				if ($match.Index -ge $token.Extent.StartOffset -and $match.Index -lt $token.Extent.EndOffset)
				{
					return $match.Value
				}
			}
			return ''
		})
	if ($source -cne $formatted)
	{
		if ($Check)
		{
			Write-Host "Needs formatting: $path"; $failed = $true
		}
		else
		{
			[IO.File]::WriteAllText($absolute, $formatted, [Text.UTF8Encoding]::new($false))
		}
	}
}
if ($failed)
{
	exit 1
}
