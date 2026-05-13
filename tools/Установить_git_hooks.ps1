$ErrorActionPreference = 'Stop'

[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
$OutputEncoding = [System.Text.UTF8Encoding]::new()

$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$hookPath = Join-Path $root '.githooks\pre-commit'

if (-not (Test-Path -LiteralPath $hookPath)) {
    throw 'Tracked pre-commit hook is missing: .githooks/pre-commit'
}

Push-Location $root
try {
    git config core.hooksPath .githooks
} finally {
    Pop-Location
}

'Git hooks installed: core.hooksPath=.githooks'


