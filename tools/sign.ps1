<#
.SYNOPSIS
  Firma con Authenticode los binarios de ZUENA Plugins (VST3, tests, launcher).

.DESCRIPTION
  Pensado para el certificado Certum "Standard Code Signing in the Cloud" (SimplySign).
  Requisito: tener SimplySign Desktop abierto y conectado, así el certificado aparece en el
  almacén personal de Windows. Smart App Control solo deja ejecutar código firmado por una
  autoridad del programa Trusted Root de Microsoft; sin firma, bloquea tests, plugin y launcher.

.EXAMPLE
  .\tools\sign.ps1 -Subject "Tu Nombre" -Paths build\release\plugins\ZuenaEQ\ZuenaEQ_artefacts\Release\VST3
#>
param(
    [Parameter(Mandatory)] [string] $Subject,          # nombre del titular tal como figura en el certificado
    [Parameter(Mandatory)] [string[]] $Paths,          # archivos o carpetas (se firman .exe, .dll y .vst3)
    [string] $TimestampUrl = "http://time.certum.pl"
)

$ErrorActionPreference = "Stop"
$signtool = Get-ChildItem "C:\Program Files (x86)\Windows Kits\10\bin" -Recurse -Filter signtool.exe |
    Where-Object FullName -match '\\x64\\' | Sort-Object FullName | Select-Object -Last 1 -ExpandProperty FullName
if (-not $signtool) { throw "No se encontró signtool.exe (instalá el Windows SDK)." }

$cert = Get-ChildItem Cert:\CurrentUser\My -CodeSigningCert | Where-Object Subject -like "*$Subject*" | Select-Object -First 1
if (-not $cert) { throw "No hay un certificado de firma de código con '$Subject'. ¿Está SimplySign Desktop conectado?" }

$files = foreach ($p in $Paths) {
    $item = Get-Item -LiteralPath $p
    if ($item.PSIsContainer) {
        Get-ChildItem -LiteralPath $item.FullName -Recurse -File | Where-Object Extension -in ".exe", ".dll", ".vst3"
    } else { $item }
}
if (-not $files) { throw "No hay binarios para firmar en: $($Paths -join ', ')" }

foreach ($f in $files) {
    & $signtool sign /sha1 $cert.Thumbprint /fd sha256 /tr $TimestampUrl /td sha256 /d "ZUENA Plugins" $f.FullName
    if ($LASTEXITCODE -ne 0) { throw "Falló la firma de $($f.FullName)" }
    & $signtool verify /pa /q $f.FullName
    if ($LASTEXITCODE -ne 0) { throw "La firma de $($f.FullName) no verifica" }
    Write-Host "Firmado: $($f.FullName)"
}
