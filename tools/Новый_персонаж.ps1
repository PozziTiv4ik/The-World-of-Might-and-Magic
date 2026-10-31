param(
    [Parameter(Mandatory = $true)]
    [string]$Name,

    [string]$Role = 'уточнить',

    [ValidateSet('missing', 'planned', 'available')]
    [string]$PortraitStatus = 'missing',

    [string]$PortraitPath = 'null',

    # Other names of the same person.
    [string[]]$Aliases = @(),

    # IDs of existing characters with a similar name that are confirmed to be other people.
    [string[]]$DistinctFrom = @(),

    [switch]$SkipCheck
)

$ErrorActionPreference = 'Stop'

[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
$OutputEncoding = [System.Text.UTF8Encoding]::new()

. (Join-Path $PSScriptRoot '_lib.ps1')

$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
Invoke-VoidlightToolMain -Root $root -Name $MyInvocation.MyCommand.Name -ScriptBlock {
Assert-VoidlightSingleLine $Name 'Name'
foreach ($alias in $Aliases) { Assert-VoidlightSingleLine $alias 'Alias' }
$fileName = Convert-VoidlightFileName -Value $Name
$relativePath = "03_Персонажи/$fileName.md"
$targetPath = Join-Path $root ($relativePath -replace '/', '\')

# An existing card is never replaced: its ID and text stay. Edit it by hand instead.
if (Test-Path -LiteralPath $targetPath) {
    throw "Character file already exists: $relativePath. Edit the existing card to keep its ID and text."
}

if ($PortraitStatus -eq 'available' -and ($PortraitPath -eq 'null' -or [string]::IsNullOrWhiteSpace($PortraitPath))) {
    throw 'portrait_status=available requires -PortraitPath.'
}

# One person, one card: a title or alias equal to, or contained in, an existing one is refused.
$graph = Read-VoidlightRegistry $root 'Сущности'
Assert-VoidlightEntityIds $graph $DistinctFrom -Type @('character') -Label 'distinct character'
$similar = @(Find-VoidlightSimilarEntities @($graph.entities | Where-Object { $_.type -eq 'character' }) (@($Name) + @($Aliases)) -Except $DistinctFrom)
if ($similar.Count) {
    throw ("A character with the same or a contained name already exists:`n" + (($similar | ForEach-Object { "- $($_.id) $($_.name) ($($_.path)), совпадение: $($_.match)" }) -join "`n") +
        "`nUse the existing card (add an alias there) or, for a different person, repeat with -DistinctFrom <ID> and record the distinction in 03_Персонажи/00_Словарь_имен_и_алиасов.md.")
}

$id = New-VoidlightEntityId -Root $root -Type 'character'
$meta = @(
    'type: character'
    'status: active'
    'canon_level: active'
    "role: $Role"
    "portrait: $PortraitPath"
    "portrait_status: $PortraitStatus"
    "id: $id"
    "aliases: $(Format-VoidlightMetaArray $Aliases)"
)
if ($DistinctFrom.Count) { $meta += "distinct_from: $(Format-VoidlightMetaArray $DistinctFrom)" }

$content = @(
    "# $Name"
    ''
    '---'
    $meta
    '---'
    ''
    '## Кратко'
    ''
    $Role
    ''
    '## Текущее положение'
    ''
    'Уточнить.'
    ''
    '## Портрет'
    ''
    "- Статус: $PortraitStatus"
    "- Основной файл: $PortraitPath"
    '- Идея для генерации: уточнить по карточке персонажа и стилю проекта.'
    ''
    '## Прошлое'
    ''
    '- Уточнить.'
    ''
    '## Цели'
    ''
    '- Уточнить.'
    ''
    '## Связи'
    ''
    '- Союзники: уточнить.'
    '- Враги: уточнить.'
    '- Семья: уточнить.'
    '- Политические связи: уточнить.'
    ''
    '## Секреты и неизвестное'
    ''
    '- Уточнить.'
    ''
    '## Связанные файлы'
    ''
    '- `03_Персонажи/00_Индекс_персонажей.md`'
) -join "`n"

Write-VoidlightText $targetPath ($content + "`n")

Complete-VoidlightMutation -Root $root -Documents @($relativePath) -Rebuild 'Собрать_индекс_персонажей' -SkipCheck:$SkipCheck

"Created character: $relativePath ($id)"
}
