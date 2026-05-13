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
