param([switch]$VerifyOnly)
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$manifest=Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'vendor-dependencies.json') | ConvertFrom-Json
foreach($entry in $manifest.files){
 $target=Join-Path (Join-Path $repo 'ebook-reader') $entry.name
 if($VerifyOnly){if(!(Test-Path -LiteralPath $target)){throw "Missing local dependency: $($entry.name)"};continue}
 if(Test-Path -LiteralPath $target){throw "Dependency exists: $($entry.name). Use -VerifyOnly to check it; this script never overwrites local files."}
}
if(!$VerifyOnly){
 foreach($entry in $manifest.files){
  $target=Join-Path (Join-Path $repo 'ebook-reader') $entry.name
  Invoke-WebRequest -Uri $entry.url -OutFile $target -UseBasicParsing
  if((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLower() -ne $entry.sha256){throw "Upstream checksum mismatch: $($entry.name)"}
  $text=[IO.File]::ReadAllText($target).Replace("`r`n","`n")
  [IO.File]::WriteAllText($target,$text,[Text.UTF8Encoding]::new($false))
 }
 Push-Location $repo
 try { & git apply --unidiff-zero --whitespace=nowarn 'patches/display-safety.patch';if($LASTEXITCODE-ne 0){throw 'Display safety patch failed'} }finally{Pop-Location}
}
foreach($entry in $manifest.files){
 $target=Join-Path (Join-Path $repo 'ebook-reader') $entry.name
 $text=[IO.File]::ReadAllText($target).Replace("`r`n","`n")
 $digest=[Security.Cryptography.SHA256]::Create();try{$hash=([BitConverter]::ToString($digest.ComputeHash([Text.Encoding]::UTF8.GetBytes($text)))).Replace('-','').ToLower()}finally{$digest.Dispose()}
 if($hash-ne$entry.result_sha256){throw "Patched dependency differs: $($entry.name)"}
}
Write-Output 'Verified pinned local display dependencies and project safety patch.'
