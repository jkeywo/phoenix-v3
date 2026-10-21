param([string]$Python = 'python')
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$environment = Join-Path $repo 'build/inference-venv'
& $Python -m venv $environment
if ($LASTEXITCODE)
{
	throw 'Cannot create the inference environment'
}
$runtime = Join-Path $environment 'Scripts/python.exe'
& $runtime -m pip install 'torch==2.11.0+cu130' --index-url https://download.pytorch.org/whl/cu130
if ($LASTEXITCODE)
{
	throw 'CUDA PyTorch installation failed'
}
& $runtime -m pip install -r "$repo/tools/inference/requirements.txt"
if ($LASTEXITCODE)
{
	throw 'AnyJev installation failed'
}
& $runtime -c 'import torch; assert torch.cuda.is_available(), "CUDA is unavailable"; print(torch.cuda.get_device_name())'
if ($LASTEXITCODE)
{
	throw 'CUDA verification failed'
}
Write-Output 'Inference environment ready. See docs/controller-models.md for model setup and pairing.'
