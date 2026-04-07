param(
  [string]$Port = '',
  [int]$BaudRate = 921600,
  [string]$EsptoolPath = $env:ESPTOOL_PATH,
  [string]$CoreVersion = '',
  [int]$MaxFlashAttempts = 3,
  [switch]$PreflightOnly
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $scriptDir 'esp32_layout_common.ps1')

$fqbn = 'esp32:esp32:lolin_s2_mini'
$core = Get-CoreContext -Fqbn $fqbn -CoreVersion $CoreVersion

if (-not [string]::IsNullOrWhiteSpace($EsptoolPath)) {
  if (-not (Test-Path $EsptoolPath)) {
    throw "esptool non trovato: $EsptoolPath"
  }
} else {
  $EsptoolPath = $core.EsptoolPath
}

if ($core.FlashSizeBytes -ne 4MB) {
  throw "Flash size board/core non coerente con target 4MB: $($core.FlashSize)"
}

$recoveryDir = Join-Path $scriptDir 'ESP32S2_Recovery_OTA'
$buildDir = Join-Path $scriptDir 'build\esp32.esp32.lolin_s2_mini'

$bootloaderBin = Join-Path $buildDir 'ESP32S2_WiFiManager_WebServer.ino.bootloader.bin'
$partitionsBin = Join-Path $buildDir 'ESP32S2_WiFiManager_WebServer.ino.partitions.bin'
$bootAppBin = Join-Path $buildDir 'boot_app0.bin'
$mainBin = Join-Path $scriptDir 'ESP32S2_WiFiManager_WebServer.ino.bin'
$recoveryBin = Join-Path $recoveryDir 'ESP32S2_Recovery_OTA.ino.bin'
$rootPartitionsCsv = Join-Path $scriptDir 'partitions_factory.csv'

if (-not (Test-Path $bootAppBin)) {
  Copy-Item -Path $core.BootApp0Bin -Destination $bootAppBin -Force
}

Assert-FilesExist -Paths @($EsptoolPath, $bootloaderBin, $bootAppBin, $partitionsBin, $mainBin, $recoveryBin, $rootPartitionsCsv)

$sourceMap = Get-PartitionMapFromCsv -CsvPath $rootPartitionsCsv
Assert-RequiredAppLayout -PartitionMap $sourceMap
Assert-TargetPartitionLayout -PartitionMap $sourceMap -SourceLabel 'partitions_factory.csv' -FlashSizeBytes $core.FlashSizeBytes

$decodedPartitionsPath = Join-Path $buildDir '_decoded_partitions_for_flash.csv'
$decodedMap = Decode-PartitionsBin -GenEsp32PartPath $core.GenEsp32PartPath -PartitionsBinPath $partitionsBin -OutputCsvPath $decodedPartitionsPath
Assert-TargetPartitionLayout -PartitionMap $decodedMap -SourceLabel 'partitions.bin (flash)' -FlashSizeBytes $core.FlashSizeBytes
Compare-PartitionMaps -Expected $sourceMap -Actual $decodedMap -ExpectedLabel 'partitions_factory.csv' -ActualLabel 'partitions.bin (flash)'

$factorySlot = Get-PartitionEntry -PartitionMap $sourceMap -Name 'factory'
$ota0Slot = Get-PartitionEntry -PartitionMap $sourceMap -Name 'ota_0'
$ota1Slot = Get-PartitionEntry -PartitionMap $sourceMap -Name 'ota_1'

$mainBinSize = Assert-ImageFitsSlot -ImagePath $mainBin -SlotSizeBytes ([int64]$ota0Slot.Size) -SlotLabel 'ota_0'
$recoveryBinSize = Assert-ImageFitsSlot -ImagePath $recoveryBin -SlotSizeBytes ([int64]$factorySlot.Size) -SlotLabel 'factory'

$bootloaderAddr = $core.BootloaderAddr
$partitionsAddr = $core.PartitionsAddr
$bootApp0Addr = $core.BootApp0Addr
$factoryAddr = Format-PartitionHex ([int64]$factorySlot.Offset)
$ota0Addr = Format-PartitionHex ([int64]$ota0Slot.Offset)
$boardProps = $core.BoardProperties

function Get-BoolBoardProp {
  param(
    [hashtable]$Props,
    [string]$Key,
    [bool]$Default = $false
  )
  if ($null -eq $Props -or -not $Props.ContainsKey($Key)) { return $Default }
  $raw = [string]$Props[$Key]
  if ([string]::IsNullOrWhiteSpace($raw)) { return $Default }
  $v = $raw.Trim().ToLowerInvariant()
  if ($v -in @('1','true','yes','on')) { return $true }
  if ($v -in @('0','false','no','off')) { return $false }
  return $Default
}

