<#
.SYNOPSIS
  Compila ZUENA Plugins dentro de Windows Sandbox (sin Smart App Control).
.DESCRIPTION
  En el PC de desarrollo Smart App Control puede bloquear juceaide (la herramienta de JUCE que CMake
  ejecuta al reconfigurar y al generar los recursos de versión), y entonces no se puede cambiar una
  versión ni compilar un VST3. La Sandbox no tiene esa protección.
  Se comparten Visual Studio y el SDK de Windows en solo lectura y el proyecto con escritura, en las
  MISMAS rutas que en el host: build\release y su build.ninja siguen siendo válidos y los binarios
  quedan acá. Las variables del compilador se toman del host (vcvars64) y se aplican adentro.
.EXAMPLE
  powershell -ExecutionPolicy Bypass -File tools\sandbox-build.ps1            (todo)
  powershell -ExecutionPolicy Bypass -File tools\sandbox-build.ps1 -Target ZuenaLuma_VST3
#>
param([string] $Target = '', [int] $TimeoutMinutes = 90)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$sandboxExe = Join-Path $env:windir 'System32\WindowsSandbox.exe'
$vs = 'C:\Program Files\Microsoft Visual Studio\2022\Community'
$kits = 'C:\Program Files (x86)\Windows Kits'
$work = Join-Path $root 'build\sandbox-build'

if (-not (Test-Path -LiteralPath $sandboxExe)) { throw 'Windows Sandbox no está activado.' }
if (Get-Process -Name 'WindowsSandbox*' -ErrorAction SilentlyContinue) { throw 'Ya hay una Windows Sandbox abierta; cerrala primero.' }
Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $work -Force | Out-Null

# Variables del compilador (las mismas que usa tools\build.cmd).
$dump = Join-Path $env:TEMP 'zuena-vcenv.cmd'
"@echo off`r`ncall `"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul 2>nul || exit /b 1`r`nset" | Set-Content -Encoding ASCII $dump
$keep = '^(INCLUDE|LIB|LIBPATH|PATH|VC[A-Za-z]*|VS[A-Za-z0-9]*|Windows[A-Za-z]*|UCRTVersion|UniversalCRTSdkDir|Framework[A-Za-z0-9]*|ExtensionSdkDir|DevEnvDir|Platform|VisualStudioVersion|CommandPromptType)='
cmd /c "`"$dump`"" | Where-Object { $_ -match $keep } | Set-Content -Encoding UTF8 (Join-Path $work 'vcenv.txt')
$Target | Set-Content -Encoding UTF8 (Join-Path $work 'target.txt')

$esc = { param($s) [System.Security.SecurityElement]::Escape($s) }
$wsb = Join-Path $env:TEMP 'zuena-build.wsb'
@"
<Configuration>
  <Networking>Disable</Networking>
  <ClipboardRedirection>Disable</ClipboardRedirection>
  <MemoryInMB>8192</MemoryInMB>
  <MappedFolders>
    <MappedFolder><HostFolder>$(& $esc $root)</HostFolder><SandboxFolder>$(& $esc $root)</SandboxFolder><ReadOnly>false</ReadOnly></MappedFolder>
    <MappedFolder><HostFolder>$(& $esc $vs)</HostFolder><SandboxFolder>$(& $esc $vs)</SandboxFolder><ReadOnly>true</ReadOnly></MappedFolder>
    <MappedFolder><HostFolder>$(& $esc $kits)</HostFolder><SandboxFolder>$(& $esc $kits)</SandboxFolder><ReadOnly>true</ReadOnly></MappedFolder>
  </MappedFolders>
  <LogonCommand>
    <Command>powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$(& $esc (Join-Path $PSScriptRoot 'sandbox\build-in-sandbox.ps1'))"</Command>
  </LogonCommand>
</Configuration>
"@ | Set-Content -Encoding UTF8 $wsb

Write-Host 'Compilando en Windows Sandbox (se cierra sola al terminar)...'
Start-Process -FilePath $sandboxExe -ArgumentList "`"$wsb`""
$deadline = (Get-Date).AddMinutes($TimeoutMinutes)
$done = Join-Path $work 'done.txt'
while (-not (Test-Path -LiteralPath $done)) {
    if ((Get-Date) -gt $deadline) { throw "Sin resultado tras $TimeoutMinutes min. Revisá $work\build.log" }
    Start-Sleep -Seconds 10
}
Get-Content -Encoding UTF8 (Join-Path $work 'build.log') -Tail 12
$code = [int](Get-Content -Encoding UTF8 $done | Select-Object -First 1)
Write-Host "Código de salida de la compilación: $code"
exit $code
