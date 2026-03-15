# Check if the A7670E target server is reachable (same host:port as a7670e_config.h).
# Run from project root: .\scripts\check_server.ps1
# Or with custom host/port: .\scripts\check_server.ps1 -HostName "178.62.86.70" -Port 3000

param(
    [string]$HostName = "gates.crea-cell.com",
    [int]$Port = 3000
)

$ErrorActionPreference = "Stop"
Write-Host "Checking if server is ready: ${HostName}:${Port} ..."

try {
    $tcp = New-Object System.Net.Sockets.TcpClient
    $task = $tcp.ConnectAsync($HostName, $Port)
    $task.Wait(5000) | Out-Null
    if ($tcp.Connected) {
        $tcp.Close()
        Write-Host "OK - Server is reachable (${HostName}:${Port})." -ForegroundColor Green
        exit 0
    }
    $tcp.Close()
} catch {
    Write-Host "FAIL - Cannot connect: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
Write-Host "FAIL - Timeout (5s). Is the server running and port ${Port} open?" -ForegroundColor Red
exit 1
