param([string]$ReaderUrl='http://192.168.4.1',[string]$Ssid,[Security.SecureString]$Password,[switch]$Clear)
$ErrorActionPreference='Stop'
if($ReaderUrl-ne'http://192.168.4.1'){throw 'Join the reader hotspot and use http://192.168.4.1.'}
if(-not $Clear){if(-not $Ssid){$Ssid=Read-Host 'Clock Wi-Fi SSID'};if(-not $Password){$Password=Read-Host 'Clock Wi-Fi password' -AsSecureString}}
$session=Invoke-RestMethod -Uri "$ReaderUrl/api/session"
if($session.product-ne'InkReader'-or$session.api-ne1-or-not$session.clock){throw 'Compatible clock-enabled reader not found.'}
$headers=@{'X-Reader-Token'=$session.token;'X-Reader-Operation'='1';'X-Reader-Request'=[string](Get-Random -Minimum 1 -Maximum 1000000000)}
$null=Invoke-RestMethod -Uri "$ReaderUrl/api/op" -Method Post -Headers $headers -Body ([Text.Encoding]::UTF8.GetBytes('{}'))
$pointer=[IntPtr]::Zero;$plain=''
try{
 if($Clear){$metadata=@{clear=$true}}else{
  $pointer=[Runtime.InteropServices.Marshal]::SecureStringToBSTR($Password)
  $plain=[Runtime.InteropServices.Marshal]::PtrToStringBSTR($pointer)
  $metadata=@{ssid=$Ssid;password=$plain}
 }
 $headers['X-Reader-Operation']='14';$headers['X-Reader-Request']=[string](Get-Random -Minimum 1 -Maximum 1000000000)
 $null=Invoke-RestMethod -Uri "$ReaderUrl/api/op" -Method Post -Headers $headers -Body ([Text.Encoding]::UTF8.GetBytes(($metadata|ConvertTo-Json -Compress)))
 Write-Output $(if($Clear){'Clock credentials cleared.'}else{'Clock Wi-Fi saved. Password not logged.'})
}finally{
 if($pointer-ne[IntPtr]::Zero){[Runtime.InteropServices.Marshal]::ZeroFreeBSTR($pointer)}
 if($metadata){$metadata.Clear()};$plain='';$session=$null
}
