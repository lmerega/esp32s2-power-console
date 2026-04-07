param(
  [switch]$BumpVersion,
  [switch]$Verbose,
  [string]$Flash = '',
  [int]$FlashPort = 80,
  [string]$ArduinoCliPath = $env:ARDUINO_CLI_PATH,
  [string]$CoreVersion = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $scriptDir 'esp32_layout_common.ps1')

$inoFile = Join-Path $scriptDir 'ESP32S2_WiFiManager_WebServer.ino'
$fqbn = 'esp32:esp32:lolin_s2_mini'
$libDir = 'C:\Arduino\libraries'
$buildDir = Join-Path $scriptDir 'build\esp32.esp32.lolin_s2_mini'
$partitionSrc = Join-Path $scriptDir 'partitions_factory.csv'
$sketchPartitionsCsv = Join-Path $scriptDir 'partitions_factory.csv'
$legacySketchPartitionsCsv = Join-Path $scriptDir 'partitions.csv'
$mainBinPath = Join-Path $scriptDir 'ESP32S2_WiFiManager_WebServer.ino.bin'
$buildPartitionsBinPath = Join-Path $buildDir 'ESP32S2_WiFiManager_WebServer.ino.partitions.bin'
$buildBootloaderPath = Join-Path $buildDir 'ESP32S2_WiFiManager_WebServer.ino.bootloader.bin'
$buildLogPath = Join-Path $scriptDir 'build_last_compile.log'

$arduinoCli = Get-ArduinoCliPath -ArduinoCliPath $ArduinoCliPath
Assert-FilesExist -Paths @($inoFile, $partitionSrc)

# Some Arduino CLI / ESP32 core setups look for a sketch-local `partitions.csv`
# before honoring `build.partitions`. Keep a compatibility copy in sync so the
# build does not depend on the core's fallback files being present.
Copy-Item -Path $partitionSrc -Destination $legacySketchPartitionsCsv -Force

$core = Get-CoreContext -Fqbn $fqbn -CoreVersion $CoreVersion
$sourcePartitions = Get-PartitionMapFromCsv -CsvPath $partitionSrc
Assert-RequiredAppLayout -PartitionMap $sourcePartitions
Assert-TargetPartitionLayout -PartitionMap $sourcePartitions -SourceLabel 'partitions_factory.csv' -FlashSizeBytes $core.FlashSizeBytes
$ota0 = Get-PartitionEntry -PartitionMap $sourcePartitions -Name 'ota_0'
$mainSlotMaxBytes = [int64]$ota0.Size

if ($core.FlashSizeBytes -ne 4MB) {
  throw "Flash size board/core non coerente con target 4MB: $($core.FlashSize)"
}

Write-Host '============================================================'
Write-Host '  BUILD BIN - ESP32 S2 MAIN OTA'
Write-Host '============================================================'
Write-Host "FQBN:                $fqbn"
Write-Host "Core ESP32:          $($core.CoreVersion)"
Write-Host "Bootloader offset:   $($core.BootloaderAddr)"
Write-Host "Partitions offset:   $($core.PartitionsAddr)"
Write-Host "boot_app0 offset:    $($core.BootApp0Addr)"
Write-Host "Flash size:          $($core.FlashSize)"
Write-Host "Main slot (ota_0):   $mainSlotMaxBytes B"
Write-Host ''

$content = Get-Content -Raw $inoFile
$originalContent = $content
$versionBumped = $false
$match = [regex]::Match($content, '#define FW_VERSION\s+"(\d+)\.(\d+)\.(\d+)"')
if (-not $match.Success) {
  throw 'FW_VERSION non trovata'
}

$currentVersion = '{0}.{1}.{2}' -f $match.Groups[1].Value, $match.Groups[2].Value, $match.Groups[3].Value
$effectiveVersion = $currentVersion

if ($BumpVersion) {
  $effectiveVersion = '{0}.{1}.{2}' -f $match.Groups[1].Value, $match.Groups[2].Value, ([int]$match.Groups[3].Value + 1)
  $newContent = [regex]::Replace(
    $content,
    '#define FW_VERSION\s+"(\d+)\.(\d+)\.(\d+)"',
    ('#define FW_VERSION "' + $effectiveVersion + '"'),
    1
  )
  Set-Content -Path $inoFile -Value $newContent -Encoding Ascii
  $versionBumped = $true
  Write-Host "Versione firmware: $currentVersion -> $effectiveVersion"
} else {
  Write-Host "Versione firmware: $effectiveVersion"
}

Get-ChildItem -Path $scriptDir -Filter 'ESP32S2_WiFiManager_WebServer.ino*.bin' -File -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue
if (Test-Path $buildDir) {
  Remove-Item -Path $buildDir -Recurse -Force
}

$compileArgs = @('compile', '--fqbn', $fqbn, '--libraries', $libDir, '--export-binaries')
$compileArgs += @('--build-property', 'build.partitions=partitions_factory')
$compileArgs += @('--build-property', "upload.maximum_size=$mainSlotMaxBytes")
if ($Verbose) { $compileArgs += '--verbose' }
$compileArgs += $scriptDir

