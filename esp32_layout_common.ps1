Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Convert-PartitionNumber {
  param([Parameter(Mandatory = $true)] [string]$Value)

  $v = $Value.Trim()
  if ([string]::IsNullOrWhiteSpace($v)) {
    throw "Valore numerico partizione vuoto"
  }

  if ($v -match '^(?i)0x[0-9a-f]+$') {
    return [Convert]::ToInt64($v.Substring(2), 16)
  }
  if ($v -match '^\d+$') {
    return [Convert]::ToInt64($v, 10)
  }
  if ($v -match '^(?<num>\d+)\s*(?<unit>[kKmM])(?:[bB])?$') {
    $num = [Convert]::ToInt64($Matches['num'], 10)
    $unit = $Matches['unit'].ToUpperInvariant()
    if ($unit -eq 'K') { return $num * 1KB }
    if ($unit -eq 'M') { return $num * 1MB }
  }

  throw "Formato numerico partizione non supportato: '$Value'"
}

function Convert-FlashSizeToBytes {
  param([Parameter(Mandatory = $true)] [string]$Value)
  return Convert-PartitionNumber -Value $Value
}

function Format-PartitionHex {
  param([Parameter(Mandatory = $true)] [int64]$Value)
  return ('0x{0:X}' -f $Value)
}

function Get-TargetPartitionLayout {
  return @(
    @{ Name = 'nvs';      Type = 'data'; SubType = 'nvs';      Offset = [int64]0x9000;   Size = [int64]0x5000 },
    @{ Name = 'otadata';  Type = 'data'; SubType = 'ota';      Offset = [int64]0xE000;   Size = [int64]0x2000 },
    @{ Name = 'factory';  Type = 'app';  SubType = 'factory';  Offset = [int64]0x10000;  Size = [int64]0x100000 },
    @{ Name = 'ota_0';    Type = 'app';  SubType = 'ota_0';    Offset = [int64]0x110000; Size = [int64]0x170000 },
    @{ Name = 'ota_1';    Type = 'app';  SubType = 'ota_1';    Offset = [int64]0x280000; Size = [int64]0x170000 },
    @{ Name = 'coredump'; Type = 'data'; SubType = 'coredump'; Offset = [int64]0x3F0000; Size = [int64]0x10000 }
  )
}

function Get-TargetPartitionMap {
  $map = @{}
  foreach ($entry in Get-TargetPartitionLayout) {
    $key = $entry.Name.ToLowerInvariant()
    $map[$key] = $entry
  }
  return $map
}

function Get-TargetSlotSize {
  param([Parameter(Mandatory = $true)] [string]$Name)

  $map = Get-TargetPartitionMap
  $key = $Name.ToLowerInvariant()
  if (-not $map.ContainsKey($key)) {
    throw "Slot '$Name' non definito nel layout target"
  }
  return [int64]$map[$key].Size
}

function Get-PartitionMapFromCsv {
  param([Parameter(Mandatory = $true)] [string]$CsvPath)

  if (-not (Test-Path $CsvPath)) {
    throw "CSV partizioni non trovato: $CsvPath"
  }

  $map = @{}
  foreach ($rawLine in Get-Content $CsvPath) {
    $line = $rawLine.Trim()
    if ([string]::IsNullOrWhiteSpace($line) -or $line.StartsWith('#')) { continue }

    $cols = @($line.Split(',') | ForEach-Object { $_.Trim() })
    if ($cols.Count -lt 5) { continue }
    if ([string]::IsNullOrWhiteSpace($cols[0])) { continue }

    $nameKey = $cols[0].ToLowerInvariant()
    $map[$nameKey] = @{
      Name = $cols[0]
      Type = $cols[1].ToLowerInvariant()
      SubType = $cols[2].ToLowerInvariant()
      Offset = Convert-PartitionNumber -Value $cols[3]
      Size = Convert-PartitionNumber -Value $cols[4]
      Flags = if ($cols.Count -ge 6) { $cols[5] } else { '' }
      RawLine = $line
    }
  }

  return $map
}

