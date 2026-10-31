param(
    # -KeepTemp is the former name of the same switch.
    [Alias('KeepTemp')]
    [switch]$KeepFixture
)

$ErrorActionPreference = 'Stop'

[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
$OutputEncoding = [System.Text.UTF8Encoding]::new()

. (Join-Path $PSScriptRoot '_lib.ps1')
. (Join-Path $PSScriptRoot 'lib/Testing.ps1')
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
Invoke-VoidlightToolMain -Root $root -Name $MyInvocation.MyCommand.Name -ScriptBlock {
$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("voidlight_tool_test_" + [guid]::NewGuid().ToString('N'))
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

    $text = Read-VoidlightText $Path
    if (-not $text.Contains($Expected)) {
        throw "Expected text was not found in $Path`: $Expected"
    }
}

function Assert-TextNotContains {
    param(
        [string]$Path,
        [string]$Unexpected
    )

    $text = Read-VoidlightText $Path
    if ($text.Contains($Unexpected)) {
        throw "Unexpected text was found in $Path`: $Unexpected"
    }
}

function Get-NextAcceptedDecisionId {
    param([string]$DecisionLogPath)

    $decisionLog = Read-VoidlightText $DecisionLogPath
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
        [string]$DecisionRegistryPath,
        [string]$ToolsRoot
    )

    $testQuestion = 'Тестовое pending-решение для проверки инструментов'
    $registry = Read-VoidlightJson $DecisionRegistryPath
    $existing = @(
        $registry.decisions |
            Where-Object {
                (Test-VoidlightPendingDecision $_) -and
                $_.question -eq $testQuestion
            }
    ) | Select-Object -First 1

    if ($null -ne $existing) {
        return $existing.id
    }

    $toolOutput = & (Join-Path $ToolsRoot 'Новое_решение.ps1') `
        -Question $testQuestion `
        -Owner 'Инструменты' `
        -PlayerCharacter 'Тест / Инструменты' `
        -Scene 'Проверка инструментов' `
        -SceneIds 'SCENE-0062' `
        -StoryDate 'тест инструментов' `
        -Choice 'ожидает решения' `
        -PlayerAddition 'временная запись, созданная только в копии проекта для проверки инструментов.' `
        -ImmediateEffect 'ожидает решения' `
        -LongTermConsequences 'тестовая запись должна закрыться инструментом.' `
        -Links 'tools/Проверить_инструменты.ps1' `
        -SkipCheck

    $toolOutput | ForEach-Object { Write-Host $_ }
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }

    $registry = Read-VoidlightJson $DecisionRegistryPath
    $created = @(
        $registry.decisions |
            Where-Object {
                (Test-VoidlightPendingDecision $_) -and
                $_.question -eq $testQuestion
            }
    ) | Select-Object -Last 1

    if ($null -eq $created) {
        throw 'Новое_решение.ps1 did not add the tool test pending decision to the decision registry.'
    }

    return $created.id
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
            '.voidlight',
            '.build',
            '/XF',
            'regnum-test*',
            'platform-demo*',
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
    $frontRegistryPath = Join-Path $copyRoot '09_Реестры\Фронты.json'
    $sourceIndexPath = Join-Path $copyRoot '08_Источники\00_Индекс_источников.md'
    $sceneIndexPath = Join-Path $copyRoot '01_Кампания\00_Индекс_сцен.md'
    $assetIndexPath = Join-Path $copyRoot '05_Активы_персонажей\00_Индекс_активов.md'
    $decisionLogPath = Join-Path $copyRoot '01_Кампания\02_Журнал_решений.md'
    $openQuestionsPath = Join-Path $copyRoot '01_Кампания\03_Нерешенные_вопросы.md'
    $closedQuestionsPath = Join-Path $copyRoot '01_Кампания\03_Закрытые_вопросы.md'
    $decisionRegistryPath = Join-Path $copyRoot '09_Реестры\Решения.json'
    $questionRegistryPath = Join-Path $copyRoot '09_Реестры\Вопросы.json'
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
        $frontRegistryAfter = Read-VoidlightJson $frontRegistryPath
        if (@($frontRegistryAfter.fronts | Where-Object { $_.id -eq 'FRONT-TOOL-TEST' }).Count -ne 1) {
            throw 'Новый_фронт.ps1 did not add FRONT-TOOL-TEST to the front registry.'
        }
    }

    Invoke-Step 'Обновить фронт' {
        & (Join-Path $toolsRoot 'Обновить_фронт.ps1') `
            -FrontId FRONT-TOOL-TEST `
            -State 'Тестовое состояние после обновления фронта' `
            -Risk 'Тестовый риск после обновления фронта' `
            -NextTrigger 'Тестовый триггер после обновления фронта' `
            -TimerStatus 'активен: тестовый таймер обновлен' `
            -TimerTrigger 'Тестовое срабатывание таймера после обновления' `
            -SkipCheck

        Assert-TextContains -Path $frontTrackerPath -Expected 'Тестовое состояние после обновления фронта'
        Assert-TextContains -Path $frontTrackerPath -Expected 'активен: тестовый таймер обновлен'
        $frontRegistryAfter = Read-VoidlightJson $frontRegistryPath
        $updatedFront = @($frontRegistryAfter.active_fronts | Where-Object { $_.id -eq 'FRONT-TOOL-TEST' })[0]
        $updatedTimer = @($frontRegistryAfter.timers | Where-Object { $_.id -eq 'FRONT-TOOL-TEST' })[0]
        if ($updatedFront.state -ne 'Тестовое состояние после обновления фронта') {
            throw 'Обновить_фронт.ps1 did not update active front state in registry.'
        }

        if ($updatedTimer.status -ne 'активен: тестовый таймер обновлен') {
            throw 'Обновить_фронт.ps1 did not update timer status in registry.'
        }
    }

    Invoke-Step 'Создать новый вопрос' {
        & (Join-Path $toolsRoot 'Новый_вопрос.ps1') `
            -Scope chapter `
            -Text 'Тестовый вопрос инструментов' `
            -Owner 'Инструменты' `
            -Priority 'высокий' `
            -Status waiting `
            -SkipCheck

        Assert-TextContains -Path $openQuestionsPath -Expected 'Тестовый вопрос инструментов'
        $questionRegistryAfter = Read-VoidlightJson $questionRegistryPath
        $createdQuestion = @($questionRegistryAfter.questions | Where-Object { $_.text -eq 'Тестовый вопрос инструментов' })
        if ($createdQuestion.Count -ne 1) {
            throw 'Новый_вопрос.ps1 did not add the test question to the question registry.'
        }

        if ($createdQuestion[0].attention -ne 'awaiting_evidence' -or -not $createdQuestion[0].waiting_for -or $createdQuestion[0].opened_in_chapter -ne $questionRegistryAfter.current_chapter) {
            throw 'Новый_вопрос.ps1 did not record attention, waiting_for and opened_in_chapter.'
        }
    }

    $currentContextBefore = Read-VoidlightText $currentContextPath
    $toolTestPendingId = Ensure-ToolTestPendingDecision `
        -DecisionRegistryPath $decisionRegistryPath `
        -ToolsRoot $toolsRoot

    Assert-TextContains -Path $decisionLogPath -Expected 'временная запись, созданная только в копии проекта для проверки инструментов.'
    Assert-TextContains -Path $openQuestionsPath -Expected 'Тестовое pending-решение для проверки инструментов'
    # The current context is generated: the write command leaves it to Собрать_контекст.
    if ((Read-VoidlightText $currentContextPath) -cne $currentContextBefore) {
        throw 'Новое_решение.ps1 edited the generated current context.'
    }

    $pendingRecord = @((Read-VoidlightJson $decisionRegistryPath).decisions | Where-Object { $_.id -eq $toolTestPendingId })[0]
    if (@($pendingRecord.scene_ids) -notcontains 'SCENE-0062') {
        throw 'Новое_решение.ps1 did not record scene_ids.'
    }

    & (Join-Path $toolsRoot 'Собрать_контекст.ps1') -SkipCheck | Out-Null
    Assert-TextContains -Path $currentContextPath -Expected 'Тестовое pending-решение для проверки инструментов'

    Invoke-Step 'Закрыть pending-решение не портит файлы при неизвестном ID' {
        $decisionRegistryBefore = Read-VoidlightText $decisionRegistryPath
        $decisionLogBefore = Read-VoidlightText $decisionLogPath
        $openQuestionsBefore = Read-VoidlightText $openQuestionsPath
        $missingPendingId = 'DEC-PENDING-999'
        $registryBefore = $decisionRegistryBefore | ConvertFrom-Json
        $registryIds = @($registryBefore.decisions | ForEach-Object { $_.id })
        if ($registryIds -contains $missingPendingId) {
            $missingPendingId = 'DEC-PENDING-998'
        }

        if ($registryIds -contains $missingPendingId) {
            throw 'Cannot find unused DEC-PENDING-* ID for failure test.'
        }

        Expect-Failure -Message 'Expected failure did not happen: Закрыть_решение.ps1 with unknown pending decision ID' -Action {
            & (Join-Path $toolsRoot 'Закрыть_решение.ps1') `
                -PendingId $missingPendingId `
                -AcceptedId 'DEC-999' `
                -Choice "Тестовое закрытие $missingPendingId" `
                -Effect "Тестовый эффект закрытия $missingPendingId" `
                -SkipCheck
        }

        $decisionRegistryAfter = Read-VoidlightText $decisionRegistryPath
        $decisionLogAfter = Read-VoidlightText $decisionLogPath
        $openQuestionsAfter = Read-VoidlightText $openQuestionsPath
        if ($decisionRegistryAfter -ne $decisionRegistryBefore) {
            throw 'Закрыть_решение.ps1 changed decision registry before failing.'
        }

        if ($decisionLogAfter -ne $decisionLogBefore) {
            throw 'Закрыть_решение.ps1 changed decision log before failing.'
        }

        if ($openQuestionsAfter -ne $openQuestionsBefore) {
            throw 'Закрыть_решение.ps1 changed open questions before failing.'
        }
    }

    Invoke-Step 'Закрыть pending-решение без порчи журнала' {
        $decisionLogBefore = Read-VoidlightText $decisionLogPath
        $pendingId = $toolTestPendingId
        if ($decisionLogBefore -notmatch "(?m)^###\s+$([regex]::Escape($pendingId))\s*$") {
            throw "Tool test pending decision was not found for Закрыть_решение.ps1 test: $pendingId"
        }

        $acceptedId = Get-NextAcceptedDecisionId -DecisionLogPath $decisionLogPath
        $choice = "Тестовое закрытие $pendingId"
        $effect = "Тестовый эффект закрытия $pendingId"

        & (Join-Path $toolsRoot 'Закрыть_решение.ps1') `
            -PendingId $pendingId `
            -AcceptedId $acceptedId `
            -Choice $choice `
            -Effect $effect `
            -SkipCheck

        & (Join-Path $toolsRoot 'Собрать_контекст.ps1') -SkipCheck | Out-Null
        $decisionLogAfter = Read-VoidlightText $decisionLogPath
        $openQuestionsAfter = Read-VoidlightText $openQuestionsPath
        $currentContextAfter = Read-VoidlightText $currentContextPath
        $decisionRegistryAfter = Read-VoidlightJson $decisionRegistryPath
        $acceptedRegistryDecision = @($decisionRegistryAfter.decisions | Where-Object { $_.id -eq $acceptedId })[0]

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

        if ($null -eq $acceptedRegistryDecision -or $acceptedRegistryDecision.state -ne 'accepted') {
            throw "Decision registry was not updated as accepted: $acceptedId"
        }

        if (@($decisionRegistryAfter.decisions | Where-Object { $_.id -eq $pendingId }).Count -ne 0) {
            throw "Decision registry still contains pending decision: $pendingId"
        }
    }

    Invoke-Step 'Закрыть вопрос не портит файлы при неизвестном ID' {
        $questionRegistryBefore = Read-VoidlightText $questionRegistryPath
        $openQuestionsBefore = Read-VoidlightText $openQuestionsPath
        $closedQuestionsBefore = Read-VoidlightText $closedQuestionsPath

        $missingQuestionId = 'Q-C3-999'
        $registryBefore = $questionRegistryBefore | ConvertFrom-Json
        $registryIds = @($registryBefore.questions | ForEach-Object { $_.id })
        if ($registryIds -contains $missingQuestionId) {
            $missingQuestionId = 'Q-WORLD-999'
        }

        if ($registryIds -contains $missingQuestionId) {
            throw 'Cannot find unused Q-* ID for failure test.'
        }

        Expect-Failure -Message 'Expected failure did not happen: Закрыть_вопрос.ps1 with unknown question ID' -Action {
            & (Join-Path $toolsRoot 'Закрыть_вопрос.ps1') `
                -QuestionId $missingQuestionId `
                -Resolution "Тестовое закрытие $missingQuestionId" `
                -SkipCheck
        }

        $questionRegistryAfter = Read-VoidlightText $questionRegistryPath
        $openQuestionsAfter = Read-VoidlightText $openQuestionsPath
        $closedQuestionsAfter = Read-VoidlightText $closedQuestionsPath

        if ($questionRegistryAfter -ne $questionRegistryBefore) {
            throw 'Закрыть_вопрос.ps1 changed the question registry before failing.'
        }

        if ($openQuestionsAfter -ne $openQuestionsBefore) {
            throw 'Закрыть_вопрос.ps1 changed open questions before failing.'
        }

        if ($closedQuestionsAfter -ne $closedQuestionsBefore) {
            throw 'Закрыть_вопрос.ps1 changed closed questions before failing.'
        }
    }

    Invoke-Step 'Закрыть открытый вопрос без порчи истории' {
        $openQuestionsBefore = Read-VoidlightText $openQuestionsPath
        $questionMatch = [regex]::Match($openQuestionsBefore, '(?m)^\|\s*(Q-(?:C\d+|WORLD)-\d{3})\s*\|(?:[^|]*\|){3}\s*(?:active|waiting|later)\s*\|\s*$')
        if (-not $questionMatch.Success) {
            throw 'No open Q-C*/Q-WORLD entry found for Закрыть_вопрос.ps1 test.'
        }

        $questionId = $questionMatch.Groups[1].Value
        $resolution = "Тестовое закрытие $questionId"
        $today = Get-Date -Format 'yyyy-MM-dd'
        $historyExpected = '- {0} - `{1}`: {2}' -f $today, $questionId, $resolution

        & (Join-Path $toolsRoot 'Закрыть_вопрос.ps1') `
            -QuestionId $questionId `
            -Resolution $resolution `
            -SkipCheck

        $openQuestionsAfter = Read-VoidlightText $openQuestionsPath
        $closedQuestionsAfter = Read-VoidlightText $closedQuestionsPath
        $questionRegistryAfter = Read-VoidlightJson $questionRegistryPath
        $registryQuestion = @($questionRegistryAfter.questions | Where-Object { $_.id -eq $questionId })[0]
        $registryHistory = @($questionRegistryAfter.history | Where-Object { $_.id -eq $questionId -and $_.resolution -eq $resolution })

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

        if ($null -eq $registryQuestion -or $registryQuestion.status -ne 'resolved') {
            throw "Question registry was not updated as resolved: $questionId"
        }

        if ($registryHistory.Count -ne 1) {
            throw "Question registry history was not updated once for: $questionId"
        }
    }

    Invoke-Step 'Параллельное закрытие вопросов не портит таблицы' {
        $openQuestionsBefore = Read-VoidlightText $openQuestionsPath
        $questionIds = @(
            [regex]::Matches($openQuestionsBefore, '(?m)^\|\s*(Q-(?:C\d+|WORLD)-\d{3})\s*\|(?:[^|]*\|){3}\s*(?:active|waiting|later)\s*\|\s*$') |
                ForEach-Object { $_.Groups[1].Value } |
                Select-Object -First 2
        )

        if ($questionIds.Count -lt 2) {
            'Skipped parallel close test: not enough open questions in temporary copy.'
            return
        }

        $jobs = foreach ($questionId in $questionIds) {
            Start-Job -ScriptBlock {
                param(
                    [string]$ToolsRoot,
                    [string]$QuestionId
                )

                & (Join-Path $ToolsRoot 'Закрыть_вопрос.ps1') `
                    -QuestionId $QuestionId `
                    -Resolution "Параллельное тестовое закрытие $QuestionId" `
                    -SkipCheck
            } -ArgumentList $toolsRoot, $questionId
        }

        try {
            Wait-Job -Job $jobs | Out-Null
            foreach ($job in $jobs) {
                $output = Receive-Job -Job $job -ErrorAction Stop
                if ($job.State -ne 'Completed') {
                    throw "Parallel close job failed: $($job.State) $output"
                }
            }
        } finally {
            Remove-Job -Job $jobs -Force -ErrorAction SilentlyContinue
        }

        $openQuestionsAfter = Read-VoidlightText $openQuestionsPath
        $closedQuestionsAfter = Read-VoidlightText $closedQuestionsPath
        $questionRegistryAfter = Read-VoidlightJson $questionRegistryPath
        foreach ($questionId in $questionIds) {
            if ($openQuestionsAfter -match "(?m)^\|\s*$([regex]::Escape($questionId))\s*\|") {
                throw "Parallel close left question in open table: $questionId"
            }

            if ($closedQuestionsAfter -notmatch "(?m)^\|\s*$([regex]::Escape($questionId))\s*\|(?:[^|]*\|){3}\s*resolved\s*\|\s*$") {
                throw "Parallel close did not move question to closed table: $questionId"
            }

            $registryQuestion = @($questionRegistryAfter.questions | Where-Object { $_.id -eq $questionId })[0]
            if ($null -eq $registryQuestion -or $registryQuestion.status -ne 'resolved') {
                throw "Parallel close did not resolve registry question: $questionId"
            }
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

    Invoke-Step 'Создать персонажа и локацию' {
        & (Join-Path $toolsRoot 'Новый_персонаж.ps1') `
            -Name 'Тестовый Персонаж Инструментов' `
            -Role 'тест инструментов' `
            -SkipCheck

        & (Join-Path $toolsRoot 'Новая_локация.ps1') `
            -Name 'Тестовая Локация Инструментов' `
            -Summary 'тестовая локация инструментов' `
            -FrontId FRONT-TOOL-TEST `
            -SkipCheck

        Assert-TextContains -Path (Join-Path $copyRoot '03_Персонажи\00_Индекс_персонажей.md') -Expected 'Тестовый Персонаж Инструментов'
        Assert-TextContains -Path (Join-Path $copyRoot '04_Локации\00_Индекс_локаций.md') -Expected 'Тестовая Локация Инструментов'
    }

    Invoke-Step 'Закрепить тестовый портрет' {
        Add-Type -AssemblyName System.Drawing
        $testPortraitPath = Join-Path $tempRoot 'portrait_test.png'
        $bitmap = [System.Drawing.Bitmap]::new(300, 400)
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        try {
            $graphics.Clear([System.Drawing.Color]::FromArgb(40, 60, 90))
            $bitmap.Save($testPortraitPath, [System.Drawing.Imaging.ImageFormat]::Png)
        } finally {
            $graphics.Dispose()
            $bitmap.Dispose()
        }

        & (Join-Path $toolsRoot 'Новый_портрет.ps1') `
            -Character 'Тестовый Персонаж Инструментов' `
            -ImagePath $testPortraitPath `
            -SkipCheck

        Assert-TextContains -Path (Join-Path $copyRoot '03_Персонажи\Тестовый_Персонаж_Инструментов.md') -Expected 'portrait_status: available'
        Assert-TextContains -Path (Join-Path $copyRoot '11_Медиа\Портреты_персонажей\Индекс_портретов.md') -Expected 'Тестовый Персонаж Инструментов'
    }

    Invoke-Step 'Новые ID видны следующим командам пакета' {
        $characterPath = Join-Path $copyRoot '03_Персонажи\Тестовый_Персонаж_Инструментов.md'
        $script:toolCharacterId = Get-VoidlightMeta (Read-VoidlightText $characterPath) 'id'
        if ($script:toolCharacterId -notmatch '^CHAR-\d{4}$') { throw "New character has no numeric ID: $script:toolCharacterId" }
        $graph = Read-VoidlightJson (Join-Path $copyRoot '09_Реестры\Сущности.json')
        if (@($graph.entities | Where-Object { $_.id -eq $script:toolCharacterId }).Count -ne 1) { throw 'New character is missing from the entity registry after -SkipCheck.' }
        $sceneOutput = & (Join-Path $toolsRoot 'Новая_сцена.ps1') -Branch 'Тестовая_ветка_инструментов' -Title 'Участник из того же пакета' -Status active -FrontId FRONT-TOOL-TEST -ParticipantIds $script:toolCharacterId -SkipCheck
        $script:toolScenePath = ([regex]::Match(($sceneOutput -join "`n"), '(?m)^Created scene:\s*(.+?)\s*$')).Groups[1].Value
        $script:toolSceneId = Get-VoidlightMeta (Read-VoidlightText (Join-Path $copyRoot $script:toolScenePath)) 'id'
        & (Join-Path $toolsRoot 'Новое_знание.ps1') -Text 'Тестовый факт инструментов.' -Truth confirmed -EvidenceIds $script:toolSceneId -SubjectIds $script:toolCharacterId -KnownTo $script:toolCharacterId -KnowledgeEvidence "$($script:toolCharacterId)|$($script:toolSceneId)|Присутствовал в тестовой сцене." -SkipCheck | Out-Null
        $fact = @((Read-VoidlightJson (Join-Path $copyRoot '09_Реестры\Знания.json')).facts | Where-Object { $_.text -eq 'Тестовый факт инструментов.' })[0]
        if ($fact.knowledge_evidence[0].basis -ne 'Присутствовал в тестовой сцене.') { throw 'Per-holder knowledge basis was not stored.' }
    }

    Invoke-Step 'Знание требует своё основание для каждого знающего' {
        $knowledgePath = Join-Path $copyRoot '09_Реестры\Знания.json'
        $before = Read-VoidlightText $knowledgePath
        Expect-Failure -Message 'Knowledge holder without a basis accepted' -Action {
            & (Join-Path $toolsRoot 'Новое_знание.ps1') -Text 'Без основания.' -Truth reported -EvidenceIds $script:toolSceneId -KnownTo @($script:toolCharacterId, 'CHAR-0059') -KnowledgeEvidence "$($script:toolCharacterId)|$($script:toolSceneId)|Присутствовал." -SkipCheck
        }
        if ((Read-VoidlightText $knowledgePath) -cne $before) { throw 'Rejected knowledge changed the registry.' }
    }

    Invoke-Step 'Дубли названий отклоняются, повтор не стирает карточку' {
        $characterPath = Join-Path $copyRoot '03_Персонажи\Тестовый_Персонаж_Инструментов.md'
        $before = Read-VoidlightText $characterPath
        $message = Expect-Failure -PassThru -Message 'Contained name was accepted' -Action {
            & (Join-Path $toolsRoot 'Новый_персонаж.ps1') -Name 'Тестовый Персонаж' -SkipCheck
        }
        if (-not $message.Contains($script:toolCharacterId)) { throw "Duplicate refusal does not name the existing card: $message" }
        Expect-Failure -Message 'Repeated name replaced the card' -Action { & (Join-Path $toolsRoot 'Новый_персонаж.ps1') -Name 'Тестовый Персонаж Инструментов' -SkipCheck }
        Expect-Failure -Message 'Removed -Force was accepted' -Action { & (Join-Path $toolsRoot 'Новый_персонаж.ps1') -Name 'Тестовый Персонаж Инструментов' -Force -SkipCheck }
        Expect-Failure -Message 'Repeated location replaced the card' -Action { & (Join-Path $toolsRoot 'Новая_локация.ps1') -Name 'Тестовая Локация Инструментов' -SkipCheck }
        if ((Read-VoidlightText $characterPath) -cne $before) { throw 'Refused creation changed the existing card.' }
        $output = & (Join-Path $toolsRoot 'Новый_персонаж.ps1') -Name 'Тестовый Персонаж' -DistinctFrom $script:toolCharacterId -SkipCheck
        $distinct = Read-VoidlightText (Join-Path $copyRoot '03_Персонажи\Тестовый_Персонаж.md')
        if (@(Get-VoidlightArrayMeta $distinct 'distinct_from') -notcontains $script:toolCharacterId) { throw 'Confirmed distinction was not recorded.' }
    }

    Invoke-Step 'Сцена из входящего связывает участников и фронты' {
        & (Join-Path $toolsRoot 'Сцена_из_входящего.ps1') `
            -Branch 'Тестовая_ветка_инструментов' `
            -Title 'Входящее с участниками' `
            -Text 'Текст входящего с участниками.' `
            -ParticipantIds CHAR-0059 `
            -FrontIds FRONT-TOOL-TEST `
            -SkipCheck | Out-Null
        $scene = @(Get-ChildItem -LiteralPath (Join-Path $copyRoot '01_Кампания\Ветки\Тестовая_ветка_инструментов') -Filter '*.md' | Where-Object { (Read-VoidlightText $_.FullName).Contains('# Сцена') -and (Get-VoidlightTitle (Read-VoidlightText $_.FullName)) -like '*Входящее с участниками*' })
        if ($scene.Count -ne 1) { throw 'Scene from the inbox was not created.' }
        $text = Read-VoidlightText $scene[0].FullName
        if (@(Get-VoidlightArrayMeta $text 'participant_ids') -notcontains 'CHAR-0059' -or @(Get-VoidlightArrayMeta $text 'front_ids') -notcontains 'FRONT-TOOL-TEST') { throw 'Participants or fronts were not passed to the scene.' }
    }

    Invoke-Step 'Ветка, общая точка, время и замена факта' {
        & (Join-Path $toolsRoot 'Обновить_контекст_ветки.ps1') -Branch 'Тестовая_ветка_инструментов' -Create -CharacterId $script:toolCharacterId -SceneIds $script:toolSceneId -Situation 'Техническая ветка проверки инструментов.' -SkipCheck | Out-Null
        $state = Read-VoidlightJson (Join-Path $copyRoot '09_Реестры\Контекст.json')
        if (@($state.branches | Where-Object { $_.name -eq 'Тестовая_ветка_инструментов' -and $_.character_id -eq $script:toolCharacterId }).Count -ne 1) { throw 'Branch was not created.' }
        Expect-Failure -Message 'Second branch for one character accepted' -Action {
            & (Join-Path $toolsRoot 'Обновить_контекст_ветки.ps1') -Branch 'Вторая_тестовая_ветка' -Create -CharacterId $script:toolCharacterId -SceneIds $script:toolSceneId -Situation 'Дубль.' -SkipCheck
        }
        $fact = @((Read-VoidlightJson (Join-Path $copyRoot '09_Реестры\Знания.json')).facts | Where-Object { $_.text -eq 'Тестовый факт инструментов.' })[0]
        & (Join-Path $toolsRoot 'Обновить_контекст_ветки.ps1') -Global -FactIds (@($state.fact_ids) + @($fact.id)) -SkipCheck | Out-Null
        & (Join-Path $toolsRoot 'Новое_знание.ps1') -Text 'Уточнённый тестовый факт инструментов.' -Truth confirmed -EvidenceIds $script:toolSceneId -Supersedes $fact.id -SkipCheck | Out-Null
        $knowledge = Read-VoidlightJson (Join-Path $copyRoot '09_Реестры\Знания.json')
        $newFact = @($knowledge.facts | Where-Object { $_.text -eq 'Уточнённый тестовый факт инструментов.' })[0]
        $oldFact = @($knowledge.facts | Where-Object { $_.id -eq $fact.id })[0]
        if ($oldFact.superseded_by -ne $newFact.id) { throw 'Old fact is not marked as replaced.' }
        $global = @((Read-VoidlightJson (Join-Path $copyRoot '09_Реестры\Контекст.json')).fact_ids)
        if ($global -contains $fact.id -or $global -notcontains $newFact.id) { throw 'Global context still points at the replaced fact.' }
        Expect-Failure -Message 'Fact replacement cycle accepted' -Action {
            & (Join-Path $toolsRoot 'Заменить_факт.ps1') -FactId $newFact.id -ReplacedBy $fact.id -SkipCheck
        }
        $turnList = & (Join-Path $toolsRoot 'Список_ходов.ps1') -Json
        if (@(($turnList -join "`n") | ConvertFrom-Json).Count -ne 0) { throw 'The copy without .voidlight reported turn journals.' }
        $sceneIds = @((Read-VoidlightJson (Join-Path $copyRoot '09_Реестры\Сущности.json')).entities | Where-Object { $_.path -like '01_Кампания/Ветки/Тестовая_ветка_инструментов/*' } | ForEach-Object { $_.id })
        $other = @($sceneIds | Where-Object { $_ -ne $script:toolSceneId })[0]
        & (Join-Path $toolsRoot 'Новая_связь_времени.ps1') -From $script:toolSceneId -To $other -Relation after -EvidenceIds $script:toolSceneId -SkipCheck | Out-Null
        Expect-Failure -Message 'Cyclic event order accepted' -Action {
            & (Join-Path $toolsRoot 'Новая_связь_времени.ps1') -From $other -To $script:toolSceneId -Relation after -EvidenceIds $script:toolSceneId -SkipCheck
        }
        $relations = @((Read-VoidlightJson (Join-Path $copyRoot '09_Реестры\Хронология_связей.json')).relations | Where-Object { $_.from -eq $script:toolSceneId })
        if ($relations.Count -ne 1) { throw 'Temporal relation was not recorded once.' }
    }

    Invoke-Step 'Индексы активов владельцев собираются из карточек' {
        & (Join-Path $toolsRoot 'Собрать_индекс_активов.ps1') -SkipCheck | Out-Null
        Assert-TextContains -Path (Join-Path $copyRoot '05_Активы_персонажей\Крэг-Хак\00_Индекс_активов.md') -Expected 'Титанический топор'
        Assert-TextContains -Path (Join-Path $copyRoot '05_Активы_персонажей\Лорд_Никитосиос\00_Индекс_активов.md') -Expected 'Багровая гидра'
        Assert-TextContains -Path (Join-Path $copyRoot '05_Активы_персонажей\Принц_Михаэль\00_Индекс_активов.md') -Expected 'Склады гильдии в Феликии'
        Assert-TextContains -Path (Join-Path $copyRoot '05_Активы_персонажей\Принц_Михаэль\00_Индекс_активов.md') -Expected '## Важное ограничение'
    }

    Invoke-Step 'Закрытый канон меняется только с записанной причиной' {
        $chapter = '01_Кампания/Главы/Глава_01_Две_недели_до_Дня_Единения.md'
        $path = Join-Path $copyRoot $chapter
        Write-VoidlightText $path ((Read-VoidlightText $path) + "`n")
        $context = & (Join-Path $toolsRoot 'Проверить_контекст.ps1') -AsObject
        if (@($context.Errors | Where-Object { $_ -like "*Closed canon changed*$chapter*" }).Count -ne 1) { throw 'Changed closed chapter passed the check.' }
        & (Join-Path $toolsRoot 'Обновить_защиту_канона.ps1') -Path $chapter -Reason 'Тест: перевод строки в конце файла' -SkipCheck | Out-Null
        $manifest = Read-VoidlightJson (Join-Path $copyRoot '10_Обслуживание\Миграция_v2.json')
        if (@($manifest.amendments | Where-Object { $_.path -eq $chapter -and $_.reason -like 'Тест:*' }).Count -ne 1) { throw 'Amendment was not recorded.' }
    }
    Invoke-Step 'Собрать индекс источников' {
        & (Join-Path $toolsRoot 'Собрать_индекс_источников.ps1') -SkipCheck
        Assert-TextContains -Path $sourceIndexPath -Expected 'Тестовое входящее инструментов'
        Assert-TextContains -Path $sourceIndexPath -Expected 'Входящее для сцены инструментов'
        Assert-TextContains -Path $sourceIndexPath -Expected '| processed | source_note | обработано | Тестовое входящее инструментов |'
    }

    Invoke-Step 'Собрать индекс активов персонажей' {
        & (Join-Path $toolsRoot 'Собрать_индекс_активов.ps1') -SkipCheck
        Assert-TextContains -Path $assetIndexPath -Expected 'Флагманский линкор'
        Assert-TextContains -Path $assetIndexPath -Expected 'Принц Михаэль'
    }

    Invoke-Step 'Единая проверка отклоняет повреждённые поля реестров' {
        foreach($case in @(
            @{name='Решения';list='decisions';field='state';message='invalid state'},
            @{name='Вопросы';list='questions';field='scope';message='invalid scope'},
            @{name='Фронты';list='urgent_forks';field='priority';message='invalid priority'}
        )){
            $path=Join-Path $copyRoot ('09_Реестры/'+$case.name+'.json')
            $original=Read-VoidlightText $path
            try {
                $broken=$original|ConvertFrom-Json
                $broken.($case.list)[0].($case.field)='INVALID-TEST-VALUE'
                Write-VoidlightJson $path $broken
                $result=& (Join-Path $copyRoot 'tools/Проверить_реестры.ps1') -Quiet -PassThru
                if(-not @($result.Errors|Where-Object {$_ -like ('*'+$case.message+'*')}).Count){throw "Invalid field was accepted: $($case.name)/$($case.field)"}
            } finally {Write-VoidlightText $path $original}
        }
    }

    Invoke-Step 'Финальная проверка временной копии' {
        & (Join-Path $toolsRoot 'Собрать_связи.ps1') -AssignMissingIds -SkipCheck
        & (Join-Path $toolsRoot 'Собрать_индекс_сцен.ps1') -SkipCheck
        & (Join-Path $toolsRoot 'Собрать_индекс_активов.ps1') -SkipCheck
        & (Join-Path $toolsRoot 'Собрать_решения.ps1') -SkipCheck
        & (Join-Path $toolsRoot 'Собрать_вопросы.ps1') -SkipCheck
        & (Join-Path $toolsRoot 'Собрать_фронты.ps1') -SkipCheck
        & (Join-Path $toolsRoot 'Собрать_панель_хода.ps1') -SkipCheck
        & (Join-Path $toolsRoot 'Собрать_контекст.ps1') -SkipCheck
        & (Join-Path $toolsRoot 'Собрать_память.ps1')
        & (Join-Path $toolsRoot 'Проверить_реестры.ps1')
        & (Join-Path $toolsRoot 'Проверить_проект.ps1')
    }

    "`nTool check completed successfully in: $copyRoot"
} finally {
    if ($KeepFixture) {
        "`nTemp copy kept: $copyRoot"
    } elseif (Test-Path -LiteralPath $tempRoot) {
        Remove-Item -LiteralPath $tempRoot -Recurse -Force
    }
}
}