if (Test-Path $buildLogPath) {
  Remove-Item -Path $buildLogPath -Force -ErrorAction SilentlyContinue
}

$stdoutLogPath = Join-Path $scriptDir 'build_last_compile_stdout.log'
$stderrLogPath = Join-Path $scriptDir 'build_last_compile_stderr.log'
foreach ($logPath in @($stdoutLogPath, $stderrLogPath)) {
  if (Test-Path $logPath) {
    Remove-Item -Path $logPath -Force -ErrorAction SilentlyContinue
  }
}

$compileArgLine = ($compileArgs | ForEach-Object {
  if ($_ -match '\s') { '"' + ($_ -replace '"', '\"') + '"' } else { $_ }
}) -join ' '

$compileProcess = Start-Process -FilePath $arduinoCli `
  -ArgumentList $compileArgLine `
  -WorkingDirectory $scriptDir `
  -NoNewWindow `
  -Wait `
  -PassThru `
  -RedirectStandardOutput $stdoutLogPath `
  -RedirectStandardError $stderrLogPath

$compileExitCode = $compileProcess.ExitCode
$compileStdout = if (Test-Path $stdoutLogPath) { Get-Content -Path $stdoutLogPath -ErrorAction SilentlyContinue } else { @() }
$compileStderr = if (Test-Path $stderrLogPath) { Get-Content -Path $stderrLogPath -ErrorAction SilentlyContinue } else { @() }
$compileOutput = @($compileStdout + $compileStderr)
$compileOutput | Out-File -FilePath $buildLogPath -Encoding utf8
$compileOutput | ForEach-Object { Write-Host $_ }

if ($compileExitCode -ne 0) {
  if ($versionBumped) {
    Set-Content -Path $inoFile -Value $originalContent -Encoding Ascii
    Write-Warning "Build fallita: FW_VERSION ripristinata a $currentVersion"
  }
  Write-Warning "Log completo disponibile in: $buildLogPath"
  Write-Warning "STDOUT: $stdoutLogPath"
  Write-Warning "STDERR: $stderrLogPath"
  Write-Warning "Per piu dettagli puoi rilanciare: .\build_bin.bat -Verbose"
  throw "Compilazione fallita con exit code $compileExitCode"
}

if (Test-Path $buildDir) {
  Get-ChildItem -Path $buildDir -Filter *.bin -File | Copy-Item -Destination $scriptDir -Force
}

Copy-Item -Path $core.BootApp0Bin -Destination (Join-Path $scriptDir 'boot_app0.bin') -Force
if (Test-Path $buildDir) {
  Copy-Item -Path $core.BootApp0Bin -Destination (Join-Path $buildDir 'boot_app0.bin') -Force
}

Assert-FilesExist -Paths @($mainBinPath, $buildPartitionsBinPath, $buildBootloaderPath)

$decodedPartitionsPath = Join-Path $buildDir '_decoded_partitions.csv'
$decodedMap = Decode-PartitionsBin -GenEsp32PartPath $core.GenEsp32PartPath -PartitionsBinPath $buildPartitionsBinPath -OutputCsvPath $decodedPartitionsPath
Assert-TargetPartitionLayout -PartitionMap $decodedMap -SourceLabel 'partitions.bin (main build)' -FlashSizeBytes $core.FlashSizeBytes
Compare-PartitionMaps -Expected $sourcePartitions -Actual $decodedMap -ExpectedLabel 'partitions_factory.csv' -ActualLabel 'partitions.bin (main build)'

$mainBinSize = Assert-ImageFitsSlot -ImagePath $mainBinPath -SlotSizeBytes $mainSlotMaxBytes -SlotLabel 'ota_0'

$bins = Get-ChildItem -Path $scriptDir -Filter *.bin -File | Sort-Object Name
Write-Host ''
Write-Host '============================================================'
Write-Host '  COMPILAZIONE MAIN COMPLETATA'
Write-Host '============================================================'
Write-Host "Main slot max: $mainSlotMaxBytes B"
Write-Host "Main bin size: $mainBinSize B"
Write-Host "Partizioni:     $buildPartitionsBinPath"
Write-Host "Bootloader:     $buildBootloaderPath"
Write-Host "boot_app0:      $(Join-Path $scriptDir 'boot_app0.bin')"
Write-Host 'BIN disponibili nella cartella sketch:'
if ($bins.Count -eq 0) {
  Write-Host 'Nessun BIN trovato'
} else {
  foreach ($bin in $bins) {
    $sizeKb = [math]::Round($bin.Length / 1KB, 1)
    Write-Host ("- {0}  [{1} KB]  {2}" -f $bin.Name, $sizeKb, $bin.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss'))
  }
}
Write-Host '============================================================'

if (-not [string]::IsNullOrWhiteSpace($Flash)) {
  $flashScript = Join-Path $scriptDir 'flash.ps1'
  if (-not (Test-Path $flashScript)) {
    throw "flash.ps1 non trovato: $flashScript"
  }
  & $flashScript -DeviceIp $Flash -HttpPort $FlashPort
}
