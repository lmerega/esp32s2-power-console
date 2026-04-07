param(
  [string]$ArduinoCliPath = $env:ARDUINO_CLI_PATH,
  [string]$CoreVersion = '',
  [switch]$Verbose
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$baseDir = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $baseDir 'esp32_layout_common.ps1')

$sketchDir = Join-Path $baseDir 'ESP32S2_Recovery_OTA'
$inoFile = Join-Path $sketchDir 'ESP32S2_Recovery_OTA.ino'
$fqbn = 'esp32:esp32:lolin_s2_mini'
$libDir = 'C:\Arduino\libraries'
$buildDir = Join-Path $sketchDir 'build\esp32.esp32.lolin_s2_mini'
$partitionSrc = Join-Path $baseDir 'partitions_factory.csv'
$partitionSketchCsv = Join-Path $sketchDir 'partitions_factory.csv'
$legacyPartitionSketchCsv = Join-Path $sketchDir 'partitions.csv'
$factorySlotMaxBytes = Get-TargetSlotSize -Name 'factory'

$arduinoCli = Get-ArduinoCliPath -ArduinoCliPath $ArduinoCliPath
Assert-FilesExist -Paths @($inoFile, $partitionSrc)

$core = Get-CoreContext -Fqbn $fqbn -CoreVersion $CoreVersion
if ($core.FlashSizeBytes -ne 4MB) {
  throw "Flash size board/core non coerente con target 4MB: $($core.FlashSize)"
}

$sourceMap = Get-PartitionMapFromCsv -CsvPath $partitionSrc
Assert-RequiredAppLayout -PartitionMap $sourceMap
Assert-TargetPartitionLayout -PartitionMap $sourceMap -SourceLabel 'partitions_factory.csv' -FlashSizeBytes $core.FlashSizeBytes

Copy-Item -Path $partitionSrc -Destination $partitionSketchCsv -Force
Copy-Item -Path $partitionSrc -Destination $legacyPartitionSketchCsv -Force

Write-Host '============================================================'
Write-Host '  BUILD BIN - ESP32 RECOVERY (FACTORY)'
Write-Host '============================================================'
Write-Host "Toolchain:          $arduinoCli"
Write-Host "Sketch:             $sketchDir"
Write-Host "Board:              $fqbn"
Write-Host "Core ESP32:         $($core.CoreVersion)"
Write-Host "Factory slot max:   $factorySlotMaxBytes B"
Write-Host "Bootloader offset:  $($core.BootloaderAddr)"
Write-Host "Partitions offset:  $($core.PartitionsAddr)"
Write-Host "boot_app0 offset:   $($core.BootApp0Addr)"

Get-ChildItem -Path $sketchDir -Filter '*.bin' -File -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue
if (Test-Path $buildDir) { Remove-Item -Path $buildDir -Recurse -Force }

$compileArgs = @('compile', '--fqbn', $fqbn, '--libraries', $libDir, '--export-binaries')
$compileArgs += @('--build-property', 'build.partitions=partitions_factory')
$compileArgs += @('--build-property', "upload.maximum_size=$factorySlotMaxBytes")
if ($Verbose) { $compileArgs += '--verbose' }
$compileArgs += $sketchDir

& $arduinoCli @compileArgs
if ($LASTEXITCODE -ne 0) { throw "Compilazione recovery fallita con exit code $LASTEXITCODE" }

if (Test-Path $buildDir) {
  Get-ChildItem -Path $buildDir -Filter *.bin -File | Copy-Item -Destination $sketchDir -Force
}

$recoveryBinPath = Join-Path $sketchDir 'ESP32S2_Recovery_OTA.ino.bin'
$partitionsBinPath = Join-Path $buildDir 'ESP32S2_Recovery_OTA.ino.partitions.bin'
Assert-FilesExist -Paths @($recoveryBinPath, $partitionsBinPath)

$decodedPartitionsPath = Join-Path $buildDir '_decoded_partitions.csv'
$decodedMap = Decode-PartitionsBin -GenEsp32PartPath $core.GenEsp32PartPath -PartitionsBinPath $partitionsBinPath -OutputCsvPath $decodedPartitionsPath
Assert-TargetPartitionLayout -PartitionMap $decodedMap -SourceLabel 'partitions.bin (recovery build)' -FlashSizeBytes $core.FlashSizeBytes
Compare-PartitionMaps -Expected $sourceMap -Actual $decodedMap -ExpectedLabel 'partitions_factory.csv' -ActualLabel 'partitions.bin (recovery build)'

$recoverySize = Assert-ImageFitsSlot -ImagePath $recoveryBinPath -SlotSizeBytes $factorySlotMaxBytes -SlotLabel 'factory'

$bins = Get-ChildItem -Path $sketchDir -Filter *.bin -File | Sort-Object Name
Write-Host ''
Write-Host 'BIN recovery:'
foreach ($bin in $bins) {
  $sizeKb = [math]::Round($bin.Length / 1KB, 1)
  Write-Host ("- {0}  [{1} KB]  {2}" -f $bin.Name, $sizeKb, $bin.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss'))
}
Write-Host "Recovery bin size: $recoverySize B"
Write-Host '============================================================'