function Compare-PartitionMaps {
  param(
    [Parameter(Mandatory = $true)] [hashtable]$Expected,
    [Parameter(Mandatory = $true)] [hashtable]$Actual,
    [Parameter(Mandatory = $true)] [string]$ExpectedLabel,
    [Parameter(Mandatory = $true)] [string]$ActualLabel,
    [string[]]$Names = @()
  )

  $namesToCheck = if ($Names.Count -gt 0) {
    $Names
  } else {
    @($Expected.Keys | Sort-Object)
  }

  foreach ($name in $namesToCheck) {
    $key = $name.ToLowerInvariant()
    if (-not $Expected.ContainsKey($key)) {
      throw "${ExpectedLabel}: partizione '$name' non presente nel riferimento"
    }
    if (-not $Actual.ContainsKey($key)) {
      throw "${ActualLabel}: partizione '$name' mancante"
    }

    $e = $Expected[$key]
    $a = $Actual[$key]
    if ($a.Type -ne $e.Type -or $a.SubType -ne $e.SubType) {
      throw "${ActualLabel}: partizione '$name' type/subtype non coerente con $ExpectedLabel (atteso $($e.Type)/$($e.SubType), trovato $($a.Type)/$($a.SubType))"
    }
    if ($a.Offset -ne $e.Offset) {
      throw "${ActualLabel}: partizione '$name' offset non coerente con $ExpectedLabel (atteso $(Format-PartitionHex $e.Offset), trovato $(Format-PartitionHex $a.Offset))"
    }
    if ($a.Size -ne $e.Size) {
      throw "${ActualLabel}: partizione '$name' size non coerente con $ExpectedLabel (atteso $(Format-PartitionHex $e.Size), trovato $(Format-PartitionHex $a.Size))"
    }
  }
}

function Assert-RequiredAppLayout {
  param([Parameter(Mandatory = $true)] [hashtable]$PartitionMap)

  $required = @('factory', 'ota_0', 'ota_1')
  foreach ($name in $required) {
    if (-not $PartitionMap.ContainsKey($name)) {
      throw "Layout partizioni non valido: partizione '$name' mancante"
    }
    $p = $PartitionMap[$name]
    if ($p.Type -ne 'app') {
      throw "Layout partizioni non valido: '$name' deve avere type=app (trovato $($p.Type))"
    }
  }

  if ($PartitionMap['factory'].SubType -ne 'factory') {
    throw "Layout partizioni non valido: 'factory' deve avere subtype=factory (trovato $($PartitionMap['factory'].SubType))"
  }
  if ($PartitionMap['ota_0'].SubType -ne 'ota_0') {
    throw "Layout partizioni non valido: 'ota_0' deve avere subtype=ota_0 (trovato $($PartitionMap['ota_0'].SubType))"
  }
  if ($PartitionMap['ota_1'].SubType -ne 'ota_1') {
    throw "Layout partizioni non valido: 'ota_1' deve avere subtype=ota_1 (trovato $($PartitionMap['ota_1'].SubType))"
  }
}

function Assert-TargetPartitionLayout {
  param(
    [Parameter(Mandatory = $true)] [hashtable]$PartitionMap,
    [Parameter(Mandatory = $true)] [string]$SourceLabel,
    [int64]$FlashSizeBytes = [int64](4MB)
  )

  $target = Get-TargetPartitionMap
  Compare-PartitionMaps -Expected $target -Actual $PartitionMap -ExpectedLabel 'layout_target_obbligatorio' -ActualLabel $SourceLabel

  $extra = @($PartitionMap.Keys | Where-Object { -not $target.ContainsKey($_.ToLowerInvariant()) })
  if ($extra.Count -gt 0) {
    throw "${SourceLabel}: contiene partizioni non previste dal layout target: $($extra -join ', ')"
  }

  foreach ($entry in Get-TargetPartitionLayout) {
    $end = [int64]$entry.Offset + [int64]$entry.Size
    if ($end -gt $FlashSizeBytes) {
      throw "${SourceLabel}: partizione '$($entry.Name)' eccede flash size $(Format-PartitionHex $FlashSizeBytes)"
    }
  }

  $factory = $target['factory']
  $ota0 = $target['ota_0']
  $ota1 = $target['ota_1']
  $coredump = $target['coredump']

  if (($factory.Offset + $factory.Size) -ne $ota0.Offset) {
    throw "${SourceLabel}: factory non contigua a ota_0"
  }
  if (($ota0.Offset + $ota0.Size) -ne $ota1.Offset) {
    throw "${SourceLabel}: ota_0 non contigua a ota_1"
  }
  if (($ota1.Offset + $ota1.Size) -ne $coredump.Offset) {
    throw "${SourceLabel}: ota_1 non contigua a coredump"
  }
}

