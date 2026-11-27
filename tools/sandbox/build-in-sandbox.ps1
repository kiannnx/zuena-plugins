# Se ejecuta DENTRO de Windows Sandbox (ver tools\sandbox-build.ps1). Aplica las variables del compilador
# tomadas del host y compila en build\sandbox-release, una carpeta propia: build\release usa rutas cortas
# de 8.3 (C:\PROGRA~1\MICROS~4\...) que en la Sandbox no existen. La primera vez configura desde cero
# (juceaide corre sin Smart App Control). Deja registro y código de salida en build\sandbox-build y se apaga.
$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$work = Join-Path $root 'build\sandbox-build'
$final = Join-Path $root 'build\sandbox-release'
# Se compila en un disco local de la Sandbox: mt.exe no puede leer manifiestos dentro de una carpeta
# compartida (c1010070). Al terminar se copian los resultados a build\sandbox-release del host.
$out = 'C:\zuena-build'
$log = Join-Path $work 'build.log'

foreach ($line in Get-Content -Encoding UTF8 (Join-Path $work 'vcenv.txt')) {
    $eq = $line.IndexOf('=')
    if ($eq -gt 0) { [Environment]::SetEnvironmentVariable($line.Substring(0, $eq), $line.Substring($eq + 1), 'Process') }
}
$env:PATH = "$env:PATH;C:\Windows\System32;C:\Windows;C:\Windows\System32\WindowsPowerShell\v1.0"
$vsCmake = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake'
$cmake = "$vsCmake\CMake\bin\cmake.exe"
$ninja = "$vsCmake\Ninja\ninja.exe"
$target = (Get-Content -Encoding UTF8 (Join-Path $work 'target.txt') -ErrorAction SilentlyContinue | Select-Object -First 1)

function Write-Log([string] $text) { $text | Out-File -Append -Encoding utf8 $log }
function Invoke-Logged([string[]] $arguments) {
    & $cmake @arguments 2>&1 | ForEach-Object { "$_" } | Out-File -Append -Encoding utf8 $log
    return $LASTEXITCODE
}

"Compilación en Windows Sandbox - $(Get-Date -Format s) - objetivo: $(if ($target) { $target } else { 'todo' })" | Out-File -Encoding utf8 $log
$code = 0
if (-not (Test-Path -LiteralPath (Join-Path $out 'CMakeCache.txt'))) {
    Write-Log '--- configuración'
    $code = Invoke-Logged @('-S', $root, '-B', $out, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
                           "-DCMAKE_MAKE_PROGRAM=$ninja", '-DCMAKE_C_COMPILER=cl', '-DCMAKE_CXX_COMPILER=cl')
}
if ($code -eq 0) {
    Write-Log '--- compilación'
    $buildArgs = @('--build', $out, '--parallel')
    if ($target) { $buildArgs += @('--target', $target.Trim()) }
    $code = Invoke-Logged $buildArgs
}
if ($code -eq 0) {
    # Solo lo que se usa en el host: plugins (VST3, Standalone) y herramientas de prueba, con la misma estructura.
    Write-Log '--- copia de resultados'
    robocopy (Join-Path $out 'plugins') (Join-Path $final 'plugins') /MIR /XD CMakeFiles /XF *.obj *.ilk *.pdb *.lib *.exp *.ninja_deps /NFL /NDL /NJH /NP | Out-File -Append -Encoding utf8 $log
    if ($LASTEXITCODE -ge 8) { $code = $LASTEXITCODE }
}
Write-Log "FIN $code"
"$code" | Set-Content -Encoding UTF8 (Join-Path $work 'done.txt')
Stop-Computer -Force
