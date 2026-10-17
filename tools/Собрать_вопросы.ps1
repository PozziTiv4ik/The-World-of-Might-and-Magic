[CmdletBinding()]
param(
    [switch]$SkipCheck,

    [switch]$Only
)

$ErrorActionPreference = 'Stop'
if($Only){$SkipCheck=$true}

[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
$OutputEncoding = [System.Text.UTF8Encoding]::new()

. (Join-Path $PSScriptRoot '_lib.ps1')
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path

Invoke-WmmaToolMain -Root $root -Name $MyInvocation.MyCommand.Name -ScriptBlock {
$openQuestionsPath = Join-Path $root '01_Кампания\03_Нерешенные_вопросы.md'
$closedQuestionsPath = Join-Path $root '01_Кампания\03_Закрытые_вопросы.md'

function Get-ChapterQuestionsHeading {
    param([int]$Chapter)

    return "Вопросы главы $Chapter"
}

function Add-QuestionTable {
    param(
        [System.Collections.Generic.List[string]]$Lines,
        [string[]]$Header,
        [object[]]$Questions
    )

    $rows = @($Questions | ForEach-Object { ,@($_.id, $_.priority, $_.text, $_.owner, $_.status) })
    Add-WmmaMarkdownTable $Lines $Header $rows -Escape { param($Value) Format-WmmaTableCell $Value }
}

function Get-ActiveDecisionRows {
    $registry=Read-WmmaRegistry $root 'Решения'
    foreach($decision in $registry.decisions){
        if($decision.id -notmatch '^DEC-PENDING-\d{3}$' -and $decision.state -ne 'pending'){continue}
        ,@(
            $decision.id
            $(if([string]::IsNullOrWhiteSpace($decision.priority)){'высокий'}else{$decision.priority})
            $(if([string]::IsNullOrWhiteSpace($decision.question)){$decision.choice}else{$decision.question})
            $(if([string]::IsNullOrWhiteSpace($decision.owner)){$decision.player_character}else{$decision.owner})
            $(if([string]::IsNullOrWhiteSpace($decision.panel_status)){'active'}else{$decision.panel_status})
        )
    }
}

function Render-OpenQuestions {
    param([object]$Registry)

    $decisionRows = @(Get-ActiveDecisionRows)
    $questions = @($Registry.questions)
    $chapterNumber = [int]$Registry.current_chapter
    $chapterPrefix = "Q-C$chapterNumber"
    $chapterHeading = Get-ChapterQuestionsHeading -Chapter $chapterNumber
    $chapterQuestions = @($questions | Where-Object { $_.scope -eq 'chapter' -and $_.status -ne 'resolved' })
    $worldQuestions = @($questions | Where-Object { $_.scope -eq 'world' -and $_.status -ne 'resolved' })
    $lines = New-Object 'System.Collections.Generic.List[string]'

    $lines.Add('# Нерешенные вопросы')
    $lines.Add('')
    $lines.Add('---')
    $lines.Add('type: open_questions')
    $lines.Add('status: active')
    $lines.Add('canon_level: active')
    $lines.Add("current_chapter: $($Registry.current_chapter)")
    $lines.Add("updated_real_date: $($Registry.updated_real_date)")
    $lines.Add('generated_by: tools/Собрать_вопросы.ps1')
    $lines.Add('source_registry: 09_Реестры/Вопросы.json')
    $lines.Add('decision_registry: 09_Реестры/Решения.json')
    $lines.Add('---')
    $lines.Add('')
    $lines.Add('Этот файл пересобирается из `09_Реестры/Вопросы.json`; активные `DEC-PENDING-*` берутся из `09_Реестры/Решения.json`. Для создания вопросов используй `.\tools\Новый_вопрос.ps1`, для закрытия вопросов - `.\tools\Закрыть_вопрос.ps1`.')
    $lines.Add('')
    $lines.Add("Новый ``$chapterPrefix-*`` или ``Q-WORLD-*`` всегда получает следующий свободный номер по максимуму из ``09_Реестры/Вопросы.json``. Не переиспользуй ID закрытого вопроса.")
    $lines.Add('')
    $lines.Add('## Как читать приоритет')
    $lines.Add('')
    $lines.Add('- `критический` - решение нужно для ближайшего хода или может резко изменить карту.')
    $lines.Add('- `высокий` - активный сюжетный узел текущей главы.')
    $lines.Add('- `средний` - важное последствие, которое можно раскрыть после ближайших решений.')
    $lines.Add('- `низкий` - лор или фон, который стоит уточнить позже.')
    $lines.Add('')
    $lines.Add('## Как читать статус')
    $lines.Add('')
    $lines.Add('- `active` - ближайший блокирующий узел или решение, которое нужно держать перед глазами.')
    $lines.Add('- `waiting` - важный вопрос главы, который ждет сцены, решения или нового источника.')
    $lines.Add('- `later` - мировой или фоновый бэклог без давления на ближайший ход.')
    $lines.Add('')
    $lines.Add('Закрытый вопрос получает статус `resolved` в JSON-реестре и выводится в `01_Кампания/03_Закрытые_вопросы.md`; в этом файле остаются только `active`, `waiting` и `later`.')
    $lines.Add('')
    $lines.Add('## Активные решения')
    $lines.Add('')
    Add-WmmaMarkdownTable $lines @('ID', 'Приоритет', 'Вопрос', 'Владелец / ветка', 'Статус') $decisionRows -Escape { param($Value) Format-WmmaTableCell $Value }
    $lines.Add('')
    $lines.Add("## $chapterHeading")
    $lines.Add('')
    Add-QuestionTable -Lines $lines -Header @('ID', 'Приоритет', 'Вопрос', 'Владелец / ветка', 'Статус') -Questions $chapterQuestions
    $lines.Add('')
    $lines.Add('## Вопросы по миру')
    $lines.Add('')
    Add-QuestionTable -Lines $lines -Header @('ID', 'Приоритет', 'Вопрос', 'Область', 'Статус') -Questions $worldQuestions

    Write-WmmaText $openQuestionsPath (($lines -join "`n").TrimEnd() + "`n")
}

function Render-ClosedQuestions {
    param([object]$Registry)

    $questions = @($Registry.questions)
    $chapterNumber = [int]$Registry.current_chapter
    $chapterPrefix = "Q-C$chapterNumber"
    $chapterHeading = Get-ChapterQuestionsHeading -Chapter $chapterNumber
    $chapterQuestions = @($questions | Where-Object { $_.scope -eq 'chapter' -and $_.status -eq 'resolved' })
    $worldQuestions = @($questions | Where-Object { $_.scope -eq 'world' -and $_.status -eq 'resolved' })
    $history = @($Registry.history)
    $lines = New-Object 'System.Collections.Generic.List[string]'

    $lines.Add('# Закрытые вопросы')
    $lines.Add('')
    $lines.Add('---')
    $lines.Add('type: closed_questions')
    $lines.Add('status: active')
    $lines.Add('canon_level: support')
    $lines.Add("current_chapter: $($Registry.current_chapter)")
    $lines.Add("updated_real_date: $($Registry.updated_real_date)")
    $lines.Add('generated_by: tools/Собрать_вопросы.ps1')
    $lines.Add('source_registry: 09_Реестры/Вопросы.json')
    $lines.Add('---')
    $lines.Add('')
    $lines.Add('Этот файл пересобирается из `09_Реестры/Вопросы.json` и хранит вопросы, которые уже получили канонический ответ. Открытые вопросы и pending-решения остаются в `01_Кампания/03_Нерешенные_вопросы.md`.')
    $lines.Add('')
    $lines.Add("Новые ``$chapterPrefix-*`` и ``Q-WORLD-*`` создаются через ``.\tools\Новый_вопрос.ps1``; закрытый ID не переиспользуется.")
    $lines.Add('')
    $lines.Add("## $chapterHeading")
    $lines.Add('')
    Add-QuestionTable -Lines $lines -Header @('ID', 'Приоритет', 'Вопрос', 'Владелец / ветка', 'Статус') -Questions $chapterQuestions
    $lines.Add('')
    $lines.Add('## Вопросы по миру')
    $lines.Add('')
    Add-QuestionTable -Lines $lines -Header @('ID', 'Приоритет', 'Вопрос', 'Область', 'Статус') -Questions $worldQuestions
    $lines.Add('')
    $lines.Add('## История закрытия вопросов')
    foreach ($item in $history) {
        $lines.Add('')
        $lines.Add(('- {0} - `{1}`: {2}' -f $item.date, $item.id, (Format-WmmaTableCell $item.resolution)))
    }

    Write-WmmaText $closedQuestionsPath (($lines -join "`n").TrimEnd() + "`n")
}

$registry = Read-WmmaRegistry $root 'Вопросы'

Render-OpenQuestions -Registry $registry
Render-ClosedQuestions -Registry $registry

if (-not $Only) {
    Invoke-WmmaTool -Root $root -Name 'Собрать_срезы_реестров' -Arguments @{ SkipCheck = $true }
}

if (-not $SkipCheck) {
    Invoke-WmmaTool -Root $root -Name 'Собрать_панель_хода' -Arguments @{ SkipCheck = $true }
    Invoke-WmmaTool -Root $root -Name 'Проверить_проект'
}

"Updated question registry and Markdown views: 09_Реестры/Вопросы.json"
}
