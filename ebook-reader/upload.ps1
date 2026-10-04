param([string]$Port='COM6',[switch]$BuildOnly,[switch]$DisableClock)
& (Join-Path (Split-Path $PSScriptRoot -Parent) 'tools/build.ps1') -Upload:(!$BuildOnly) -Port $Port -DisableClock:$DisableClock
