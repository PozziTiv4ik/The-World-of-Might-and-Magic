[CmdletBinding()]
param(
    [switch]$SkipCheck
)

$ErrorActionPreference = 'Stop'

[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
$OutputEncoding = [System.Text.UTF8Encoding]::new()

. (Join-Path $PSScriptRoot '_lib.ps1')
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path

Invoke-VoidlightToolMain -Root $root -Name $MyInvocation.MyCommand.Name -ScriptBlock {
$frontTrackerPath = Join-Path $root '01_Кампания\06_Фронты_и_таймеры.md'

function Format-LinksCell {
    param([object[]]$Links)

    $formatted = @(
        @($Links) |
            ForEach-Object { Format-VoidlightProjectReference $_ } |
            Where-Object { $_ }
    )

    if ($formatted.Count -eq 0) {
        return '-'
    }

    return ($formatted -join ', ')
}

function Render-FrontTracker {
    param([object]$Registry)

    $fronts = @($Registry.fronts)
    $urgentForks = @($Registry.urgent_forks)
    $activeFronts = @($Registry.active_fronts)
    $timers = @($Registry.timers)
    $rules = @($Registry.rules)
    $lines = New-Object 'System.Collections.Generic.List[string]'

    $lines.Add('# Фронты и таймеры')
    $lines.Add('')
    $lines.Add('---')
    $lines.Add('type: front_tracker')
    $lines.Add('status: active')
    $lines.Add('canon_level: active')
    $lines.Add("current_chapter: $($Registry.current_chapter)")
    $lines.Add("date_in_story: $($Registry.date_in_story)")
    $lines.Add("updated_real_date: $($Registry.updated_real_date)")
    $lines.Add('generated_by: tools/Собрать_фронты.ps1')
    $lines.Add('source_registry: 09_Реестры/Фронты.json')
    $lines.Add('---')
    $lines.Add('')
    $lines.Add('Этот файл пересобирается из `09_Реестры/Фронты.json`. Для создания фронта используй `.\tools\Новый_фронт.ps1`, для обновления фронта или таймера - `.\tools\Обновить_фронт.ps1`.')
    $lines.Add('')
    $lines.Add('Главная точка текущей главы остается в `01_Кампания/01_Активная_глава.md`. Здесь фиксируются не новые события, а удобная карта уже зафиксированных фронтов и ожидаемых триггеров.')
    $lines.Add('')
    $escape = { param($Value) Format-VoidlightTableCell $Value }
    $lines.Add('## Справочник FRONT-ID')
    $lines.Add('')
    Add-VoidlightMarkdownTable $lines @('ID', 'Фронт') @($fronts | ForEach-Object { ,@($_.id, $_.name) }) -Escape $escape
    $lines.Add('')
    $lines.Add('## Срочные развилки')
    $lines.Add('')
    # The links cell is a list of formatted references; the other cells are escaped here.
    Add-VoidlightMarkdownTable $lines @('ID', 'Приоритет', 'Фронт', 'Суть', 'Следующий триггер', 'Связанные файлы') @(
        $urgentForks | ForEach-Object {
            ,@((& $escape $_.id), (& $escape $_.priority), (& $escape $_.front), (& $escape $_.summary), (& $escape $_.trigger), (Format-LinksCell @($_.links)))
        }
    )
    $lines.Add('')
    $lines.Add('## Активные фронты')
    $lines.Add('')
    Add-VoidlightMarkdownTable $lines @('ID', 'Фронт', 'Контроль / участники', 'Текущее состояние', 'Риск', 'Следующий триггер') @(
        $activeFronts | ForEach-Object { ,@($_.id, $_.front, $_.participants, $_.state, $_.risk, $_.next_trigger) }
    ) -Escape $escape
    $lines.Add('')
    $lines.Add('## Таймеры угроз')
    $lines.Add('')
    Add-VoidlightMarkdownTable $lines @('ID', 'Таймер', 'Статус', 'Что считать срабатыванием') @(
        $timers | ForEach-Object { ,@($_.id, $_.timer, $_.status, $_.trigger) }
    ) -Escape $escape
    $lines.Add('')
    $lines.Add('## Правило обновления')
    $lines.Add('')
    $lines.Add('После каждого крупного сюжетного апдейта текущей кампании обновлять:')
    $lines.Add('')
    for ($i = 0; $i -lt $rules.Count; $i++) {
        $lines.Add(('{0}. {1}' -f ($i + 1), $rules[$i]))
    }

    Write-VoidlightText $frontTrackerPath (($lines -join "`n").TrimEnd() + "`n")
}

$registry = Read-VoidlightRegistry $root 'Фронты'

Render-FrontTracker -Registry $registry

if (-not $SkipCheck) {
    Invoke-VoidlightTool -Root $root -Name 'Собрать_панель_хода' -Arguments @{ SkipCheck = $true }
    Invoke-VoidlightTool -Root $root -Name 'Проверить_проект'
}

"Updated front registry and Markdown tracker: 09_Реестры/Фронты.json"
}