$use1200bpsTouch = Get-BoolBoardProp -Props $boardProps -Key 'upload.use_1200bps_touch' -Default $false
$waitForUploadPort = Get-BoolBoardProp -Props $boardProps -Key 'upload.wait_for_upload_port' -Default $false
$disableDTR = Get-BoolBoardProp -Props $boardProps -Key 'serial.disableDTR' -Default $false
$disableRTS = Get-BoolBoardProp -Props $boardProps -Key 'serial.disableRTS' -Default $false

function Convert-ToArray {
  param([object]$InputObject)
  if ($null -eq $InputObject) { return @() }
  if ($InputObject -is [System.Array]) { return @($InputObject) }
  if ($InputObject -is [System.Collections.IEnumerable] -and -not ($InputObject -is [string])) { return @($InputObject) }
  return @($InputObject)
}

function Get-SafeCount {
  param([object]$InputObject)
  return @(Convert-ToArray -InputObject $InputObject).Count
}

function Invoke-1200bpsTouch {
  param([Parameter(Mandatory = $true)][string]$PortName)
  $sp = New-Object System.IO.Ports.SerialPort $PortName,1200,'None',8,'One'
  try {
    if (-not $disableDTR) { $sp.DtrEnable = $false }
    if (-not $disableRTS) { $sp.RtsEnable = $false }
    $sp.Open()
    Start-Sleep -Milliseconds 120
    return @{
      Success = $true
      Error = ''
    }
  } catch {
    return @{
      Success = $false
      Error = $_.Exception.Message
    }
  } finally {
    if ($sp.IsOpen) { $sp.Close() }
    $sp.Dispose()
  }
}

function Wait-ForUploadPort {
  param(
    [string[]]$BeforePorts = @(),
    [int]$TimeoutMs = 7000
  )
  $before = @(Convert-ToArray -InputObject $BeforePorts)
  $deadline = (Get-Date).AddMilliseconds($TimeoutMs)
  $last = @()
  while ((Get-Date) -lt $deadline) {
    $last = @(Get-SystemSerialPorts)
    if ($last.Count -gt 0) {
      $delta = @($last | Where-Object { $before -notcontains $_ })
      if ($delta.Count -gt 0) { return $last }
      if (-not $waitForUploadPort) { return $last }
    }
    Start-Sleep -Milliseconds 200
  }
  return $last
}

function Get-SystemSerialPorts {
  try {
    $raw = [System.IO.Ports.SerialPort]::GetPortNames()
    $ports = @(
      (Convert-ToArray $raw) |
      ForEach-Object { $_.ToString().Trim().ToUpperInvariant() } |
      Where-Object { $_ -ne '' } |
      Sort-Object -Unique
    )
    $wmiHealthy = @()
    try {
      $wmiHealthy = @(
        (Get-CimInstance Win32_PnPEntity -ErrorAction Stop |
          Where-Object {
            $_.Name -match '\(COM\d+\)' -and
            $_.Status -eq 'OK'
          } |
          ForEach-Object {
            $m = [regex]::Match($_.Name, '\((COM\d+)\)')
            if ($m.Success) { $m.Groups[1].Value.ToUpperInvariant() }
          } |
          Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
          Sort-Object -Unique)
      )
    } catch {
      $wmiHealthy = @()
    }
    if ($wmiHealthy.Count -gt 0) {
      return @($ports | Where-Object { $wmiHealthy -contains $_ })
    }
    return $ports
  } catch {
    return @()
  }
}

function Get-ArduinoCliPorts {
  $cli = 'C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe'
  if (-not (Test-Path $cli)) { return @() }
  try {
    $raw = (& $cli board list --format json 2>$null | Out-String).Trim()
    if ([string]::IsNullOrWhiteSpace($raw)) { return @() }
    $obj = $raw | ConvertFrom-Json
    $items = @()
    if ($obj.PSObject.Properties.Name -contains 'detected_ports') {
      $items = Convert-ToArray $obj.detected_ports
    } elseif ($obj.PSObject.Properties.Name -contains 'ports') {
      $items = Convert-ToArray $obj.ports
    } else {
      $items = Convert-ToArray $obj
    }
    $ports = @()
    foreach ($it in $items) {
      if ($null -eq $it) { continue }
      if ($it.PSObject.Properties.Name -contains 'address') {
        $ports += $it.address
      } elseif ($it.PSObject.Properties.Name -contains 'port' -and $it.port.PSObject.Properties.Name -contains 'address') {
        $ports += $it.port.address
      }
    }
    return @(
      (Convert-ToArray $ports) |
      ForEach-Object { $_.ToString().Trim().ToUpperInvariant() } |
      Where-Object { $_ -ne '' } |
      Sort-Object -Unique
    )
  } catch {
    return @()
  }
}

