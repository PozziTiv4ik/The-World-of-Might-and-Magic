param(
    [switch]$SkipCheck
)

$ErrorActionPreference = 'Stop'

[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
$OutputEncoding = [System.Text.UTF8Encoding]::new()

. (Join-Path $PSScriptRoot '_lib.ps1')
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path

Invoke-WmmaToolMain -Root $root -Name $MyInvocation.MyCommand.Name -ScriptBlock {
$locationRoot = Join-Path $root '04_Локации'
$targetPath = Join-Path $locationRoot '00_Индекс_локаций.md'

# Rows of the previous index keep their order; new cards go to the end.
$existingRows = (Get-WmmaIndexRows -Text (Read-WmmaText $targetPath -AllowMissing) -FileColumn 3 -HeaderCell 'Локация').Rows
$locationRows = New-Object 'System.Collections.Generic.List[object]'

foreach ($file in Get-ChildItem -LiteralPath $locationRoot -File -Filter '*.md') {
    if ($file.Name -like '00_*' -or $file.Name -like '01_*' -or $file.Name -like '02_*') {
        continue
    }

    $text = Read-WmmaText $file.FullName
    if ((Get-WmmaMeta -Text $text -Field 'type') -ne 'location') {
        continue
    }

    $relativePath = Get-WmmaRelativePath -Root $root $file.FullName
    $frontId = Get-WmmaMeta -Text $text -Field 'front_id'
    if ([string]::IsNullOrWhiteSpace($frontId)) {
        throw "Location card is missing front_id: $relativePath"
    }

    $locationRows.Add([pscustomobject]@{
        Name = Get-WmmaTitle -Text $text
        Status = Get-WmmaMeta -Text $text -Field 'status'
        FrontId = $frontId
        File = $relativePath
        Order = if ($existingRows.ContainsKey($relativePath)) { $existingRows[$relativePath].Order } else { 100000 }
    }) | Out-Null
}

$lines = New-WmmaGeneratedHeader -Title 'Индекс локаций' -Type 'location_index' -CanonLevel 'active' -Script 'Собрать_индекс_локаций' `
    -Intro 'Этот файл генерируется автоматически из карточек локаций. Поле `front_id` хранится в front matter каждой карточки локации.'
$lines.Add('')
$lines.Add('## Служебные карты и обзоры')
$lines.Add('')
Add-WmmaMarkdownTable $lines @('Файл', 'Назначение') @(
    ,@('`04_Локации/00_Карта_и_регионы.md`', 'словесная карта и крупные региональные заметки')
    ,@('`04_Локации/01_Регионы_Империи.md`', 'список регионов Империи')
    ,@('`04_Локации/02_Торговые_маршруты_и_пиратские_зоны.md`', 'торговые маршруты и пиратские зоны')
)
$lines.Add('')
$lines.Add('## Локации')
$lines.Add('')
Add-WmmaMarkdownTable $lines @('Локация', 'Статус', 'FRONT-ID', 'Файл') @(
    $locationRows | Sort-Object Order, Name, File | ForEach-Object { ,@($_.Name, $_.Status, $_.FrontId, "``$($_.File)``") }
) -Escape { param($Value) Format-WmmaTableCell -Empty '-' $Value }

Save-WmmaIndex -Root $root -Path $targetPath -Lines $lines -Label 'location index' -SkipCheck:$SkipCheck
}