function Decode-PartitionsBin {
  param(
    [Parameter(Mandatory = $true)] [string]$GenEsp32PartPath,
    [Parameter(Mandatory = $true)] [string]$PartitionsBinPath,
    [Parameter(Mandatory = $true)] [string]$OutputCsvPath
  )

  if (-not (Test-Path $GenEsp32PartPath)) {
    throw "gen_esp32part non trovato: $GenEsp32PartPath"
  }
  if (-not (Test-Path $PartitionsBinPath)) {
    throw "partitions.bin non trovato: $PartitionsBinPath"
  }

  if (Test-Path $OutputCsvPath) { Remove-Item $OutputCsvPath -Force }
  & $GenEsp32PartPath $PartitionsBinPath $OutputCsvPath | Out-Null
  if ($LASTEXITCODE -ne 0 -or -not (Test-Path $OutputCsvPath)) {
    throw "Impossibile decodificare partitions bin: $PartitionsBinPath"
  }

  return Get-PartitionMapFromCsv -CsvPath $OutputCsvPath
}

function Assert-ImageFitsSlot {
  param(
    [Parameter(Mandatory = $true)] [string]$ImagePath,
    [Parameter(Mandatory = $true)] [int64]$SlotSizeBytes,
    [Parameter(Mandatory = $true)] [string]$SlotLabel
  )

  if (-not (Test-Path $ImagePath)) {
    throw "Immagine non trovata: $ImagePath"
  }

  $size = (Get-Item $ImagePath).Length
  if ($size -gt $SlotSizeBytes) {
    throw "Immagine troppo grande per ${SlotLabel}: $size B > $SlotSizeBytes B"
  }

  return $size
}

function Assert-FilesExist {
  param([Parameter(Mandatory = $true)] [string[]]$Paths)

  foreach ($path in $Paths) {
    if (-not (Test-Path $path)) {
      throw "Artefatto mancante: $path"
    }
  }
}

function Get-ArduinoCliPath {
  param([string]$ArduinoCliPath)

  if (-not [string]::IsNullOrWhiteSpace($ArduinoCliPath)) {
    if (-not (Test-Path $ArduinoCliPath)) {
      throw "arduino-cli non trovato: $ArduinoCliPath"
    }
    return $ArduinoCliPath
  }

  $defaultArduinoCli = 'C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe'
  if (-not (Test-Path $defaultArduinoCli)) {
    throw "arduino-cli non trovato: $defaultArduinoCli"
  }

  return $defaultArduinoCli
}

function Get-LatestDirectoryByName {
  param([Parameter(Mandatory = $true)] [string]$Path)

  if (-not (Test-Path $Path)) {
    throw "Directory non trovata: $Path"
  }

  $dir = Get-ChildItem $Path -Directory | Sort-Object Name -Descending | Select-Object -First 1
  if ($null -eq $dir) {
    throw "Nessuna sottodirectory trovata in: $Path"
  }

  return $dir
}

