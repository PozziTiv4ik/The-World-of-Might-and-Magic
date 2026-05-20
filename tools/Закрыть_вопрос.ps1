param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^Q-(C2|WORLD)-\d{3}$')]
    [string]$QuestionId,

    [string]$Resolution = 'Закрыто решением или обновлением канона.',

    [switch]$SkipCheck
)

$ErrorActionPreference = 'Stop'

[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
$OutputEncoding = [System.Text.UTF8Encoding]::new()


. (Join-Path $PSScriptRoot '_lib.ps1')
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
Invoke-WmmaToolMain -Root $root -Name $MyInvocation.MyCommand.Name -ScriptBlock {
$today = Get-Date -Format 'yyyy-MM-dd'
$openQuestionsPath = Join-Path $root '01_Кампания\03_Нерешенные_вопросы.md'
$closedQuestionsPath = Join-Path $root '01_Кампания\03_Закрытые_вопросы.md'

function New-ClosedQuestionsText {
    param([string]$Date)

    return @(
        '# Закрытые вопросы',
        '',
        '---',
        'type: closed_questions',
        'status: active',
        'canon_level: support',
        'current_chapter: 2',
        "updated_real_date: $Date",
        '---',
        '',
        'Этот файл хранит вопросы, которые уже получили канонический ответ. Открытые вопросы и pending-решения остаются в `01_Кампания/03_Нерешенные_вопросы.md`.',
        '',
        '## Вопросы главы 2',
        '',
        '| ID | Приоритет | Вопрос | Владелец / ветка | Статус |',
        '| --- | --- | --- | --- | --- |',
        '',
        '## Вопросы по миру',
        '',
        '| ID | Приоритет | Вопрос | Область | Статус |',
        '| --- | --- | --- | --- | --- |',
        '',
        '## История закрытия вопросов'
    ) -join "`r`n"
}

function Add-RowToClosedSection {
    param(
        [string]$Text,
        [string]$Heading,
        [string]$Row,
        [string]$QuestionId
    )

    if ($Text -match "(?m)^\|\s*$([regex]::Escape($QuestionId))\s*\|") {
        return $Text
    }

    $escapedHeading = [regex]::Escape($Heading)
    $sectionPattern = "(?ms)(^##\s+$escapedHeading\s*\r?\n\s*\|[^\r\n]+\|\s*\r?\n\|[^\r\n]*---[^\r\n]*\|\s*\r?\n)(.*?)(?=\r?\n##\s+|\z)"
    $sectionMatch = [regex]::Match($Text, $sectionPattern)

    if (-not $sectionMatch.Success) {
        throw "Cannot find closed questions section: $Heading"
    }

    $sectionHeader = $sectionMatch.Groups[1].Value.TrimEnd()
    $sectionBody = $sectionMatch.Groups[2].Value.Trim()
    if ([string]::IsNullOrWhiteSpace($sectionBody)) {
        $replacement = "$sectionHeader`r`n$Row"
    } else {
        $replacement = "$sectionHeader`r`n$sectionBody`r`n$Row"
    }

    return $Text.Substring(0, $sectionMatch.Index) + $replacement + $Text.Substring($sectionMatch.Index + $sectionMatch.Length)
}

$openQuestions = Get-Content -Raw -Encoding UTF8 -LiteralPath $openQuestionsPath
$escapedQuestionId = [regex]::Escape($QuestionId)
$rowPattern = "(?m)^\|\s*$escapedQuestionId\s*\|.*\|\s*(active|waiting|later|resolved)\s*\|\s*(?:\r?\n)?"
$rowMatch = [regex]::Match($openQuestions, $rowPattern)

if (-not $rowMatch.Success) {
    if ((Test-Path -LiteralPath $closedQuestionsPath) -and (Get-Content -Raw -Encoding UTF8 -LiteralPath $closedQuestionsPath) -match "(?m)^\|\s*$escapedQuestionId\s*\|") {
        throw "$QuestionId is already closed in 01_Кампания/03_Закрытые_вопросы.md."
    }

    throw "Cannot find $QuestionId row with a status column."
}

$sourceRow = ($rowMatch.Value -replace '\r?\n\s*$', '').TrimEnd()
$resolvedRow = [regex]::Replace($sourceRow, '\|\s*(active|waiting|later|resolved)\s*\|\s*$', '| resolved |', 1)
$updatedOpenQuestions = [regex]::Replace($openQuestions, $rowPattern, '', 1)
$updatedOpenQuestions = [regex]::Replace($updatedOpenQuestions, '(?m)^updated_real_date:\s*.+$', "updated_real_date: $today", 1)
$updatedOpenQuestions = $updatedOpenQuestions.TrimEnd() + "`r`n"

if (Test-Path -LiteralPath $closedQuestionsPath) {
    $closedQuestions = Get-Content -Raw -Encoding UTF8 -LiteralPath $closedQuestionsPath
} else {
    $closedQuestions = New-ClosedQuestionsText -Date $today
}

$targetSection = if ($QuestionId -like 'Q-C2-*') { 'Вопросы главы 2' } else { 'Вопросы по миру' }
$updatedClosedQuestions = [regex]::Replace($closedQuestions, '(?m)^updated_real_date:\s*.+$', "updated_real_date: $today", 1)
$updatedClosedQuestions = Add-RowToClosedSection -Text $updatedClosedQuestions -Heading $targetSection -Row $resolvedRow -QuestionId $QuestionId

if ($updatedClosedQuestions -notmatch '(?m)^## История закрытия вопросов\s*$') {
    $updatedClosedQuestions = $updatedClosedQuestions.TrimEnd() + "`r`n`r`n## История закрытия вопросов`r`n"
}

$historyLine = '- {0} - `{1}`: {2}' -f $today, $QuestionId, $Resolution
if (-not $updatedClosedQuestions.Contains($historyLine)) {
    $updatedClosedQuestions = $updatedClosedQuestions.TrimEnd() + "`r`n`r`n$historyLine`r`n"
}

Set-Content -LiteralPath $closedQuestionsPath -Encoding UTF8 -Value $updatedClosedQuestions
Set-Content -LiteralPath $openQuestionsPath -Encoding UTF8 -Value $updatedOpenQuestions

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

"Closed question: $QuestionId"
}
