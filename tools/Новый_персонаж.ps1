param(
    [Parameter(Mandatory = $true)]
    [string]$Name,

    [string]$Role = 'уточнить',

    [ValidateSet('missing', 'planned', 'available')]
    [string]$PortraitStatus = 'missing',

    [string]$PortraitPath = 'null',

    [switch]$Force,

    [switch]$SkipCheck
)

$ErrorActionPreference = 'Stop'

[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
$OutputEncoding = [System.Text.UTF8Encoding]::new()

function Convert-ToProjectFileName {
    param([string]$Value)

    $safe = [regex]::Replace($Value.Trim(), '\s+', '_')
    $safe = $safe -replace '[\\/:*?"<>|]', ''
    $safe = $safe.Trim('_', '.', ' ')

    if ([string]::IsNullOrWhiteSpace($safe)) {
        throw 'Cannot build a safe file name from an empty character name.'
    }

    return $safe
}

$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$fileName = Convert-ToProjectFileName -Value $Name
$relativePath = "03_Персонажи/$fileName.md"
$targetPath = Join-Path $root ($relativePath -replace '/', '\')

if ((Test-Path -LiteralPath $targetPath) -and -not $Force) {
    throw "Character file already exists: $relativePath"
}

if ($PortraitStatus -eq 'available' -and ($PortraitPath -eq 'null' -or [string]::IsNullOrWhiteSpace($PortraitPath))) {
    throw 'portrait_status=available requires -PortraitPath.'
}

$content = @"
# $Name

---
type: character
status: active
canon_level: active
role: $Role
portrait: $PortraitPath
portrait_status: $PortraitStatus
---

## Кратко

$Role

## Текущее положение

Уточнить.

## Портрет

- Статус: $PortraitStatus
- Основной файл: $PortraitPath
- Идея для генерации: уточнить по карточке персонажа и стилю проекта.

## Прошлое

- Уточнить.

## Цели

- Уточнить.

## Связи

- Союзники: уточнить.
- Враги: уточнить.
- Семья: уточнить.
- Политические связи: уточнить.

## Секреты и неизвестное

- Уточнить.

## Связанные файлы

- ``03_Персонажи/00_Индекс_персонажей.md``
"@

Set-Content -LiteralPath $targetPath -Encoding UTF8 -Value $content

$indexPath = Join-Path $root '03_Персонажи\00_Индекс_персонажей.md'
$indexText = Get-Content -Raw -Encoding UTF8 -LiteralPath $indexPath
$portraitWord = switch ($PortraitStatus) {
    'available' { 'есть' }
    'planned' { 'запланирован' }
    default { 'нужен' }
}
$indexRow = "| $Name | $Role | $portraitWord | ``$relativePath`` |"

if ($indexText -notmatch [regex]::Escape($relativePath)) {
    if ($indexText -notmatch '## Добавлено инструментом') {
        $section = @"

## Добавлено инструментом

| Персонаж | Роль | Портрет | Файл |
| --- | --- | --- | --- |
$indexRow
"@
        $indexText = [regex]::Replace($indexText, '(\r?\n## Визуальные материалы)', "$section`$1", 1)
    } else {
        $indexText = [regex]::Replace($indexText, '(\r?\n## Визуальные материалы)', "`r`n$indexRow`$1", 1)
    }

    Set-Content -LiteralPath $indexPath -Encoding UTF8 -Value $indexText
}

if (-not $SkipCheck) {
    & (Join-Path $root 'tools\Проверить_проект.ps1')
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

"Created character: $relativePath"

