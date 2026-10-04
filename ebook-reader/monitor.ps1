param([string]$Port = 'COM6', [int]$Seconds = 60)
$serial = [System.IO.Ports.SerialPort]::new($Port, 115200, 'None', 8, 'One')
$serial.ReadTimeout = 200
$serial.DtrEnable = $false
$serial.RtsEnable = $false
$logPath = Join-Path $PSScriptRoot 'serial-log.txt'
try {
    $serial.Open()
    $serial.Write('s')
    $endTime = [DateTime]::UtcNow.AddSeconds($Seconds)
    while ([DateTime]::UtcNow -lt $endTime) {
        try {
            $line = $serial.ReadLine().TrimEnd()
            $line
            Add-Content -LiteralPath $logPath -Value $line -Encoding utf8
        } catch [System.TimeoutException] {}
    }
} finally {
    if ($serial.IsOpen) { $serial.Close() }
    $serial.Dispose()
}
