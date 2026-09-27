$ErrorActionPreference = "Stop"
$RobotHost = "10.109.150.232"
$OperatorExeName = "OmegaBotOperator"

Write-Host "Остановка OmegaBot..."
Get-Process -Name $OperatorExeName -ErrorAction SilentlyContinue | Stop-Process -Force
ssh -o BatchMode=yes -o ConnectTimeout=5 "raspberry@$RobotHost" "cd ~/labirinth_robot && bash scripts/stop_robot.sh"
if ($LASTEXITCODE -ne 0) { throw "Не удалось остановить процессы на Raspberry Pi." }
Write-Host "OmegaBot остановлен."