function Get-CoreContext {
  param(
    [Parameter(Mandatory = $true)] [string]$Fqbn,
    [string]$CoreVersion = ''
  )

  $parts = $Fqbn.Split(':')
  if ($parts.Count -lt 3) {
    throw "FQBN non valido: $Fqbn"
  }

  $vendor = $parts[0]
  $arch = $parts[1]
  $boardId = $parts[2]

  if ($vendor -ne 'esp32' -or $arch -ne 'esp32') {
    throw "FQBN non supportato per questa pipeline: $Fqbn"
  }

  $arduino15 = Join-Path $env:LOCALAPPDATA 'Arduino15'
  $coreBase = Join-Path $arduino15 'packages\esp32\hardware\esp32'
  if (-not (Test-Path $coreBase)) {
    throw "Core ESP32 non trovato: $coreBase"
  }

  $coreDir = if ([string]::IsNullOrWhiteSpace($CoreVersion)) {
    Get-LatestDirectoryByName -Path $coreBase
  } else {
    $p = Join-Path $coreBase $CoreVersion
    if (-not (Test-Path $p)) {
      throw "Versione core ESP32 non trovata: $p"
    }
    Get-Item $p
  }

  $boardsTxt = Join-Path $coreDir.FullName 'boards.txt'
  $platformTxt = Join-Path $coreDir.FullName 'platform.txt'
  if (-not (Test-Path $boardsTxt)) { throw "boards.txt non trovato: $boardsTxt" }
  if (-not (Test-Path $platformTxt)) { throw "platform.txt non trovato: $platformTxt" }

  $boardProps = @{}
  foreach ($line in Get-Content $boardsTxt) {
    if ($line -match "^$([regex]::Escape($boardId))\.(?<key>[^=]+)=(?<val>.*)$") {
      $boardProps[$Matches['key']] = $Matches['val']
    }
  }
  if ($boardProps.Count -eq 0) {
    throw "Board '$boardId' non trovata in $boardsTxt"
  }

  $platformLines = Get-Content $platformTxt
  $uploadPatternLine = $platformLines | Where-Object { $_ -match '^tools\.esptool_py\.upload\.pattern_args=' } | Select-Object -First 1
  if ([string]::IsNullOrWhiteSpace($uploadPatternLine)) {
    throw "tools.esptool_py.upload.pattern_args non trovato in $platformTxt"
  }

  $bootloaderAddr = if ($boardProps.ContainsKey('build.bootloader_addr')) { $boardProps['build.bootloader_addr'] } else { '0x1000' }
  $partitionsAddr = '0x8000'
  $bootApp0Addr = '0xE000'

  if ($uploadPatternLine -match '(0x[0-9a-fA-F]+)\s+"\{build\.path\}/\{build\.project_name\}\.partitions\.bin"') {
    $partitionsAddr = $Matches[1]
  }
  if ($uploadPatternLine -match '(0x[0-9a-fA-F]+)\s+"\{runtime\.platform\.path\}/tools/partitions/boot_app0\.bin"') {
    $bootApp0Addr = $Matches[1]
  }

  $toolsPartitions = Join-Path $coreDir.FullName 'tools\partitions'
  $bootApp0Bin = Join-Path $toolsPartitions 'boot_app0.bin'
  $genEsp32Part = Join-Path $coreDir.FullName 'tools\gen_esp32part.exe'

  $esptoolBase = Join-Path $arduino15 'packages\esp32\tools\esptool_py'
  $esptoolDir = Get-LatestDirectoryByName -Path $esptoolBase
  $esptoolExe = Join-Path $esptoolDir.FullName 'esptool.exe'

  if (-not (Test-Path $bootApp0Bin)) { throw "boot_app0.bin non trovato: $bootApp0Bin" }
  if (-not (Test-Path $genEsp32Part)) { throw "gen_esp32part.exe non trovato: $genEsp32Part" }
  if (-not (Test-Path $esptoolExe)) { throw "esptool.exe non trovato: $esptoolExe" }

  $uploadMaximumSize = if ($boardProps.ContainsKey('upload.maximum_size')) { [int64]$boardProps['upload.maximum_size'] } else { -1 }
  $flashSize = if ($boardProps.ContainsKey('build.flash_size')) { $boardProps['build.flash_size'] } else { '4MB' }
  $flashMode = if ($boardProps.ContainsKey('build.flash_mode')) { $boardProps['build.flash_mode'] } else { 'qio' }
  $flashFreq = if ($boardProps.ContainsKey('build.flash_freq')) { $boardProps['build.flash_freq'] } else { '80m' }

  return @{
    Fqbn = $Fqbn
    BoardId = $boardId
    CoreVersion = $coreDir.Name
    CoreRoot = $coreDir.FullName
    BoardsTxt = $boardsTxt
    PlatformTxt = $platformTxt
    BoardProperties = $boardProps
    BootloaderAddr = $bootloaderAddr
    PartitionsAddr = $partitionsAddr
    BootApp0Addr = $bootApp0Addr
    UploadMaximumSize = $uploadMaximumSize
    FlashSize = $flashSize
    FlashSizeBytes = Convert-FlashSizeToBytes -Value $flashSize
    FlashMode = $flashMode
    FlashFreq = $flashFreq
    BootApp0Bin = $bootApp0Bin
    GenEsp32PartPath = $genEsp32Part
    EsptoolPath = $esptoolExe
  }
}

function Get-PartitionEntry {
  param(
    [Parameter(Mandatory = $true)] [hashtable]$PartitionMap,
    [Parameter(Mandatory = $true)] [string]$Name
  )

  $key = $Name.ToLowerInvariant()
  if (-not $PartitionMap.ContainsKey($key)) {
    throw "Partizione '$Name' non trovata"
  }
  return $PartitionMap[$key]
}



