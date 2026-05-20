param(
    [switch]$SkipPortraits,

    [switch]$SkipArchive,

    [switch]$SkipSceneIndex,

    [switch]$SkipSourceIndex
)

$ErrorActionPreference = 'Stop'

[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
$OutputEncoding = [System.Text.UTF8Encoding]::new()


. (Join-Path $PSScriptRoot '_lib.ps1')
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path

Invoke-WmmaToolMain -Root $root -Name $MyInvocation.MyCommand.Name -ScriptBlock {
function Invoke-Step {
    param(
        [string]$Name,
        [scriptblock]$Action
    )

    "`n== $Name =="
    & $Action
    if (-not $?) {
        exit 1
    }
}

if (-not $SkipSceneIndex) {
    Invoke-Step 'Сборка индекса сцен' {
        & (Join-Path $root 'tools\Собрать_индекс_сцен.ps1') -SkipCheck
    }
}

if (-not $SkipSourceIndex) {
    Invoke-Step 'Сборка индекса источников' {
        & (Join-Path $root 'tools\Собрать_индекс_источников.ps1') -SkipCheck
    }
}

Invoke-Step 'Сборка решений' {
    & (Join-Path $root 'tools\Собрать_решения.ps1') -SkipCheck
}

Invoke-Step 'Сборка вопросов' {
    & (Join-Path $root 'tools\Собрать_вопросы.ps1') -SkipCheck
}

Invoke-Step 'Сборка панели следующего хода' {
    & (Join-Path $root 'tools\Собрать_панель_хода.ps1') -SkipCheck
}

if (-not $SkipArchive) {
    Invoke-Step 'Проверка архива' {
        & (Join-Path $root 'tools\Проверить_архив.ps1')
    }
}

if (-not $SkipPortraits) {
    Invoke-Step 'Проверка портретов' {
        & (Join-Path $root 'tools\Проверить_портреты.ps1')
    }
}

Invoke-Step 'Общая проверка проекта' {
    & (Join-Path $root 'tools\Проверить_проект.ps1')
}

"`nTurn workspace is ready."
}
