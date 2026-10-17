@{
	IncludeRules = @(
		'PSPlaceOpenBrace',
		'PSPlaceCloseBrace',
		'PSUseConsistentIndentation',
		'PSUseConsistentWhitespace'
	)
	Rules = @{
		PSPlaceOpenBrace = @{
			Enable = $true
			OnSameLine = $false
			NewLineAfter = $true
			IgnoreOneLineBlock = $false
		}
		PSPlaceCloseBrace = @{
			Enable = $true
			NewLineAfter = $true
			IgnoreOneLineBlock = $false
			NoEmptyLineBefore = $true
		}
		PSUseConsistentIndentation = @{
			Enable = $true
			Kind = 'tab'
			IndentationSize = 4
			PipelineIndentation = 'IncreaseIndentationForFirstPipeline'
		}
		PSUseConsistentWhitespace = @{
			Enable = $true
			CheckInnerBrace = $true
			CheckOpenBrace = $true
			CheckOpenParen = $true
			CheckOperator = $true
			CheckSeparator = $true
		}
	}
}
