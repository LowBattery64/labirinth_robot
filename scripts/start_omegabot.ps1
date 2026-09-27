param(
    [switch]$Build
)

$ErrorActionPreference = "Stop"
$RobotHost = "10.109.150.232"
$CameraUrl = "http://10.109.150.34/"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$OperatorExe = Join-Path $ProjectRoot "operator_ui\build\Release\OmegaBotOperator.exe"

function Test-TcpPort {
    param([string]$HostName, [int]$Port)
    try {
        $result = Test-NetConnection -ComputerName $HostName -Port $Port -WarningAction SilentlyContinue
        return $result.TcpTestSucceeded
    }
    catch { return $false }
}

function Wait-TcpPort {
    param([string]$HostName, [int]$Port, [int]$TimeoutSeconds = 20)
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        if (Test-TcpPort -HostName $HostName -Port $Port) { return }
        Start-Sleep -Milliseconds 500
    }
    throw "Порт $Port на $HostName не стал доступен за $TimeoutSeconds секунд."
}

Write-Host "========================================"
Write-Host "       OMEGABOT STARTUP"
Write-Host "========================================"
Write-Host ""

Write-Host "[1/6] Проверка SSH к Raspberry Pi..."
ssh -o BatchMode=yes -o ConnectTimeout=5 "raspberry@$RobotHost" "echo SSH_OK"
if ($LASTEXITCODE -ne 0) { throw "Не удалось подключиться к Raspberry Pi по SSH. Проверь SSH-ключ и сеть." }

Write-Host "[2/6] Запуск серверов на Raspberry Pi..."
ssh -o BatchMode=yes "raspberry@$RobotHost" "cd ~/labirinth_robot && bash scripts/start_robot.sh"
if ($LASTEXITCODE -ne 0) { throw "Скрипт запуска Raspberry Pi завершился с ошибкой." }

Write-Host "[3/6] Ожидание TCP 5000 и 5001..."
Wait-TcpPort -HostName $RobotHost -Port 5000
Wait-TcpPort -HostName $RobotHost -Port 5001

Write-Host "[4/6] Активация видеопотока камеры..."
Start-Process $CameraUrl | Out-Null
Start-Sleep -Seconds 2
if (-not (Test-TcpPort -HostName $RobotHost -Port 5001)) { throw "Видеосервер Raspberry Pi недоступен." }

Write-Host "[5/6] Проверка Operator UI..."
if ($Build -or -not (Test-Path $OperatorExe)) {
    Write-Host "       Сборка Operator UI..."
    cmake -S (Join-Path $ProjectRoot "operator_ui") -B (Join-Path $ProjectRoot "operator_ui\build") -G "Visual Studio 17 2022" -A x64
    if ($LASTEXITCODE -ne 0) { throw "CMake configuration завершилась с ошибкой." }
    cmake --build (Join-Path $ProjectRoot "operator_ui\build") --config Release
    if ($LASTEXITCODE -ne 0) { throw "Сборка Operator UI завершилась с ошибкой." }
}

if (-not (Test-Path $OperatorExe)) { throw "Исполняемый файл Operator UI не найден: $OperatorExe" }

Write-Host "[6/6] Запуск Operator UI..."
Start-Process $OperatorExe

Write-Host ""
Write-Host "========================================"
Write-Host "       OMEGABOT READY"
Write-Host "========================================"
Write-Host ""
Write-Host "Robot:   $RobotHost"
Write-Host "Camera:  10.109.150.34"
Write-Host "Control: TCP 5000"
Write-Host "Video:   TCP 5001"
Write-Host ""
