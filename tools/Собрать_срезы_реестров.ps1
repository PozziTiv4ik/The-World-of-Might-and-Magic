param(
    [switch]$SkipCheck
)

$ErrorActionPreference = 'Stop'

[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
$OutputEncoding = [System.Text.UTF8Encoding]::new()

. (Join-Path $PSScriptRoot '_lib.ps1')
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path

Invoke-WmmaToolMain -Root $root -Name $MyInvocation.MyCommand.Name -ScriptBlock {
$today = Get-Date -Format 'yyyy-MM-dd'
$registryDir = Join-Path $root '09_Реестры'

$pendingDecisionPath = Join-Path $registryDir 'Решения_незакрытые.json'
$acceptedDecisionPath = Join-Path $registryDir 'Решения_закрытые.json'
$openQuestionPath = Join-Path $registryDir 'Вопросы_открытые.json'
$closedQuestionPath = Join-Path $registryDir 'Вопросы_закрытые.json'

function Save-JsonFile {
    param(
        [string]$Path,
        [object]$Value
    )

    $json = $Value | ConvertTo-Json -Depth 12
    Write-WmmaGeneratedText -Path $Path -Text ($json.TrimEnd() + "`n") -Format json
}

# Total and per-value counts of one field (status of questions, state of decisions).
function New-Counts {
    param(
        [object[]]$Items,
        [string]$Field,
        [string[]]$Values
    )

    $counts = [ordered]@{
        total = @($Items).Count
    }

    foreach ($value in $Values) {
        $counts[$value] = @($Items | Where-Object { $_.$Field -eq $value }).Count
    }

    return [pscustomobject]$counts
}

function New-ClosedQuestionRows {
    param(
        [object[]]$Questions,
        [object[]]$History
    )

    $historyById = @{}
    foreach ($item in $History) {
        if (-not [string]::IsNullOrWhiteSpace([string]$item.id)) {
            $historyById[[string]$item.id] = $item
        }
    }

    $rows = New-Object 'System.Collections.Generic.List[object]'
    foreach ($question in $Questions) {
        $historyItem = $null
        if ($historyById.ContainsKey([string]$question.id)) {
            $historyItem = $historyById[[string]$question.id]
        }

        $rows.Add([pscustomobject][ordered]@{
            id = $question.id
            priority = $question.priority
            text = $question.text
            owner = $question.owner
            scope = $question.scope
            status = $question.status
            resolved_real_date = if ($historyItem) { $historyItem.date } else { $null }
            resolution = if ($historyItem) { $historyItem.resolution } else { $null }
        }) | Out-Null
    }

    return @($rows.ToArray())
}

$decisionRegistry = Read-WmmaRegistry $root 'Решения'
$questionRegistry = Read-WmmaRegistry $root 'Вопросы'

$decisions = @($decisionRegistry.decisions)
$pendingDecisions = @($decisions | Where-Object { Test-WmmaPendingDecision $_ })
$acceptedDecisions = @($decisions | Where-Object { Test-WmmaAcceptedDecision $_ })

$questions = @($questionRegistry.questions)
$history = @($questionRegistry.history)
$openQuestions = @($questions | Where-Object { $_.status -ne 'resolved' })
$closedQuestions = @($questions | Where-Object { $_.status -eq 'resolved' })

$pendingDecisionView = [pscustomobject][ordered]@{
    type = 'pending_decision_view'
    status = 'active'
    canon_level = 'support'
    updated_real_date = $today
    generated_from = @('09_Реестры/Решения.json')
    count = $pendingDecisions.Count
    counts = New-Counts -Items $pendingDecisions -Field 'state' -Values @('pending')
    decisions = @($pendingDecisions)
}

$acceptedDecisionView = [pscustomobject][ordered]@{
    type = 'accepted_decision_view'
    status = 'active'
    canon_level = 'support'
    updated_real_date = $today
    generated_from = @('09_Реестры/Решения.json')
    count = $acceptedDecisions.Count
    counts = New-Counts -Items $acceptedDecisions -Field 'state' -Values @('accepted')
    decisions = @($acceptedDecisions)
}

$openQuestionView = [pscustomobject][ordered]@{
    type = 'open_question_view'
    status = 'active'
    canon_level = 'support'
    current_chapter = $questionRegistry.current_chapter
    updated_real_date = $today
    generated_from = @('09_Реестры/Вопросы.json')
    count = $openQuestions.Count
    counts = New-Counts -Items $openQuestions -Field 'status' -Values @('active', 'waiting', 'later')
    questions = @($openQuestions)
}

$closedQuestionRows = New-ClosedQuestionRows -Questions $closedQuestions -History $history
$closedQuestionView = [pscustomobject][ordered]@{
    type = 'closed_question_view'
    status = 'active'
    canon_level = 'support'
    current_chapter = $questionRegistry.current_chapter
    updated_real_date = $today
    generated_from = @('09_Реестры/Вопросы.json')
    count = $closedQuestionRows.Count
    counts = New-Counts -Items $closedQuestions -Field 'status' -Values @('resolved')
    questions = @($closedQuestionRows)
    history = @($history)
}

Save-JsonFile -Path $pendingDecisionPath -Value $pendingDecisionView
Save-JsonFile -Path $acceptedDecisionPath -Value $acceptedDecisionView
Save-JsonFile -Path $openQuestionPath -Value $openQuestionView
Save-JsonFile -Path $closedQuestionPath -Value $closedQuestionView

if (-not $SkipCheck) {
    Invoke-WmmaTool -Root $root -Name 'Проверить_реестры'
}

"Updated registry JSON views: 09_Реестры/Решения_незакрытые.json, 09_Реестры/Решения_закрытые.json, 09_Реестры/Вопросы_открытые.json, 09_Реестры/Вопросы_закрытые.json"
}
