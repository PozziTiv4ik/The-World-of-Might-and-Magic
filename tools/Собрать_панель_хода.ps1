param([int]$MaxDecisions,[int]$MaxQuestions,[int]$MaxFronts,[int]$MaxTimers,[string]$Branch='',[switch]$SkipCheck,[object]$Data)
$ErrorActionPreference='Stop'
# Omitted limits keep the defaults of Select-WmmaPanelItems (10 decisions, 8 questions, 10 fronts, 8 timers).
$limits=@{};foreach($name in @('MaxDecisions','MaxQuestions','MaxFronts','MaxTimers')){if($PSBoundParameters.ContainsKey($name)){$limits[$name]=$PSBoundParameters[$name]}}
. (Join-Path $PSScriptRoot '_lib.ps1')
$root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Invoke-WmmaToolMain -Root $root -Name $MyInvocation.MyCommand.Name -ScriptBlock {
    $Data=Resolve-WmmaReadModel $root $Data
    $selection=Get-WmmaSelection -Root $root -Branch $Branch -Data $Data
    $panel=Select-WmmaPanelItems $selection @limits
    $lines=[Collections.Generic.List[string]]::new()
    $lines.Add('# Следующий ход'); $lines.Add(''); $lines.Add('---'); $lines.Add('type: next_turn_panel'); $lines.Add('status: active'); $lines.Add('canon_level: support')
    $lines.Add("current_chapter: $($Data.chapter)"); $lines.Add('generated_by: tools/Собрать_панель_хода.ps1'); $lines.Add('---'); $lines.Add('')
    $lines.Add('Собирается напрямую из реестров решений, вопросов, фронтов и контекста. Порядок: связь с текущим ходом, ветка, приоритет, стабильный ID. Вопросы прошлых глав сохраняют исходные ID.'); $lines.Add('')
    function Add-PanelTable([string]$Heading,[string[]]$Header,[object[]]$Rows) {
        $lines.Add("## $Heading"); $lines.Add('')
        Add-WmmaMarkdownTable $lines $Header $Rows -Escape {param($Value) Convert-WmmaCell $Value}
        $lines.Add('')
    }
    Add-PanelTable 'Ближайшие решения' @('ID','Приоритет','Вопрос','Владелец / ветка','Статус') @($panel.decisions | ForEach-Object { ,@($_.id,$_.priority,$_.question,$_.owner,$_.panel_status) })
    Add-PanelTable 'Активные вопросы' @('ID','Приоритет','Вопрос','Владелец / ветка','Статус') @($panel.questions | ForEach-Object { ,@($_.id,$_.priority,$_.text,$_.owner,$_.status) })
    Add-PanelTable 'Срочные фронты' @('FRONT-ID','Приоритет','Фронт','Суть','Следующий триггер') @($panel.fronts | ForEach-Object { ,@($_.id,$_.priority,$_.front,$_.summary,$_.trigger) })
    Add-PanelTable 'Активные таймеры' @('FRONT-ID','Таймер','Статус','Что считать срабатыванием') @($panel.timers | ForEach-Object { ,@($_.id,$_.timer,$_.status,$_.trigger) })
    $lines.Add("Всего активных вопросов: $($selection.questions.Count); показано: $($panel.questions.Count). Остальные доступны в реестре и через Получить_контекст с указанием ветки или запроса.")
    Write-WmmaText (Join-Path $root '01_Кампания/07_Следующий_ход.md') (($lines -join "`n")+"`n")
    if (-not $SkipCheck) { Invoke-WmmaTool -Root $root -Name 'Проверить_проект' }
    'Updated next turn panel: 01_Кампания/07_Следующий_ход.md'
}
