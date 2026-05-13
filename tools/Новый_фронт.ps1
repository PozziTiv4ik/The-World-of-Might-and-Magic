param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^FRONT-[A-Z0-9-]+$')]
    [string]$Id,

    [Parameter(Mandatory = $true)]
    [string]$Name,

    [string]$Description = '',

    [ValidateSet('критический', 'высокий', 'средний', 'низкий')]
    [string]$Priority = 'средний',

    [string]$Participants = 'Уточнить.',

    [string]$State = 'Уточнить.',

    [string]$Risk = 'Уточнить.',

    [string]$Trigger = 'Уточнить.',

    [string[]]$Links = @(),

    [switch]$Urgent,

    [string]$UrgentSummary = '',

    [string]$Timer = '',

    [string]$TimerStatus = 'активен',

    [switch]$SkipCheck
)

$ErrorActionPreference = 'Stop'

[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
$OutputEncoding = [System.Text.UTF8Encoding]::new()

function Format-ProjectReference {
    param([string]$Value)

    if ([string]::IsNullOrWhiteSpace($Value)) {
        return $null
    }

    $reference = $Value.Trim() -replace '\\', '/'

    if ($reference -match '^`.+`$' -or $reference -match '^[a-z]+://') {
        return $reference
    }

    return "``$reference``"
}

function Add-RowBeforeHeading {
    param(
        [string]$Text,
        [string]$Heading,
        [string]$Row
    )

    $escapedHeading = [regex]::Escape($Heading)
    $pattern = "(\r?\n)(##\s+$escapedHeading\s*)"
    $result = [regex]::Replace(
        $Text,
        $pattern,
        {
            param($match)
            return "$($match.Groups[1].Value)$Row$($match.Groups[1].Value)$($match.Groups[2].Value)"
        },
        1
    )

    if ($result -eq $Text) {
        throw "Heading not found: ## $Heading"
    }

    return $result
}

$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$frontTrackerPath = Join-Path $root '01_Кампания\06_Фронты_и_таймеры.md'
$frontTracker = Get-Content -Raw -Encoding UTF8 -LiteralPath $frontTrackerPath
$today = Get-Date -Format 'yyyy-MM-dd'

if ([string]::IsNullOrWhiteSpace($Description)) {
    $Description = $Name
}

if ($frontTracker -match "\b$([regex]::Escape($Id))\b") {
    throw "FRONT-ID already exists: $Id"
}

$linkReferences = @(
    $Links |
        ForEach-Object { Format-ProjectReference -Value $_ } |
        Where-Object { $_ }
)

$linksCell = if ($linkReferences.Count -gt 0) {
    $linkReferences -join ', '
} else {
    '-'
}

$frontTracker = [regex]::Replace(
    $frontTracker,
    '(?m)^updated_real_date:\s*.+$',
    "updated_real_date: $today",
    1
)

$frontTracker = Add-RowBeforeHeading `
    -Text $frontTracker `
    -Heading 'Срочные развилки' `
    -Row "| $Id | $Description |"

if ($Urgent) {
    $urgentText = if ([string]::IsNullOrWhiteSpace($UrgentSummary)) { $State } else { $UrgentSummary }
    $frontTracker = Add-RowBeforeHeading `
        -Text $frontTracker `
        -Heading 'Активные фронты' `
        -Row "| $Id | $Priority | $Name | $urgentText | $Trigger | $linksCell |"
}

$frontTracker = Add-RowBeforeHeading `
    -Text $frontTracker `
    -Heading 'Таймеры угроз' `
    -Row "| $Id | $Name | $Participants | $State | $Risk | $Trigger |"

if (-not [string]::IsNullOrWhiteSpace($Timer)) {
    $frontTracker = Add-RowBeforeHeading `
        -Text $frontTracker `
        -Heading 'Правило обновления' `
        -Row "| $Id | $Timer | $TimerStatus | $Trigger |"
}

Set-Content -LiteralPath $frontTrackerPath -Encoding UTF8 -Value $frontTracker

& (Join-Path $root 'tools\Собрать_панель_хода.ps1') -SkipCheck
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

if (-not $SkipCheck) {
    & (Join-Path $root 'tools\Проверить_проект.ps1')
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

"Created front: $Id"