function Get-PortCandidates {
  param([string]$PreferredPort)
  $systemPorts = @(Get-SystemSerialPorts)
  $list = New-Object System.Collections.Generic.List[string]
  if (-not [string]::IsNullOrWhiteSpace($PreferredPort)) {
    $pp = $PreferredPort.Trim().ToUpperInvariant()
    if ($systemPorts -contains $pp) { $list.Add($pp) }
  }
  foreach ($p in Get-ArduinoCliPorts) {
    if (-not [string]::IsNullOrWhiteSpace($p) -and $systemPorts -contains $p) { $list.Add($p) }
  }
  foreach ($p in $systemPorts) { if (-not [string]::IsNullOrWhiteSpace($p)) { $list.Add($p) } }
  return @($list | Select-Object -Unique)
}

function Test-PortOpenable {
  param([Parameter(Mandatory = $true)][string]$PortName)
  $sp = New-Object System.IO.Ports.SerialPort $PortName,115200,'None',8,'One'
  try {
    $sp.ReadTimeout = 300
    $sp.WriteTimeout = 300
    $sp.Open()
    return @{
      Openable = $true
      Error = ''
    }
  } catch {
    return @{
      Openable = $false
      Error = $_.Exception.Message
    }
  } finally {
    if ($sp.IsOpen) { $sp.Close() }
    $sp.Dispose()
  }
}

function Run-Esptool {
  param([string[]]$CliArgs)
  $safeArgs = @(Convert-ToArray -InputObject $CliArgs)
  if ($safeArgs.Count -eq 0) {
    throw 'Run-Esptool: lista argomenti vuota.'
  }

  $outFile = Join-Path $env:TEMP ("esptool_out_" + [Guid]::NewGuid().ToString('N') + ".log")
  $errFile = Join-Path $env:TEMP ("esptool_err_" + [Guid]::NewGuid().ToString('N') + ".log")
  try {
    $argLineParts = @()
    foreach ($a in $safeArgs) {
      $s = if ($null -eq $a) { '' } else { $a.ToString() }
      if ($s -match '[\s"]') {
        $argLineParts += '"' + ($s -replace '"', '\"') + '"'
      } else {
        $argLineParts += $s
      }
    }
    $argLine = ($argLineParts -join ' ')
    $p = Start-Process -FilePath $EsptoolPath -ArgumentList $argLine -NoNewWindow -Wait -PassThru -RedirectStandardOutput $outFile -RedirectStandardError $errFile
    $stdout = if (Test-Path $outFile) { Get-Content -Raw $outFile } else { '' }
    $stderr = if (Test-Path $errFile) { Get-Content -Raw $errFile } else { '' }
    return @{
      ExitCode = [int]$p.ExitCode
      Output = (($stdout + [Environment]::NewLine + $stderr).Trim())
    }
  } finally {
    if (Test-Path $outFile) { Remove-Item $outFile -Force -ErrorAction SilentlyContinue }
    if (Test-Path $errFile) { Remove-Item $errFile -Force -ErrorAction SilentlyContinue }
  }
}

function Probe-ChipId {
  param([string]$ProbePort)
  $probeArgs = @('--chip','esp32s2','--port',$ProbePort,'--baud','115200','chip-id')
  return Run-Esptool -CliArgs $probeArgs
}

Write-Host '============================================================'
Write-Host '  FLASH COMPLETO - ESP32 S2 MINI (SERIALE)'
Write-Host '============================================================'
Write-Host "FQBN:              $fqbn"
Write-Host "Core ESP32:        $($core.CoreVersion)"
Write-Host "esptool:           $EsptoolPath"
Write-Host "Flash size:        $($core.FlashSize)"
Write-Host "Flash mode/freq:   $($core.FlashMode) / $($core.FlashFreq)"
Write-Host "Bootloader offset: $bootloaderAddr"
Write-Host "Partitions offset: $partitionsAddr"
Write-Host "boot_app0 offset:  $bootApp0Addr"
Write-Host "factory offset:    $factoryAddr"
Write-Host "ota_0 offset:      $ota0Addr"
Write-Host "Main size:         $mainBinSize B"
Write-Host "Recovery size:     $recoveryBinSize B"
if (-not [string]::IsNullOrWhiteSpace($Port)) {
  Write-Host "Porta/Baud:        $Port / $BaudRate"
}
Write-Host "1200bps touch:     $use1200bpsTouch"
Write-Host "Wait upload port:  $waitForUploadPort"
Write-Host 'Preflight:         OK'
Write-Host '============================================================'

if ($PreflightOnly) {
  Write-Host 'PreflightOnly=true: nessuna scrittura flash eseguita.'
  exit 0
}

$currentPort = $Port.Trim().ToUpperInvariant()
$allErrors = New-Object System.Collections.Generic.List[string]
$baudPlan = @($BaudRate, 921600, 460800, 230400, 115200) | Select-Object -Unique
$baudIndex = 0

