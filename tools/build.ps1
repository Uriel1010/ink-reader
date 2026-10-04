param([string]$ArduinoCli='', [switch]$Upload, [string]$Port='COM6', [switch]$DisableClock)
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
& (Join-Path $PSScriptRoot 'setup-vendor.ps1') -VerifyOnly
if(!$ArduinoCli){$found=Get-Command arduino-cli -ErrorAction SilentlyContinue;if($found){$ArduinoCli=$found.Source}else{$ArduinoCli=Join-Path $env:LOCALAPPDATA 'Programs/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe'}}
if(!(Test-Path -LiteralPath $ArduinoCli)){throw 'Install Arduino CLI or specify -ArduinoCli.'}
$taskTemp=Join-Path ([IO.Path]::GetTempPath()) ('ink-reader-'+[guid]::NewGuid().ToString('N'))
$sketch=Join-Path $taskTemp 'ebook-reader';$build=Join-Path $taskTemp 'build'
New-Item -ItemType Directory -Path $sketch -Force | Out-Null
Get-ChildItem -LiteralPath (Join-Path $repo 'ebook-reader') -File | Where-Object {$_.Extension -in '.ino','.h','.cpp','.c','.t','.inl'} | ForEach-Object {Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $sketch $_.Name)}
$fqbn='esp32:esp32:esp32s3:FlashSize=8M,PSRAM=opi,CDCOnBoot=default,PartitionScheme=default_8MB'
$argsList=@('compile','--fqbn',$fqbn,'--build-path',$build)
if($DisableClock){$argsList+=@('--build-property','compiler.cpp.extra_flags=-DREADER_ENABLE_CLOCK_SCREENSAVER=0')}
$argsList+=$sketch
& $ArduinoCli @argsList
if($LASTEXITCODE -ne 0){throw 'Compilation failed.'}
if((Get-Item -LiteralPath (Join-Path $build 'ebook-reader.ino.bin')).Length -gt 3342336){throw 'Application exceeds partition size.'}
Write-Output "Build output: $build"
if($Upload){& $ArduinoCli upload --fqbn $fqbn --port $Port --input-dir $build;if($LASTEXITCODE -ne 0){throw 'Upload failed.'}}
