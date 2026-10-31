param(
    [ValidatePattern('^DEC-PENDING-\d{3}$')]
    [string]$Id,

    [Parameter(Mandatory = $true)]
    [string]$Question,

    [string]$Owner,

    [ValidateSet('критический', 'высокий', 'средний', 'низкий')]
    [string]$Priority = 'высокий',

    [ValidateSet('active', 'waiting', 'later')]
    [string]$PanelStatus = 'active',

    [Parameter(Mandatory = $true)]
    [string]$PlayerCharacter,

    # Scenes of the choice (SCENE-…); the first one also fills -Scene when it is omitted.
    [Parameter(Mandatory = $true)]
    [string[]]$SceneIds,

    [string]$Scene = '',

    [string]$StoryDate = 'уточнить',

    [string]$Choice,

    [string]$PlayerAddition = 'ожидает решения.',

    [string]$ImmediateEffect = 'ожидает решения.',

    [string]$LongTermConsequences = 'ожидает решения.',

    [string[]]$Links = @('01_Кампания/03_Нерешенные_вопросы.md'),

    [switch]$SkipCheck
)

$ErrorActionPreference = 'Stop'

[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
$OutputEncoding = [System.Text.UTF8Encoding]::new()

. (Join-Path $PSScriptRoot '_lib.ps1')
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path

Invoke-VoidlightToolMain -Root $root -Name $MyInvocation.MyCommand.Name -ScriptBlock {
$today = Get-Date -Format 'yyyy-MM-dd'

function Format-LinkText {
    param([string[]]$RawLinks)

    $tick = [char]96
    $formatted = New-Object 'System.Collections.Generic.List[string]'

    foreach ($link in $RawLinks) {
        if ([string]::IsNullOrWhiteSpace($link)) {
            continue
        }

        $clean = $link.Trim().Trim('`')
        if (-not (Test-VoidlightReference $root $clean)) {
            throw "Decision link points to missing file: $clean"
        }

        $formatted.Add("$tick$clean$tick") | Out-Null
    }

    if ($formatted.Count -eq 0) {
        throw 'At least one valid decision link is required.'
    }

    return ($formatted.ToArray() -join ', ')
}

function Get-NextPendingDecisionId {
    param([object]$Registry)

    $decisionLogPath = Join-Path $root '01_Кампания\02_Журнал_решений.md'
    $openQuestionsPath = Join-Path $root '01_Кампания\03_Нерешенные_вопросы.md'
    $currentContextPath = Join-Path $root '01_Кампания\00_Текущий_контекст.md'
    $haystack = @(
        $Registry | ConvertTo-Json -Depth 8
        Read-VoidlightText $decisionLogPath -AllowMissing
        Read-VoidlightText $openQuestionsPath -AllowMissing
        Read-VoidlightText $currentContextPath -AllowMissing
    ) -join "`n"

    $numbers = @(
        [regex]::Matches($haystack, '\bDEC(?:-PENDING)?-(\d{3})\b') |
            ForEach-Object { [int]$_.Groups[1].Value }
    )

    $max = 0
    if ($numbers.Count -gt 0) {
        $max = [int](($numbers | Measure-Object -Maximum).Maximum)
    }

    return ('DEC-PENDING-{0:D3}' -f ($max + 1))
}

if ([string]::IsNullOrWhiteSpace($Owner)) {
    $Owner = $PlayerCharacter
}

if ([string]::IsNullOrWhiteSpace($Choice)) {
    $Choice = "ожидает решения: $Question"
}

# Every decision names the scenes of the choice; a new scene of the batch is already registered.
$SceneIds = @($SceneIds | Where-Object { $_ } | Select-Object -Unique)
if ($SceneIds.Count -eq 0) { throw 'A decision needs at least one scene ID (-SceneIds).' }
$graph = Read-VoidlightRegistry $root 'Сущности'
Assert-VoidlightEntityIds $graph $SceneIds -Type @('scene') -Label 'decision scene'
if ([string]::IsNullOrWhiteSpace($Scene)) {
    $Scene = @($graph.entities | Where-Object { $_.id -eq $SceneIds[0] })[0].path
}

$registry = Read-VoidlightRegistry $root 'Решения'
$decisions = @($registry.decisions)

if ([string]::IsNullOrWhiteSpace($Id)) {
    $Id = Get-NextPendingDecisionId -Registry $registry
}

if (@($decisions | Where-Object { $_.id -eq $Id }).Count -gt 0) {
    throw "Decision ID already exists in 09_Реестры/Решения.json: $Id"
}

if ($Id -match '^DEC-PENDING-(\d{3})$') {
    $acceptedId = 'DEC-{0}' -f $Matches[1]
    if (@($decisions | Where-Object { $_.id -eq $acceptedId }).Count -gt 0) {
        throw "Accepted decision with the same number already exists: $acceptedId"
    }
}

$linksText = Format-LinkText -RawLinks $Links

$decisions += [pscustomobject][ordered]@{
    id = $Id
    uid = 'DUID-' + [guid]::NewGuid().ToString('N')
    transitions = @()
    resolved_from = $null
    link_paths = @($Links)
    state = 'pending'
    real_date = 'ожидает решения'
    story_date = $StoryDate
    player_character = $PlayerCharacter
    scene = $Scene
    choice = $Choice
    player_addition = $PlayerAddition
    immediate_effect = $ImmediateEffect
    long_term_consequences = $LongTermConsequences
    links = $linksText
    status_text = 'ожидает решения.'
    priority = $Priority
    question = $Question
    owner = $Owner
    panel_status = $PanelStatus
    scene_ids = @($SceneIds)
}

$registry.decisions = @($decisions)
$registry.updated_real_date = $today

Write-VoidlightRegistry $root 'Решения' $registry

Complete-VoidlightMutation -Root $root -Rebuild 'Собрать_решения' -SkipCheck:$SkipCheck

"Created pending decision: $Id"
}
