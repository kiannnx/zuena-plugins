<#
.SYNOPSIS
  Ejecuta los tests nativos de ZUENA EQ y ZUENA Luma (motor, auditoría de interfaz a 100 % y 70 %,
  rendimiento, capturas y carga VST3) en Windows Sandbox. Capturas y registros: build\sandbox-results.
.DESCRIPTION
  El PC de desarrollo mantiene Smart App Control activado y los binarios no se firman (decisión del
  usuario, 2026-11-26). La Sandbox es una Windows descartable donde esos binarios sí se ejecutan.
  Comparte build\release en solo lectura y recoge los resultados en build\sandbox-results.
  Requisito único: la característica "Windows Sandbox" activada (ver Obsidian-Vault, Firma-de-Codigo).
.EXAMPLE
  powershell -ExecutionPolicy Bypass -File tools\sandbox-tests.ps1
#>
# -BuildDir build\sandbox-release: probar los binarios compilados en Sandbox (tools\sandbox-build.ps1).
param([int] $TimeoutMinutes = 15, [string] $BuildDir = 'build\release')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root $BuildDir
$results = Join-Path $root 'build\sandbox-results'
$sandboxExe = Join-Path $env:windir 'System32\WindowsSandbox.exe'

if (-not (Test-Path -LiteralPath $sandboxExe)) {
    throw 'Windows Sandbox no está activado. En PowerShell como administrador: Enable-WindowsOptionalFeature -Online -FeatureName Containers-DisposableClientVM -All  (y reiniciar).'
}
if (-not (Test-Path -LiteralPath (Join-Path $build 'plugins\ZuenaEQ\run_tests.exe'))) {
    throw 'No hay binarios compilados. Ejecutá tools\build.cmd primero.'
}
if (Get-Process -Name 'WindowsSandbox*' -ErrorAction SilentlyContinue) { throw 'Ya hay una Windows Sandbox abierta; cerrala primero.' }

Remove-Item -LiteralPath $results -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $results -Force | Out-Null
$esc = { param($s) [System.Security.SecurityElement]::Escape($s) }
$wsb = Join-Path $env:TEMP 'zuena-tests.wsb'
@"
<Configuration>
  <Networking>Disable</Networking>
  <ClipboardRedirection>Disable</ClipboardRedirection>
  <MappedFolders>
    <MappedFolder><HostFolder>$(& $esc $build)</HostFolder><SandboxFolder>C:\zuena\build</SandboxFolder><ReadOnly>true</ReadOnly></MappedFolder>
    <MappedFolder><HostFolder>$(& $esc (Join-Path $PSScriptRoot 'sandbox'))</HostFolder><SandboxFolder>C:\zuena\sandbox</SandboxFolder><ReadOnly>true</ReadOnly></MappedFolder>
    <MappedFolder><HostFolder>$(& $esc $results)</HostFolder><SandboxFolder>C:\zuena\results</SandboxFolder><ReadOnly>false</ReadOnly></MappedFolder>
  </MappedFolders>
  <LogonCommand>
    <Command>powershell.exe -NoProfile -ExecutionPolicy Bypass -File C:\zuena\sandbox\run-tests.ps1</Command>
  </LogonCommand>
</Configuration>
"@ | Set-Content -Encoding UTF8 $wsb

Write-Host 'Abriendo Windows Sandbox (se cierra sola al terminar)...'
Start-Process -FilePath $sandboxExe -ArgumentList "`"$wsb`""
$deadline = (Get-Date).AddMinutes($TimeoutMinutes)
while (-not (Test-Path -LiteralPath (Join-Path $results 'done.txt'))) {
    if ((Get-Date) -gt $deadline) { throw "Sin resultados tras $TimeoutMinutes min. Revisá la ventana de la Sandbox." }
    Start-Sleep -Seconds 5
}
Get-Content -Encoding UTF8 (Join-Path $results 'resumen.txt')
foreach ($log in Get-ChildItem $results -Filter *.log) {
    Write-Host "`n--- $($log.Name) (final)"
    Get-Content -Encoding UTF8 $log.FullName -Tail 8
}
$failed = Select-String -Path (Join-Path $results 'resumen.txt') -Pattern "`t(FALLA|FALTA)" -Quiet
if ($failed) { exit 1 }
