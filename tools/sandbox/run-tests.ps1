# Se ejecuta DENTRO de Windows Sandbox (máquina descartable, sin Smart App Control).
# Corre los tests nativos de ZUENA EQ sobre los binarios recién compilados y deja los resultados
# en la carpeta compartida C:\zuena\results. Al terminar apaga la Sandbox.
$ErrorActionPreference = 'Continue'
$bin = 'C:\zuena\build\plugins\ZuenaEQ'
$out = 'C:\zuena\results'
$summary = Join-Path $out 'resumen.txt'
Set-Location $out

function Invoke-Step([string]$name, [string]$exe, [string[]]$arguments) {
    $log = Join-Path $out "$name.log"
    if (-not (Test-Path -LiteralPath $exe)) {
        "$name`tFALTA`t$exe" | Add-Content -Encoding UTF8 $summary
        return
    }
    try {
        $startArgs = @{ FilePath = $exe; WorkingDirectory = $out; NoNewWindow = $true; Wait = $true; PassThru = $true
                        RedirectStandardOutput = $log; RedirectStandardError = "$log.err" }
        if ($arguments.Count -gt 0) { $startArgs.ArgumentList = $arguments } # vacío: Start-Process falla sin lanzar
        $p = Start-Process @startArgs
        $code = $p.ExitCode
        $state = if ($code -eq 0) { 'OK' } else { 'FALLA(0x{0:X8})' -f $code }
    } catch {
        $state = "NO ARRANCA: $($_.Exception.Message)"
    }
    "$name`t$state" | Add-Content -Encoding UTF8 $summary
}

"ZUENA Plugins - tests nativos en Windows Sandbox - $(Get-Date -Format s)" | Set-Content -Encoding UTF8 $summary
$snap = "$bin\zeq_snapshot_artefacts\Release\zeq_snapshot.exe"

# ZUENA EQ: motor, auditoría de la interfaz a 100 % y 70 %, rendimiento, capturas y carga como VST3.
Invoke-Step 'eq_run_tests' "$bin\run_tests.exe" @()
$env:ZEQ_SCALE = '100'
Invoke-Step 'eq_ui_audit_100' $snap @('audit')
Invoke-Step 'eq_bench' $snap @('bench')
Invoke-Step 'eq_captura_100' $snap @("$out\eq-100.png")
# Captura oscura para la galería (la preferencia global vive en %APPDATA%; la Sandbox se descarta al terminar).
$pref = Join-Path $env:APPDATA 'ZUENA\ZuenaEQ.settings'
New-Item -ItemType Directory -Force (Split-Path $pref) | Out-Null
Set-Content -Encoding ASCII $pref "dark=1"
Invoke-Step 'eq_captura_oscura' $snap @("$out\eq-oscuro.png")
Set-Content -Encoding ASCII $pref "dark=0"
$env:ZEQ_SCALE = '70'
Invoke-Step 'eq_ui_audit_70' $snap @('audit')
Invoke-Step 'eq_captura_70' $snap @("$out\eq-70.png")
Remove-Item Env:\ZEQ_SCALE
$vst3 = "$bin\ZuenaEQ_artefacts\Release\VST3\ZUENA EQ.vst3"
if (Test-Path -LiteralPath $vst3) { Invoke-Step 'eq_host_validate' "$bin\zeq_host_artefacts\Release\zeq_host.exe" @('validate', "`"$vst3`"") }

# ZUENA Luma: motor, auditoría del editor y carga como VST3.
$luma = 'C:\zuena\build\plugins\ZuenaLuma'
Invoke-Step 'luma_tests' "$luma\luma_tests.exe" @()
Invoke-Step 'luma_editor' "$luma\luma_qa_artefacts\Release\luma_qa.exe" @('editor', "$out\luma")
$lumaVst3 = "$luma\ZuenaLuma_artefacts\Release\VST3\ZUENA Luma.vst3"
if (Test-Path -LiteralPath $lumaVst3) { Invoke-Step 'luma_host' "$luma\luma_qa_artefacts\Release\luma_qa.exe" @('host', "`"$lumaVst3`"") }
# Compatibilidad: si hay una versión anterior en legacy\, sus proyectos tienen que sonar igual en la nueva.
$legacyVst3 = "$luma\legacy\ZUENA Luma.vst3"
if ((Test-Path -LiteralPath $legacyVst3) -and (Test-Path -LiteralPath $lumaVst3)) {
    Invoke-Step 'luma_legacy' "$luma\luma_qa_artefacts\Release\luma_qa.exe" @('legacy', "`"$legacyVst3`"", "`"$lumaVst3`"")
}
'FIN' | Set-Content -Encoding UTF8 (Join-Path $out 'done.txt')
Stop-Computer -Force
