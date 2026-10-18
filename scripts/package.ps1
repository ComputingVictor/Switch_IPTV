$ErrorActionPreference = 'Stop'
$project = Split-Path -Parent (Split-Path -Parent $PSCommandPath)
$nro = Join-Path $project 'SwitchIPTV.nro'
if (-not (Test-Path -LiteralPath $nro)) {
    throw 'No existe SwitchIPTV.nro. Compile satisfactoriamente antes de empaquetar.'
}
$dist = Join-Path $project 'dist\switch\SwitchIPTV'
New-Item -ItemType Directory -Force -Path $dist, (Join-Path $dist 'config'), (Join-Path $dist 'assets') | Out-Null
Copy-Item -LiteralPath $nro -Destination (Join-Path $dist 'SwitchIPTV.nro')
Copy-Item -LiteralPath (Join-Path $project 'config\settings.example.ini') -Destination (Join-Path $dist 'config\settings.example.ini')
if (Test-Path -LiteralPath (Join-Path $project 'config\settings.ini')) {
    Copy-Item -LiteralPath (Join-Path $project 'config\settings.ini') -Destination (Join-Path $dist 'config\settings.ini') -Force
}
Get-ChildItem -LiteralPath (Join-Path $project 'assets') -File | Copy-Item -Destination (Join-Path $dist 'assets')
Write-Host "Paquete creado en $dist"
