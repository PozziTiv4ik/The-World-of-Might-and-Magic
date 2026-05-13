param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^FRONT-[A-Z0-9-]+$')]
    [string]$FrontId,

    [ValidateSet('критический', 'высокий', 'средний', 'низкий')]
    [string]$Priority,

    [string]$UrgentSummary,

    [string]$UrgentTrigger,

    [string]$State,

    [string]$Risk,

    [string]$NextTrigger,

    [string]$TimerStatus,

    [string]$TimerTrigger,

    [switch]$SkipCheck
)

$ErrorActionPreference = 'Stop'

[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
$OutputEncoding = [System.Text.UTF8Encoding]::new()

$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$frontPath = Join-Path $root '01_Кампания\06_Фронты_и_таймеры.md'
$fronts = Get-Content -Raw -Encoding UTF8 -LiteralPath $frontPath

function Normalize-Cell {
    param([string]$Value)

    return (($Value.Trim() -replace '\r?\n', ' ') -replace '\|', '/')
}

if ($fronts -notmatch "(?m)^\|\s*$([regex]::Escape($FrontId))\s*\|") {
    throw "FRONT-ID is not found: $FrontId"
}

$changed = $false

if ($Priority -or $UrgentSummary -or $UrgentTrigger) {
    $pattern = "(?m)^(\|\s*$([regex]::Escape($FrontId))\s*\|\s*)([^|]+)(\|\s*[^|]+\|\s*)([^|]+)(\|\s*)([^|]+)(\|\s*[^|]+\|\s*)$"
    if ($fronts -notmatch $pattern) {
        throw "Cannot find urgent fork row for $FrontId."
    }

    $fronts = [regex]::Replace($fronts, $pattern, {
        param($match)

        $newPriority = if ($Priority) { Normalize-Cell $Priority } else { $match.Groups[3].Value.Trim() }
        $newSummary = if ($UrgentSummary) { Normalize-Cell $UrgentSummary } else { $match.Groups[5].Value.Trim() }
        $newTrigger = if ($UrgentTrigger) { Normalize-Cell $UrgentTrigger } else { $match.Groups[7].Value.Trim() }
        return "$($match.Groups[1].Value)$newPriority$($match.Groups[4].Value)$newSummary$($match.Groups[6].Value)$newTrigger$($match.Groups[8].Value)"
    }, 1)
    $changed = $true
}

if ($State -or $Risk -or $NextTrigger) {
    $pattern = "(?m)^(\|\s*$([regex]::Escape($FrontId))\s*\|\s*[^|]+\|\s*[^|]+\|\s*)([^|]+)(\|\s*)([^|]+)(\|\s*)([^|]+)(\|\s*)$"
    if ($fronts -notmatch $pattern) {
        throw "Cannot find active front row for $FrontId."
    }

    $fronts = [regex]::Replace($fronts, $pattern, {
        param($match)

        $newState = if ($State) { Normalize-Cell $State } else { $match.Groups[3].Value.Trim() }
        $newRisk = if ($Risk) { Normalize-Cell $Risk } else { $match.Groups[5].Value.Trim() }
        $newNextTrigger = if ($NextTrigger) { Normalize-Cell $NextTrigger } else { $match.Groups[7].Value.Trim() }
        return "$($match.Groups[1].Value)$newState$($match.Groups[4].Value)$newRisk$($match.Groups[6].Value)$newNextTrigger$($match.Groups[8].Value)"
    }, 1)
    $changed = $true
}

if ($TimerStatus -or $TimerTrigger) {
    $pattern = "(?m)^(\|\s*$([regex]::Escape($FrontId))\s*\|\s*[^|]+\|\s*)([^|]+)(\|\s*)([^|]+)(\|\s*)$"
    if ($fronts -notmatch $pattern) {
        throw "Cannot find timer row for $FrontId."
    }

    $fronts = [regex]::Replace($fronts, $pattern, {
        param($match)

        $newStatus = if ($TimerStatus) { Normalize-Cell $TimerStatus } else { $match.Groups[2].Value.Trim() }
        $newTrigger = if ($TimerTrigger) { Normalize-Cell $TimerTrigger } else { $match.Groups[4].Value.Trim() }
        return "$($match.Groups[1].Value)$newStatus$($match.Groups[3].Value)$newTrigger$($match.Groups[5].Value)"
    }, 1)
    $changed = $true
}

if (-not $changed) {
    throw 'Nothing to update. Pass at least one update parameter.'
}

$fronts = [regex]::Replace($fronts, '(?m)^updated_real_date:\s*\d{4}-\d{2}-\d{2}\s*$', "updated_real_date: $(Get-Date -Format 'yyyy-MM-dd')", 1)
Set-Content -LiteralPath $frontPath -Encoding UTF8 -Value $fronts

if (-not $SkipCheck) {
    & (Join-Path $root 'tools\Собрать_панель_хода.ps1') -SkipCheck
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }

    & (Join-Path $root 'tools\Проверить_проект.ps1')
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

"Updated front: $FrontId"


