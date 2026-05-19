param(
    [switch]$KeepTemp
)

$ErrorActionPreference = 'Stop'

[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
$OutputEncoding = [System.Text.UTF8Encoding]::new()

$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("wmma_tool_test_" + [guid]::NewGuid().ToString('N'))
$copyRoot = Join-Path $tempRoot 'project'

function Invoke-Step {
    param(
        [string]$Name,
        [scriptblock]$Action
    )

    "`n== $Name =="
    & $Action
    if (-not $? -or $LASTEXITCODE -ne 0) {
        throw "Step failed: $Name"
    }

    $global:LASTEXITCODE = 0
}

function Assert-TextContains {
    param(
        [string]$Path,
        [string]$Expected
    )

    $text = Get-Content -Raw -Encoding UTF8 -LiteralPath $Path
    if (-not $text.Contains($Expected)) {
        throw "Expected text was not found in $Path`: $Expected"
    }
}

function Assert-TextNotContains {
    param(
        [string]$Path,
        [string]$Unexpected
    )

    $text = Get-Content -Raw -Encoding UTF8 -LiteralPath $Path
    if ($text.Contains($Unexpected)) {
        throw "Unexpected text was found in $Path`: $Unexpected"
    }
}

function Get-NextAcceptedDecisionId {
    param([string]$DecisionLogPath)

    $decisionLog = Get-Content -Raw -Encoding UTF8 -LiteralPath $DecisionLogPath
    $numbers = @(
        [regex]::Matches($decisionLog, '(?m)^###\s+DEC-(\d{3})\s*$') |
            ForEach-Object { [int]$_.Groups[1].Value }
    )

    if ($numbers.Count -eq 0) {
        return 'DEC-001'
    }

    $max = [int](($numbers | Measure-Object -Maximum).Maximum)
    return ('DEC-{0:D3}' -f ($max + 1))
}

function Ensure-ToolTestPendingDecision {
    param(
        [string]$DecisionLogPath,
        [string]$OpenQuestionsPath
    )

    $decisionLog = Get-Content -Raw -Encoding UTF8 -LiteralPath $DecisionLogPath
    if ($decisionLog -match '(?m)^###\s+DEC-PENDING-\d{3}\s*$') {
        return
    }

    $openQuestions = Get-Content -Raw -Encoding UTF8 -LiteralPath $OpenQuestionsPath
    $pendingNumbers = @(
        [regex]::Matches(($decisionLog + "`n" + $openQuestions), 'DEC-PENDING-(\d{3})') |
            ForEach-Object { [int]$_.Groups[1].Value }
    )

    $nextNumber = 1
    if ($pendingNumbers.Count -gt 0) {
        $nextNumber = [int](($pendingNumbers | Measure-Object -Maximum).Maximum) + 1
    }

    $pendingId = 'DEC-PENDING-{0:D3}' -f $nextNumber
    $pendingBlock = @"
### $pendingId

Дата в реальности: ожидает решения
Дата в сюжете: тест инструментов
Игрок / персонаж: Тест / Инструменты
Сцена: Проверка инструментов
Выбор: ожидает решения
Дополнение игрока: временная запись, созданная только в копии проекта для проверки инструментов.
Немедленный эффект: ожидает решения
Долгосрочные последствия: тестовая запись должна закрыться инструментом.
Связанные файлы: `tools/Проверить_инструменты.ps1`
Статус: ожидает решения.
"@

    $waitingSectionMatch = [regex]::Match($decisionLog, '(?ms)^## Ожидают решения\s*\r?\n.*?(?=^##\s+|\z)')
    if (-not $waitingSectionMatch.Success) {
        throw 'Cannot find pending decisions section in decision log.'
    }

    $insertIndex = $waitingSectionMatch.Index + $waitingSectionMatch.Length
    $beforeInsert = $decisionLog.Substring(0, $insertIndex).TrimEnd()
    $afterInsert = $decisionLog.Substring($insertIndex).TrimStart()
    $decisionLog = "$beforeInsert`r`n`r`n$pendingBlock"
    if (-not [string]::IsNullOrWhiteSpace($afterInsert)) {
        $decisionLog += "`r`n`r`n$afterInsert"
    }

    $pendingRow = "| $pendingId | высокий | Тестовое pending-решение для проверки инструментов | Инструменты | active |"
    $activeSectionMatch = [regex]::Match($openQuestions, '(?ms)^## Активные решения\s*\r?\n.*?(?=^##\s+|\z)')
    if (-not $activeSectionMatch.Success) {
        throw 'Cannot find active decisions section in open questions.'
    }

    $activeSection = $activeSectionMatch.Value.TrimEnd()
    $updatedActiveSection = "$activeSection`r`n$pendingRow`r`n"
    $openQuestions = $openQuestions.Remove($activeSectionMatch.Index, $activeSectionMatch.Length)
    $openQuestions = $openQuestions.Insert($activeSectionMatch.Index, $updatedActiveSection)

    Set-Content -LiteralPath $DecisionLogPath -Encoding UTF8 -Value $decisionLog
    Set-Content -LiteralPath $OpenQuestionsPath -Encoding UTF8 -Value $openQuestions
}

function Invoke-ExpectedFailure {
    param(
        [string]$Name,
        [scriptblock]$Action
    )

    $failed = $false
    try {
        & $Action
    } catch {
        $failed = $true
    }

    if (-not $failed) {
        throw "Expected failure did not happen: $Name"
    }

    $global:LASTEXITCODE = 0
}

try {
    New-Item -ItemType Directory -Path $copyRoot -Force | Out-Null

    Invoke-Step 'Копия проекта во временную папку' {
        $robocopyArgs = @(
            $root,
            $copyRoot,
            '/E',
            '/XD',
            '.git',
            '/NFL',
            '/NDL',
            '/NJH',
            '/NJS',
            '/NP'
        )

        & robocopy @robocopyArgs | Out-Null
        if ($LASTEXITCODE -ge 8) {
            throw "robocopy failed with exit code $LASTEXITCODE"
        }

        $global:LASTEXITCODE = 0
    }

    $toolsRoot = Join-Path $copyRoot 'tools'
    $inboxPath = Join-Path $copyRoot '07_Черновики_и_идеи\Входящие_сообщения.md'
    $frontTrackerPath = Join-Path $copyRoot '01_Кампания\06_Фронты_и_таймеры.md'
    $sourceIndexPath = Join-Path $copyRoot '08_Источники\00_Индекс_источников.md'
    $sceneIndexPath = Join-Path $copyRoot '01_Кампания\00_Индекс_сцен.md'
    $decisionLogPath = Join-Path $copyRoot '01_Кампания\02_Журнал_решений.md'
    $openQuestionsPath = Join-Path $copyRoot '01_Кампания\03_Нерешенные_вопросы.md'
    $closedQuestionsPath = Join-Path $copyRoot '01_Кампания\03_Закрытые_вопросы.md'
    $currentContextPath = Join-Path $copyRoot '01_Кампания\00_Текущий_контекст.md'

    Invoke-Step 'Принять входящее с источником' {
        & (Join-Path $toolsRoot 'Принять_сообщение.ps1') `
            -Title 'Тестовое входящее инструментов' `
            -Text 'Тестовый текст для проверки инструментов.' `
            -Mode source `
            -SkipCheck

        Assert-TextContains -Path $inboxPath -Expected 'Тестовое входящее инструментов'
        Assert-TextContains -Path $inboxPath -Expected '08_Источники/'
    }

    Invoke-Step 'Обработать входящее' {
        & (Join-Path $toolsRoot 'Обработать_входящее.ps1') `
            -Title 'Тестовое входящее инструментов' `
            -Summary 'Тестовое входящее обработано инструментом.' `
            -Links 'tools/README.md' `
            -SkipCheck

        Assert-TextContains -Path $inboxPath -Expected 'Статус: обработано.'
        Assert-TextContains -Path $inboxPath -Expected 'Тестовое входящее обработано инструментом.'
    }

    Invoke-Step 'Создать новый фронт' {
        & (Join-Path $toolsRoot 'Новый_фронт.ps1') `
            -Id FRONT-TOOL-TEST `
            -Name 'Тестовый фронт инструментов' `
            -Description 'тестовая линия проверки инструментов' `
            -Participants 'Тестовые участники' `
            -State 'Тестовое состояние' `
            -Risk 'Тестовый риск' `
            -Trigger 'Тестовый триггер' `
            -Links '01_Кампания/Ветки/Александрос/Сцена_011_Багровый_Пик_и_возвращение_Эрин.md' `
            -Timer 'Тестовый таймер' `
            -SkipCheck

        Assert-TextContains -Path $frontTrackerPath -Expected 'FRONT-TOOL-TEST'
    }

    Ensure-ToolTestPendingDecision -DecisionLogPath $decisionLogPath -OpenQuestionsPath $openQuestionsPath

    Invoke-Step 'Закрыть pending-решение не портит вопросы при ошибке журнала' {
        $decisionLogBefore = Get-Content -Raw -Encoding UTF8 -LiteralPath $decisionLogPath
        $openQuestionsBefore = Get-Content -Raw -Encoding UTF8 -LiteralPath $openQuestionsPath
        $pendingMatch = [regex]::Match($decisionLogBefore, '(?m)^###\s+(DEC-PENDING-\d{3})\s*$')
        if (-not $pendingMatch.Success) {
            throw 'No DEC-PENDING entry found for rollback test.'
        }

        $pendingId = $pendingMatch.Groups[1].Value
        $escapedPendingId = [regex]::Escape($pendingId)
        $brokenDecisionLog = [regex]::Replace(
            $decisionLogBefore,
            "(?ms)^###\s+$escapedPendingId\s*\r?\n.*?(?=^###\s+|^##\s+|\z)",
            '',
            1
        )
        Set-Content -LiteralPath $decisionLogPath -Encoding UTF8 -Value $brokenDecisionLog

        Invoke-ExpectedFailure -Name 'Закрыть_решение.ps1 without decision block' -Action {
            & (Join-Path $toolsRoot 'Закрыть_решение.ps1') `
                -PendingId $pendingId `
                -AcceptedId (Get-NextAcceptedDecisionId -DecisionLogPath $decisionLogPath) `
                -Choice "Тестовое закрытие $pendingId" `
                -Effect "Тестовый эффект закрытия $pendingId" `
                -SkipCheck
        }

        $openQuestionsAfter = Get-Content -Raw -Encoding UTF8 -LiteralPath $openQuestionsPath
        if ($openQuestionsAfter -ne $openQuestionsBefore) {
            throw 'Закрыть_решение.ps1 changed open questions before failing.'
        }

        Set-Content -LiteralPath $decisionLogPath -Encoding UTF8 -Value $decisionLogBefore
    }

    Invoke-Step 'Закрыть pending-решение без порчи журнала' {
        $decisionLogBefore = Get-Content -Raw -Encoding UTF8 -LiteralPath $decisionLogPath
        $pendingMatch = [regex]::Match($decisionLogBefore, '(?m)^###\s+(DEC-PENDING-\d{3})\s*$')
        if (-not $pendingMatch.Success) {
            throw 'No DEC-PENDING entry found for Закрыть_решение.ps1 test.'
        }

        $pendingId = $pendingMatch.Groups[1].Value
        $acceptedId = Get-NextAcceptedDecisionId -DecisionLogPath $decisionLogPath
        $choice = "Тестовое закрытие $pendingId"
        $effect = "Тестовый эффект закрытия $pendingId"

        & (Join-Path $toolsRoot 'Закрыть_решение.ps1') `
            -PendingId $pendingId `
            -AcceptedId $acceptedId `
            -Choice $choice `
            -Effect $effect `
            -SkipCheck

        $decisionLogAfter = Get-Content -Raw -Encoding UTF8 -LiteralPath $decisionLogPath
        $openQuestionsAfter = Get-Content -Raw -Encoding UTF8 -LiteralPath $openQuestionsPath
        $currentContextAfter = Get-Content -Raw -Encoding UTF8 -LiteralPath $currentContextPath

        Assert-TextContains -Path $decisionLogPath -Expected "### $acceptedId"
        Assert-TextContains -Path $decisionLogPath -Expected "Выбор: $choice"
        Assert-TextContains -Path $decisionLogPath -Expected "Немедленный эффект: $effect"
        Assert-TextNotContains -Path $decisionLogPath -Unexpected "### $pendingId"
        Assert-TextNotContains -Path $openQuestionsPath -Unexpected 'resolved${4}'
        Assert-TextNotContains -Path $openQuestionsPath -Unexpected $pendingId

        if ($currentContextAfter -match "\b$([regex]::Escape($pendingId))\b") {
            throw "Current context still contains closed pending decision $pendingId."
        }

        if ($decisionLogAfter -notmatch '(?ms)## Формат записи.+?Выбор:\s*\r?\n.+?Немедленный эффект:\s*\r?\n') {
            throw 'Decision log format block was unexpectedly changed.'
        }

        if (([regex]::Matches($decisionLogAfter, [regex]::Escape($choice))).Count -ne 1) {
            throw 'Choice replacement touched more than the accepted decision block.'
        }
    }

    Invoke-Step 'Закрыть вопрос не портит открытые вопросы при ошибке архива' {
        $openQuestionsBefore = Get-Content -Raw -Encoding UTF8 -LiteralPath $openQuestionsPath
        $closedQuestionsBefore = Get-Content -Raw -Encoding UTF8 -LiteralPath $closedQuestionsPath
        $questionMatch = [regex]::Match($openQuestionsBefore, '(?m)^\|\s*(Q-(?:C2|WORLD)-\d{3})\s*\|(?:[^|]*\|){3}\s*(?:active|waiting|later)\s*\|\s*$')
        if (-not $questionMatch.Success) {
            throw 'No open Q-C2/Q-WORLD entry found for rollback test.'
        }

        $questionId = $questionMatch.Groups[1].Value
        $targetHeading = if ($questionId -like 'Q-C2-*') { 'Вопросы главы 2' } else { 'Вопросы по миру' }
        $brokenClosedQuestions = $closedQuestionsBefore -replace "(?m)^## $([regex]::Escape($targetHeading))\s*$", '## Сломанный раздел'
        Set-Content -LiteralPath $closedQuestionsPath -Encoding UTF8 -Value $brokenClosedQuestions

        Invoke-ExpectedFailure -Name 'Закрыть_вопрос.ps1 with broken closed question archive' -Action {
            & (Join-Path $toolsRoot 'Закрыть_вопрос.ps1') `
                -QuestionId $questionId `
                -Resolution "Тестовое закрытие $questionId" `
                -SkipCheck
        }

        $openQuestionsAfter = Get-Content -Raw -Encoding UTF8 -LiteralPath $openQuestionsPath
        if ($openQuestionsAfter -ne $openQuestionsBefore) {
            throw 'Закрыть_вопрос.ps1 changed open questions before failing.'
        }

        Set-Content -LiteralPath $closedQuestionsPath -Encoding UTF8 -Value $closedQuestionsBefore
    }

    Invoke-Step 'Закрыть открытый вопрос без порчи истории' {
        $openQuestionsBefore = Get-Content -Raw -Encoding UTF8 -LiteralPath $openQuestionsPath
        $questionMatch = [regex]::Match($openQuestionsBefore, '(?m)^\|\s*(Q-(?:C2|WORLD)-\d{3})\s*\|(?:[^|]*\|){3}\s*(?:active|waiting|later)\s*\|\s*$')
        if (-not $questionMatch.Success) {
            throw 'No open Q-C2/Q-WORLD entry found for Закрыть_вопрос.ps1 test.'
        }

        $questionId = $questionMatch.Groups[1].Value
        $resolution = "Тестовое закрытие $questionId"
        $today = Get-Date -Format 'yyyy-MM-dd'
        $historyExpected = '- {0} - `{1}`: {2}' -f $today, $questionId, $resolution

        & (Join-Path $toolsRoot 'Закрыть_вопрос.ps1') `
            -QuestionId $questionId `
            -Resolution $resolution `
            -SkipCheck

        $openQuestionsAfter = Get-Content -Raw -Encoding UTF8 -LiteralPath $openQuestionsPath
        $closedQuestionsAfter = Get-Content -Raw -Encoding UTF8 -LiteralPath $closedQuestionsPath
        Assert-TextNotContains -Path $openQuestionsPath -Unexpected '$QuestionId'
        Assert-TextNotContains -Path $openQuestionsPath -Unexpected 'resolved${'

        Assert-TextContains -Path $closedQuestionsPath -Expected $historyExpected
        Assert-TextNotContains -Path $closedQuestionsPath -Unexpected '$QuestionId'
        Assert-TextNotContains -Path $closedQuestionsPath -Unexpected 'resolved${'

        if ($openQuestionsAfter -match "(?m)^\|\s*$([regex]::Escape($questionId))\s*\|") {
            throw "Closed question row still exists in open questions: $questionId"
        }

        if ($closedQuestionsAfter -notmatch "(?m)^\|\s*$([regex]::Escape($questionId))\s*\|(?:[^|]*\|){3}\s*resolved\s*\|\s*$") {
            throw "Question row was not moved to closed questions as resolved: $questionId"
        }
    }

    Invoke-Step 'Создать сцену' {
        & (Join-Path $toolsRoot 'Новая_сцена.ps1') `
            -Branch 'Тестовая_ветка_инструментов' `
            -Title 'Проверка инструментов' `
            -FrontId FRONT-TOOL-TEST `
            -Status active `
            -Summary 'Тестовая сцена для проверки инструментов.' `
            -SkipCheck

        Assert-TextContains -Path $sceneIndexPath -Expected 'FRONT-TOOL-TEST'
    }

    Invoke-Step 'Создать сцену из входящего' {
        & (Join-Path $toolsRoot 'Сцена_из_входящего.ps1') `
            -Branch 'Тестовая_ветка_инструментов' `
            -Title 'Входящее для сцены инструментов' `
            -Text 'Текст входящего, которое должно стать сценой.' `
            -FrontId FRONT-TOOL-TEST `
            -SkipCheck

        Assert-TextContains -Path $inboxPath -Expected 'Входящее для сцены инструментов'
        Assert-TextContains -Path $inboxPath -Expected 'Создана новая сцена'
    }

    Invoke-Step 'Собрать индекс источников' {
        & (Join-Path $toolsRoot 'Собрать_индекс_источников.ps1') -SkipCheck
        Assert-TextContains -Path $sourceIndexPath -Expected 'Тестовое входящее инструментов'
        Assert-TextContains -Path $sourceIndexPath -Expected 'Входящее для сцены инструментов'
    }

    Invoke-Step 'Финальная проверка временной копии' {
        & (Join-Path $toolsRoot 'Собрать_индекс_сцен.ps1') -SkipCheck
        & (Join-Path $toolsRoot 'Собрать_панель_хода.ps1') -SkipCheck
        & (Join-Path $toolsRoot 'Проверить_проект.ps1')
    }

    "`nTool check completed successfully in: $copyRoot"
} finally {
    if ($KeepTemp) {
        "`nTemp copy kept: $copyRoot"
    } elseif (Test-Path -LiteralPath $tempRoot) {
        Remove-Item -LiteralPath $tempRoot -Recurse -Force
    }
}
