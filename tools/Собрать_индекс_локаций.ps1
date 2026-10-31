param(
    [switch]$SkipCheck
)

$ErrorActionPreference = 'Stop'

[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
$OutputEncoding = [System.Text.UTF8Encoding]::new()

. (Join-Path $PSScriptRoot '_lib.ps1')
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path

Invoke-VoidlightToolMain -Root $root -Name $MyInvocation.MyCommand.Name -ScriptBlock {
$locationRoot = Join-Path $root '04_Локации'
$targetPath = Join-Path $locationRoot '00_Индекс_локаций.md'

# Rows of the previous index keep their order; new cards go to the end.
$existingRows = (Get-VoidlightIndexRows -Text (Read-VoidlightText $targetPath -AllowMissing) -FileColumn 3 -HeaderCell 'Локация').Rows
$locationRows = New-Object 'System.Collections.Generic.List[object]'

foreach ($file in Get-ChildItem -LiteralPath $locationRoot -File -Filter '*.md') {
    if ($file.Name -like '00_*' -or $file.Name -like '01_*' -or $file.Name -like '02_*') {
        continue
    }

    $text = Read-VoidlightText $file.FullName
    if ((Get-VoidlightMeta -Text $text -Field 'type') -ne 'location') {
        continue
    }

    $relativePath = Get-VoidlightRelativePath -Root $root $file.FullName
    $frontId = Get-VoidlightMeta -Text $text -Field 'front_id'
    if ([string]::IsNullOrWhiteSpace($frontId)) {
        throw "Location card is missing front_id: $relativePath"
    }

    $locationRows.Add([pscustomobject]@{
        Name = Get-VoidlightTitle -Text $text
        Status = Get-VoidlightMeta -Text $text -Field 'status'
        FrontId = $frontId
        File = $relativePath
        MergedInto = Get-VoidlightMeta -Text $text -Field 'merged_into'
        Order = if ($existingRows.ContainsKey($relativePath)) { $existingRows[$relativePath].Order } else { 100000 }
    }) | Out-Null
}

# A merged card is a redirect: it is listed apart with the card it joined.
$mergedRows = @($locationRows | Where-Object { $_.MergedInto })
$liveRows = @($locationRows | Where-Object { -not $_.MergedInto })
$pathById = @{}
foreach ($row in $liveRows) { $pathById[(Get-VoidlightMeta -Text (Read-VoidlightText (Join-Path $root $row.File)) -Field 'id')] = $row.File }

$lines = New-VoidlightGeneratedHeader -Title 'Индекс локаций' -Type 'location_index' -CanonLevel 'active' -Script 'Собрать_индекс_локаций' `
    -Intro 'Этот файл генерируется автоматически из карточек локаций. Поле `front_id` хранится в front matter каждой карточки локации.'
$lines.Add('')
$lines.Add('## Служебные карты и обзоры')
$lines.Add('')
Add-VoidlightMarkdownTable $lines @('Файл', 'Назначение') @(
    ,@('`04_Локации/00_Карта_и_регионы.md`', 'словесная карта и крупные региональные заметки')
    ,@('`04_Локации/01_Регионы_Империи.md`', 'список регионов Империи')
    ,@('`04_Локации/02_Торговые_маршруты_и_пиратские_зоны.md`', 'торговые маршруты и пиратские зоны')
)
$lines.Add('')
$lines.Add('## Локации')
$lines.Add('')
Add-VoidlightMarkdownTable $lines @('Локация', 'Статус', 'FRONT-ID', 'Файл') @(
    $liveRows | Sort-Object Order, Name, File | ForEach-Object { ,@($_.Name, $_.Status, $_.FrontId, "``$($_.File)``") }
) -Escape { param($Value) Format-VoidlightTableCell -Empty '-' $Value }
if ($mergedRows.Count) {
    $lines.Add('')
    $lines.Add('## Объединённые карточки')
    $lines.Add('')
    Add-VoidlightMarkdownTable $lines @('Локация', 'Объединена с', 'Файл') @(
        $mergedRows | Sort-Object Name, File | ForEach-Object {
            $target = if ($pathById.ContainsKey($_.MergedInto)) { "$($_.MergedInto), ``$($pathById[$_.MergedInto])``" } else { $_.MergedInto }
            ,@($_.Name, $target, "``$($_.File)``")
        }
    ) -Escape { param($Value) Format-VoidlightTableCell -Empty '-' $Value }
}

Save-VoidlightIndex -Root $root -Path $targetPath -Lines $lines -Label 'location index' -SkipCheck:$SkipCheck
}
