$ErrorActionPreference = 'Stop'
$project = Split-Path -Parent (Split-Path -Parent $PSCommandPath)
$nroCandidates = @(
    (Join-Path $project 'cmake-build-switch\SwitchIPTV.nro'),
    (Join-Path $project 'SwitchIPTV.nro')
)
$nro = $nroCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $nro) {
    throw 'SwitchIPTV.nro was not found. Build the application before packaging it.'
}
$dist = Join-Path $project 'dist\switch\SwitchIPTV'
New-Item -ItemType Directory -Force -Path $dist, (Join-Path $dist 'config') | Out-Null
Copy-Item -LiteralPath $nro -Destination (Join-Path $dist 'SwitchIPTV.nro')
Copy-Item -LiteralPath (Join-Path $project 'config\settings.example.ini') -Destination (Join-Path $dist 'config\settings.example.ini')
Write-Host "Package created in $dist"