function Get-ActiveBaud {
  if ($baudIndex -lt 0 -or $baudIndex -ge $baudPlan.Count) { return [int]115200 }
  return [int]$baudPlan[$baudIndex]
}

function Try-StepDownBaud {
  if ($baudIndex -lt ($baudPlan.Count - 1)) {
    $baudIndex++
    return $true
  }
  return $false
}

for ($attempt = 1; $attempt -le $MaxFlashAttempts; $attempt++) {
  $candidates = @(Convert-ToArray -InputObject (Get-PortCandidates -PreferredPort $currentPort))
  if ((Get-SafeCount -InputObject $candidates) -eq 0) {
    $allErrors.Add("Tentativo ${attempt}: nessuna porta seriale disponibile.")
    Start-Sleep -Milliseconds 800
    continue
  }

  Write-Host ''
  Write-Host "Tentativo flash $attempt/$MaxFlashAttempts - candidates: $($candidates -join ', ')"

  $deltaPorts = @()
  if ($use1200bpsTouch -and (Get-SafeCount -InputObject $candidates) -gt 0) {
    $beforePorts = @(Get-SystemSerialPorts)
    $touched = $false
    foreach ($cand in $candidates) {
      $touch = Invoke-1200bpsTouch -PortName $cand
      if ($touch.Success) {
        $touched = $true
        $allErrors.Add("Tentativo $attempt, 1200bps-touch ok su $cand")
        break
      }
      $allErrors.Add("Tentativo $attempt, 1200bps-touch fail su ${cand}: $($touch.Error)")
    }
    if ($touched) {
      $afterPorts = @(Wait-ForUploadPort -BeforePorts $beforePorts -TimeoutMs 7000)
      if ($afterPorts.Count -gt 0) {
        $deltaPorts = @($afterPorts | Where-Object { $beforePorts -notcontains $_ })
        $candidates = @(Convert-ToArray -InputObject (Get-PortCandidates -PreferredPort $currentPort))
      }
    }
  }

  $orderedCandidates = @($deltaPorts + $candidates | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Select-Object -Unique)
  if ($orderedCandidates.Count -eq 0) {
    $allErrors.Add("Tentativo ${attempt}: nessuna COM candidata disponibile.")
    Start-Sleep -Milliseconds 1000
    continue
  }

  $flashSuccess = $false
  foreach ($cand in $orderedCandidates) {
    if ($cand -ne $currentPort) {
      Write-Host "Auto-port switch: $currentPort -> $cand"
      $currentPort = $cand
    }

    $flashArgs = @(
      '--chip','esp32s2',
      '--port',$currentPort,
      '--baud',"$([int](Get-ActiveBaud))",
      '--connect-attempts','15',
      '--before','default-reset',
      '--after','hard-reset',
      'write-flash',
      '--flash-mode',"$($core.FlashMode)",
      '--flash-freq',"$($core.FlashFreq)",
      '--flash-size',"$($core.FlashSize)",
      "$bootloaderAddr",$bootloaderBin,
      "$partitionsAddr",$partitionsBin,
      "$bootApp0Addr",$bootAppBin,
      "$factoryAddr",$recoveryBin,
      "$ota0Addr",$mainBin
    )

    $flash = Run-Esptool -CliArgs $flashArgs
    if ($flash.ExitCode -eq 0) {
      $flashSuccess = $true
      break
    }
    $allErrors.Add("Tentativo $attempt flash failed on ${currentPort}: $($flash.Output.Trim())")
  }

  if ($flashSuccess) {
    Write-Host ''
    Write-Host '============================================================'
    Write-Host '  FLASH COMPLETATO'
    Write-Host "  Porta usata: $currentPort"
    Write-Host '  Boot previsto: ota_0 (main). Recovery factory preservata.'
    Write-Host '============================================================'
    exit 0
  }

  $outLower = ($allErrors | Select-Object -Last 1 | Out-String).ToLowerInvariant()
  if ($outLower -match 'lost connection|port is not open|cannot configure port|could not open .*port|file not found') {
    $portsNow = @(Get-PortCandidates -PreferredPort $currentPort)
    if ($portsNow.Count -gt 0 -and -not ($portsNow -contains $currentPort)) {
      $old = $currentPort
      $currentPort = $portsNow[0]
      $allErrors.Add("Tentativo ${attempt}: porta cambiata $old -> $currentPort dopo disconnessione")
    }
  }

  if (Try-StepDownBaud) {
    $newBaud = Get-ActiveBaud
    Write-Host "Flash fallito su $currentPort, retry con baud $newBaud..."
  }
  Start-Sleep -Milliseconds 1000
}

$detail = ($allErrors | Select-Object -Last 6) -join "`n----`n"
throw "Flash fallito dopo $MaxFlashAttempts tentativi. Ultimi errori:`n$detail"



