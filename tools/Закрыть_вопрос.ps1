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

$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$today = Get-Date -Format 'yyyy-MM-dd'
$openQuestionsPath = Join-Path $root '01_Кампания\03_Нерешенные_вопросы.md'
$openQuestions = Get-Content -Raw -Encoding UTF8 -LiteralPath $openQuestionsPath
$rowPattern = "(?m)^(\|\s*$([regex]::Escape($QuestionId))\s*\|(?:[^|]*\|){3}\s*)(active|waiting|later|resolved)(\s*\|)\s*$"

if ($openQuestions -notmatch $rowPattern) {
    throw "Cannot find $QuestionId row with a status column."
}

$openQuestions = [regex]::Replace($openQuestions, $rowPattern, "`${1}resolved`${4}", 1)

if ($openQuestions -notmatch '(?m)^## История закрытия вопросов\s*$') {
    $openQuestions = $openQuestions.TrimEnd() + "`r`n`r`n## История закрытия вопросов`r`n"
}

$openQuestions = $openQuestions.TrimEnd() + "`r`n`r`n- $today - `$QuestionId`: $Resolution`r`n"
Set-Content -LiteralPath $openQuestionsPath -Encoding UTF8 -Value $openQuestions

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


