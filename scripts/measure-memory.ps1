param([Parameter(Mandatory=$true)][int]$AppProcessId,[int]$Samples=10)
$clock = [Diagnostics.Stopwatch]::StartNew()
$previousTime = $null
$previousCpu = $null
$readings = for ($sample=0; $sample -lt $Samples; $sample++) {
    $process = Get-Process -Id $AppProcessId -ErrorAction Stop
    $privateWorkingSet = $null
    try {
        $counter = Get-CimInstance Win32_PerfRawData_PerfProc_Process -Filter "IDProcess = $AppProcessId" -ErrorAction Stop
        if ($counter) { $privateWorkingSet = [Math]::Round($counter.WorkingSetPrivate / 1MB, 2) }
    } catch { }
    $time = $clock.Elapsed.TotalSeconds
    $cpu = $process.TotalProcessorTime.TotalSeconds
    $cpuPercent = if ($null -ne $previousTime -and $time -gt $previousTime) {
        [Math]::Round(100 * ($cpu - $previousCpu) / ($time - $previousTime) / [Environment]::ProcessorCount, 3)
    } else { $null }
    [PSCustomObject]@{
        Timestamp = (Get-Date).ToString('o')
        PrivateBytesMiB = [Math]::Round($process.PrivateMemorySize64 / 1MB, 2)
        PrivateWorkingSetMiB = $privateWorkingSet
        WorkingSetMiB = [Math]::Round($process.WorkingSet64 / 1MB, 2)
        CpuPercentOfMachine = $cpuPercent
        Handles = $process.HandleCount
        Threads = $process.Threads.Count
    }
    $previousTime = $time
    $previousCpu = $cpu
    if ($sample+1 -lt $Samples) { Start-Sleep -Milliseconds 1000 }
}
$readings | ConvertTo-Json
