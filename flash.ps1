param(
  [Parameter(Mandatory=$true)]
  [string]$DeviceIp,
  [int]$HttpPort = 80,
  [string]$BinPath = ""
)

$ErrorActionPreference = 'Stop'

$sketchDir = Split-Path -Parent $MyInvocation.MyCommand.Path

# --- Locate app binary ---
if ([string]::IsNullOrWhiteSpace($BinPath)) {
  $BinPath = Join-Path $sketchDir 'ESP32S2_WiFiManager_WebServer.ino.bin'
}
if (-not (Test-Path $BinPath)) {
  throw "App binary non trovato: $BinPath`nEsegui prima build_bin.bat per compilare il firmware."
}

# --- Read firmware version ---
$inoFile = Join-Path $sketchDir 'ESP32S2_WiFiManager_WebServer.ino'
$fwVersion = 'unknown'
if (Test-Path $inoFile) {
  $m = [regex]::Match((Get-Content -Raw $inoFile), '#define FW_VERSION\s+"([^"]+)"')
  if ($m.Success) { $fwVersion = $m.Groups[1].Value }
}

$baseUrl = "http://${DeviceIp}:${HttpPort}"
$binItem = Get-Item $BinPath
$binSizeKb = [math]::Round($binItem.Length / 1KB, 1)

Write-Host '============================================================'
Write-Host '  FLASH OTA - ESP32 S2 MINI'
Write-Host '============================================================'
Write-Host "  Device  : $baseUrl"
Write-Host "  Firmware: v$fwVersion"
Write-Host "  Binary  : $($binItem.Name)  [$binSizeKb KB]"
Write-Host '============================================================'
Write-Host ''

# --- Check device reachability ---
Write-Host 'Verifica raggiungibilita'' dispositivo...'
try {
  $ping = Invoke-WebRequest -Uri "$baseUrl/" -TimeoutSec 5 -UseBasicParsing -ErrorAction Stop | Out-Null
} catch {
  Write-Warning "Il dispositivo non risponde su $baseUrl"
  Write-Warning "Assicurati che sia connesso alla rete e che l'IP sia corretto."
  throw $_
}
Write-Host "  OK - dispositivo raggiungibile`n"

# --- Upload via curl ---
Write-Host 'Avvio upload OTA...'
Write-Host "(Potrebbe richiedere 30-60 secondi)`n"

$fileSize = $binItem.Length

# curl.exe is built into Windows 10/11
$curlArgs = @(
  '--silent', '--show-error',
  '-X', 'POST',
  '-H', "X-File-Size: $fileSize",
  '-F', "file=@`"$BinPath`";type=application/octet-stream",
  '--max-time', '120',
  "$baseUrl/api/update"
)

$curlOutput = & curl.exe @curlArgs 2>&1
$curlExit = $LASTEXITCODE

Write-Host "Risposta dispositivo: $curlOutput"

if ($curlExit -ne 0) {
  throw "curl fallito con exit code $curlExit"
}

# --- Poll progress until done ---
Write-Host ''
Write-Host 'Attendo conferma completamento...'
$deadline = (Get-Date).AddSeconds(30)
$finalStatus = $null
while ((Get-Date) -lt $deadline) {
  Start-Sleep -Milliseconds 1500
  try {
    $resp = Invoke-WebRequest -Uri "$baseUrl/api/update/progress" -TimeoutSec 5 -UseBasicParsing -ErrorAction Stop
    $json = $resp.Content | ConvertFrom-Json
    $pct = if ($json.pct -ne $null) { $json.pct } else { '?' }
    $status = $json.status
    Write-Host "  Stato: $status  ($pct%)"
    if ($json.finished -eq $true) {
      $finalStatus = $json
      break
    }
  } catch {
    Write-Warning "  Polling non riuscito: $_"
    break
  }
}

Write-Host ''
if ($finalStatus -ne $null -and $finalStatus.success -eq $true) {
  Write-Host '============================================================'
  Write-Host "  FLASH OTA COMPLETATO - v$fwVersion"
  Write-Host '  Il dispositivo si sta riavviando...'
  Write-Host '============================================================'
} elseif ($finalStatus -ne $null) {
  Write-Host "============================================================"
  Write-Host "  FLASH FALLITO - stato: $($finalStatus.status)"
  Write-Host "============================================================"
  exit 1
} else {
  Write-Host '  (Nessuna risposta finale - controlla il dispositivo manualmente)'
}
