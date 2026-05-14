param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^DEC-PENDING-\d{3}$')]
    [string]$PendingId,

    [Parameter(Mandatory = $true)]
    [ValidatePattern('^DEC-\d{3}$')]
    [string]$AcceptedId,

    [Parameter(Mandatory = $true)]
    [string]$Choice,

    [string]$Effect = 'Уточнить последствия в связанных файлах.',

    [switch]$SkipCheck
)

$ErrorActionPreference = 'Stop'

[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
$OutputEncoding = [System.Text.UTF8Encoding]::new()

$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$today = Get-Date -Format 'yyyy-MM-dd'

$openQuestionsPath = Join-Path $root '01_Кампания\03_Нерешенные_вопросы.md'
$openQuestions = Get-Content -Raw -Encoding UTF8 -LiteralPath $openQuestionsPath
$pendingRowPattern = "(?m)^\|\s*$([regex]::Escape($PendingId))\s*\|.*\|\s*$"

if ($openQuestions -notmatch $pendingRowPattern) {
    throw "Cannot find $PendingId row in open questions."
}

$updatedOpenQuestions = [regex]::Replace(
    $openQuestions,
    $pendingRowPattern,
    '',
    1
)

$decisionLogPath = Join-Path $root '01_Кампания\02_Журнал_решений.md'
$decisionLog = Get-Content -Raw -Encoding UTF8 -LiteralPath $decisionLogPath
$escapedPendingId = [regex]::Escape($PendingId)
$blockPattern = "(?ms)^###\s+$escapedPendingId\s*\r?\n.*?(?=^###\s+|^##\s+|\z)"
$blockMatch = [regex]::Match($decisionLog, $blockPattern)

if (-not $blockMatch.Success) {
    throw "Cannot find $PendingId heading in decision log."
}

$acceptedBlock = $blockMatch.Value
$acceptedBlock = [regex]::Replace($acceptedBlock, "(?m)^###\s+$escapedPendingId\s*$", "### $AcceptedId", 1)
$acceptedBlock = [regex]::Replace($acceptedBlock, '(?m)^Дата в реальности:\s*ожидает решения\s*$', "Дата в реальности: $today", 1)
$acceptedBlock = [regex]::Replace($acceptedBlock, '(?m)^Выбор:\s*.*$', "Выбор: $Choice", 1)
$acceptedBlock = [regex]::Replace($acceptedBlock, '(?m)^Немедленный эффект:\s*.*$', "Немедленный эффект: $Effect", 1)
$acceptedBlock = [regex]::Replace($acceptedBlock, '(?m)^Статус:\s*ожидает решения\.\s*$', 'Статус: принято.', 1)
$acceptedBlock = $acceptedBlock.Trim()

$updatedDecisionLog = $decisionLog.Remove($blockMatch.Index, $blockMatch.Length).TrimEnd()
$acceptedSectionMatch = [regex]::Match($updatedDecisionLog, '(?ms)^## Принятые решения\s*\r?\n.*?(?=^##\s+|\z)')
if (-not $acceptedSectionMatch.Success) {
    throw 'Cannot find accepted decisions section in decision log.'
}

$insertIndex = $acceptedSectionMatch.Index + $acceptedSectionMatch.Length
$beforeAcceptedInsert = $updatedDecisionLog.Substring(0, $insertIndex).TrimEnd()
$afterAcceptedInsert = $updatedDecisionLog.Substring($insertIndex).TrimStart()
$updatedDecisionLog = "$beforeAcceptedInsert`r`n`r`n$acceptedBlock"
if (-not [string]::IsNullOrWhiteSpace($afterAcceptedInsert)) {
    $updatedDecisionLog += "`r`n`r`n$afterAcceptedInsert"
}

$contextPath = Join-Path $root '01_Кампания\00_Текущий_контекст.md'
$contextLines = Get-Content -Encoding UTF8 -LiteralPath $contextPath
$contextLines = $contextLines | Where-Object { $_ -notmatch [regex]::Escape($PendingId) }
$renumbered = New-Object 'System.Collections.Generic.List[string]'
$insideImmediate = $false
$counter = 1

foreach ($line in $contextLines) {
    if ($line -eq '## Немедленные решения') {
        $insideImmediate = $true
        $counter = 1
        $renumbered.Add($line) | Out-Null
        continue
    }

    if ($insideImmediate -and $line -match '^##\s+') {
        $insideImmediate = $false
    }

    if ($insideImmediate -and $line -match '^\d+\.\s+`DEC-PENDING-\d{3}`') {
        $renumbered.Add(($line -replace '^\d+\.', "$counter.")) | Out-Null
        $counter++
    } else {
        $renumbered.Add($line) | Out-Null
    }
}

Set-Content -LiteralPath $decisionLogPath -Encoding UTF8 -Value $updatedDecisionLog
Set-Content -LiteralPath $openQuestionsPath -Encoding UTF8 -Value $updatedOpenQuestions
Set-Content -LiteralPath $contextPath -Encoding UTF8 -Value $renumbered

if (-not $SkipCheck) {
    & (Join-Path $root 'tools\Проверить_проект.ps1')
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

"Closed decision: $PendingId -> $AcceptedId"

