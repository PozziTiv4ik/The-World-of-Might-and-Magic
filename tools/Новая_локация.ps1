param(
    [Parameter(Mandatory = $true)]
    [string]$Name,

    [string]$Summary = 'Уточнить.',

    [string]$FrontId = '-',

    # Other names of the same place.
    [string[]]$Aliases = @(),

    # IDs of existing locations with a similar name that are confirmed to be other places.
    [string[]]$DistinctFrom = @(),

    [switch]$SkipCheck
)

$ErrorActionPreference = 'Stop'

[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
$OutputEncoding = [System.Text.UTF8Encoding]::new()

. (Join-Path $PSScriptRoot '_lib.ps1')

$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
Invoke-WmmaToolMain -Root $root -Name $MyInvocation.MyCommand.Name -ScriptBlock {
Assert-WmmaSingleLine $Name 'Name'
Assert-WmmaSingleLine $Summary 'Summary'
foreach ($alias in $Aliases) { Assert-WmmaSingleLine $alias 'Alias' }
$fileName = Convert-WmmaFileName -Value $Name
$relativePath = "04_Локации/$fileName.md"
$targetPath = Join-Path $root ($relativePath -replace '/', '\')

# An existing card is never replaced: its ID and text stay. Edit it by hand instead.
if (Test-Path -LiteralPath $targetPath) {
    throw "Location file already exists: $relativePath. Edit the existing card to keep its ID and text."
}

# One place, one card: a title or alias equal to, or contained in, an existing one is refused.
$graph = Read-WmmaRegistry $root 'Сущности'
Assert-WmmaEntityIds $graph $DistinctFrom -Type @('location') -Label 'distinct location'
$similar = @(Find-WmmaSimilarEntities @($graph.entities | Where-Object { $_.type -eq 'location' }) (@($Name) + @($Aliases)) -Except $DistinctFrom)
if ($similar.Count) {
    throw ("A location with the same or a contained name already exists:`n" + (($similar | ForEach-Object { "- $($_.id) $($_.name) ($($_.path)), совпадение: $($_.match)" }) -join "`n") +
        "`nUse the existing card (add an alias there) or, for a different place, repeat with -DistinctFrom <ID> and record the distinction in 03_Персонажи/00_Словарь_имен_и_алиасов.md.")
}

$id = New-WmmaEntityId -Root $root -Type 'location'
$meta = @(
    'type: location'
    'status: active'
    'canon_level: active'
    "front_id: $FrontId"
    "id: $id"
    "aliases: $(Format-WmmaMetaArray $Aliases)"
)
if ($DistinctFrom.Count) { $meta += "distinct_from: $(Format-WmmaMetaArray $DistinctFrom)" }

$content = @(
    "# $Name"
    ''
    '---'
    $meta
    '---'
    ''
    '## Кратко'
    ''
    $Summary
    ''
    '## География'
    ''
    'Уточнить.'
    ''
    '## Значение'
    ''
    'Уточнить.'
    ''
    '## Правитель / силы'
    ''
    'Уточнить.'
    ''
    '## Ресурсы'
    ''
    '- Уточнить.'
    ''
    '## История'
    ''
    '- Уточнить.'
    ''
    '## Текущие события'
    ''
    '- Уточнить.'
    ''
    '## Связанные персонажи'
    ''
    '- Уточнить.'
) -join "`n"

Write-WmmaText $targetPath ($content + "`n")

$mapPath = Join-Path $root '04_Локации\00_Карта_и_регионы.md'
$mapText = Read-WmmaText $mapPath
$row = "- ``$relativePath`` - $Summary"

if ($mapText -notmatch [regex]::Escape($relativePath)) {
    if ($mapText -notmatch '## Локации, добавленные инструментом') {
        $mapText = $mapText.TrimEnd() + "`n`n## Локации, добавленные инструментом`n`n$row"
    } else {
        $mapText = $mapText.TrimEnd() + "`n$row"
    }

    Write-WmmaText $mapPath ($mapText + "`n")
}

Complete-WmmaMutation -Root $root -Documents @($relativePath) -Rebuild 'Собрать_индекс_локаций' -SkipCheck:$SkipCheck

"Created location: $relativePath ($id)"
}
