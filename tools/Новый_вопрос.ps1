param(
    [ValidateSet('chapter', 'world')]
    [string]$Scope = 'chapter',

    [Parameter(Mandatory = $true)]
    [string]$Text,

    [Parameter(Mandatory = $true)]
    [string]$Owner,

    [ValidateSet('критический', 'высокий', 'средний', 'низкий')]
    [string]$Priority = 'высокий',

    [ValidateSet('active', 'waiting', 'later')]
    [string]$Status = 'waiting',

    [ValidatePattern('^Q-(?:C\d+|WORLD)-\d{3}$')]
    [string]$Id,

    # Attention queue of context selection; by default from the status: active -> current,
    # waiting -> awaiting_evidence, later -> background. It never closes the question.
    [ValidateSet('current', 'awaiting_evidence', 'background')]
    [string]$Attention,

    [string]$AttentionReason = 'Внимание задано при записи вопроса.',

    # The condition that lets the question move on.
    [string]$WaitingFor = 'Новое событие или сообщение, отвечающее на сформулированный вопрос',

    # By default the current chapter; a chapter question always opens in it.
    [int]$OpenedInChapter = 0,

    [switch]$SkipCheck
)

$ErrorActionPreference = 'Stop'

[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
$OutputEncoding = [System.Text.UTF8Encoding]::new()

. (Join-Path $PSScriptRoot '_lib.ps1')
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path

Invoke-WmmaToolMain -Root $root -Name $MyInvocation.MyCommand.Name -ScriptBlock {
$today = Get-Date -Format 'yyyy-MM-dd'

function Get-QuestionPrefix {
    param(
        [object]$Registry,
        [string]$QuestionScope
    )

    if ($QuestionScope -eq 'world') {
        return 'Q-WORLD'
    }

    if ($null -eq $Registry.current_chapter) {
        throw 'Question registry has no current_chapter for chapter question ID generation.'
    }

    return "Q-C$($Registry.current_chapter)"
}

function Get-NextQuestionId {
    param(
        [object]$Registry,
        [string]$QuestionScope
    )

    $prefix = Get-QuestionPrefix -Registry $Registry -QuestionScope $QuestionScope
    $escapedPrefix = [regex]::Escape($prefix)
    $numbers = @(
        @($Registry.questions) + @($Registry.history) |
            ForEach-Object {
                if ($_.id -match "^$escapedPrefix-(\d{3})$") {
                    [int]$Matches[1]
                }
            }
    )

    $max = 0
    if ($numbers.Count -gt 0) {
        $max = [int](($numbers | Measure-Object -Maximum).Maximum)
    }

    return ('{0}-{1:D3}' -f $prefix, ($max + 1))
}

$registry = Read-WmmaRegistry $root 'Вопросы'
$questions = @($registry.questions)
$history = @($registry.history)
$expectedPrefix = Get-QuestionPrefix -Registry $registry -QuestionScope $Scope

if ([string]::IsNullOrWhiteSpace($Id)) {
    $Id = Get-NextQuestionId -Registry $registry -QuestionScope $Scope
} elseif ($Id -notlike "$expectedPrefix-*") {
    throw "Question ID $Id does not match scope '$Scope'. Expected prefix: $expectedPrefix"
}

if (@($questions | Where-Object { $_.id -eq $Id }).Count -gt 0) {
    throw "Question ID already exists in 09_Реестры/Вопросы.json: $Id"
}

if (@($history | Where-Object { $_.id -eq $Id }).Count -gt 0) {
    throw "Question ID already exists in question history: $Id"
}

Assert-WmmaSingleLine $AttentionReason 'AttentionReason'
Assert-WmmaSingleLine $WaitingFor 'WaitingFor'
if (-not $Attention) {
    $Attention = switch ($Status) { 'active' { 'current' } 'waiting' { 'awaiting_evidence' } default { 'background' } }
}
$currentChapter = [int]$registry.current_chapter
if ($OpenedInChapter -le 0) { $OpenedInChapter = $currentChapter }
if ($OpenedInChapter -gt $currentChapter) { throw "A question cannot open in a future chapter: $OpenedInChapter (current $currentChapter)." }
if ($Scope -eq 'chapter' -and $OpenedInChapter -ne $currentChapter) { throw "A chapter question opens in the current chapter $currentChapter." }

$questions += [pscustomobject][ordered]@{
    id = $Id
    priority = $Priority
    text = $Text
    owner = $Owner
    scope = $Scope
    status = $Status
    opened_in_chapter = $OpenedInChapter
    attention = $Attention
    attention_reason = $AttentionReason
    waiting_for = $WaitingFor
}

$registry.questions = @($questions)
$registry.updated_real_date = $today

Write-WmmaRegistry $root 'Вопросы' $registry
Complete-WmmaMutation -Root $root -Rebuild 'Собрать_вопросы' -SkipCheck:$SkipCheck

"Created question: $Id"
}
