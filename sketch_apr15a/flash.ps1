# 一键编译并烧录 sketch_apr15a（ESP32-C3 + min_spiffs）
# 用法: .\flash.ps1
#       .\flash.ps1 -Port COM6
param(
  [string]$Port = ""
)

$ErrorActionPreference = "Stop"
$SketchDir = $PSScriptRoot
# 单引号赋值；传参用 "--fqbn=$Fqbn" 避免逗号被 PowerShell 拆参
$Fqbn = 'esp32:esp32:esp32c3:CDCOnBoot=cdc,CPUFreq=160,FlashFreq=80,FlashMode=dio,FlashSize=4M,JTAGAdapter=default,PartitionScheme=huge_app,UploadSpeed=256000,EraseFlash=none,ZigbeeMode=default,DebugLevel=none'

if (-not $Port) {
  $json = arduino-cli board list --format json 2>&1 | Out-String
  $obj = $json | ConvertFrom-Json
  foreach ($dp in $obj.detected_ports) {
    if ($dp.matching_boards -and $dp.port.address -match "^COM\d+") {
      $Port = $dp.port.address
      break
    }
  }
  if (-not $Port) { $Port = "COM5" }
}

Write-Host "compile + upload -> $Port" -ForegroundColor Cyan
& arduino-cli compile "--fqbn=$Fqbn" "$SketchDir"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& arduino-cli upload "$SketchDir" -p $Port "--fqbn=$Fqbn"
exit $LASTEXITCODE
