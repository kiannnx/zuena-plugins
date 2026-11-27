<#
.SYNOPSIS
  Genera las imágenes de la galería de ZUENA EQ para la ficha del launcher a partir de capturas REALES
  del editor (zeq_snapshot): vista clara, vista oscura y dos detalles recortados.
.DESCRIPTION
  El modo oscuro del snapshot sale de la preferencia %APPDATA%\ZUENA\ZuenaEQ.settings; el script la
  cambia solo durante la captura y deja el contenido original como estaba.
  Si Smart App Control bloquea zeq_snapshot.exe, ejecutalo de nuevo más tarde o en un PC sin la protección.
.EXAMPLE
  powershell -ExecutionPolicy Bypass -File tools\eq-gallery.ps1
#>
# -Light / -Dark: usar capturas ya hechas (p. ej. build\sandbox-results\eq-100.png y eq-oscuro.png, de
# tools\sandbox-tests.ps1) en lugar de ejecutar zeq_snapshot en este PC.
param([string] $Light = '', [string] $Dark = '')
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root 'build\release\plugins\ZuenaEQ\zeq_snapshot_artefacts\Release\zeq_snapshot.exe'
$out = Join-Path $root 'Free-VST-Client\catalog\packages'
$tmp = Join-Path $env:TEMP 'zuena-gallery'
$pref = Join-Path $env:APPDATA 'ZUENA\ZuenaEQ.settings'
New-Item -ItemType Directory -Force $tmp | Out-Null

function Invoke-Snapshot([string]$png, [bool]$dark) {
    $had = Test-Path -LiteralPath $pref
    $orig = if ($had) { [IO.File]::ReadAllText($pref) } else { $null }
    try {
        New-Item -ItemType Directory -Force (Split-Path $pref) | Out-Null
        [IO.File]::WriteAllText($pref, $(if ($dark) { "dark=1`n" } else { "dark=0`n" }))
        $p = Start-Process -FilePath $exe -ArgumentList "`"$png`"" -NoNewWindow -Wait -PassThru
        if ($p.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $png)) { throw "zeq_snapshot falló ($($p.ExitCode))" }
    } finally {
        if ($had) { [IO.File]::WriteAllText($pref, $orig) } else { Remove-Item -LiteralPath $pref -Force -ErrorAction SilentlyContinue }
    }
}

function Save-Jpeg([System.Drawing.Image]$img, [string]$path, [int]$quality = 92) {
    $codec = [System.Drawing.Imaging.ImageCodecInfo]::GetImageEncoders() | Where-Object MimeType -eq 'image/jpeg'
    $params = New-Object System.Drawing.Imaging.EncoderParameters 1
    $params.Param[0] = New-Object System.Drawing.Imaging.EncoderParameter ([System.Drawing.Imaging.Encoder]::Quality), ([long]$quality)
    $img.Save($path, $codec, $params)
}

function Save-Crop([string]$src, [string]$dst, [int]$x, [int]$y, [int]$w, [int]$h) {
    $img = [System.Drawing.Image]::FromFile($src)
    try {
        $bmp = New-Object System.Drawing.Bitmap $w, $h
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $g.InterpolationMode = 'HighQualityBicubic'
        $g.DrawImage($img, (New-Object System.Drawing.Rectangle 0, 0, $w, $h), (New-Object System.Drawing.Rectangle $x, $y, $w, $h), 'Pixel')
        $g.Dispose()
        Save-Jpeg $bmp $dst
        $bmp.Dispose()
    } finally { $img.Dispose() }
}

if ($Light -and $Dark) {
    $light = (Resolve-Path $Light).Path
    $dark = (Resolve-Path $Dark).Path
} else {
    $light = Join-Path $tmp 'light.png'
    $dark = Join-Path $tmp 'dark.png'
    Invoke-Snapshot $light $false
    Invoke-Snapshot $dark $true
}

foreach ($pair in @(@($light, 'zuena-eq-shot-claro.jpg'), @($dark, 'zuena-eq-shot-oscuro.jpg'))) {
    $img = [System.Drawing.Image]::FromFile($pair[0]); try { Save-Jpeg $img (Join-Path $out $pair[1]) } finally { $img.Dispose() }
}
# Detalles: coordenadas del editor a 1360x780 multiplicadas por la escala de la captura (2x, HD).
$s = 2
# Ficha de banda 0.3.5: 640×384 en (688, 314) para la banda 3 con dinámica (targetCardBounds del editor).
Save-Crop $light (Join-Path $out 'zuena-eq-shot-banda.jpg') (680 * $s) (306 * $s) (656 * $s) (400 * $s)
Save-Crop $dark (Join-Path $out 'zuena-eq-shot-curva.jpg') (18 * $s) (250 * $s) (712 * $s) (445 * $s)
Get-ChildItem $out -Filter 'zuena-eq-shot-*.jpg' | ForEach-Object { '{0}  {1:N0} KB' -f $_.Name, ($_.Length / 1KB) }
