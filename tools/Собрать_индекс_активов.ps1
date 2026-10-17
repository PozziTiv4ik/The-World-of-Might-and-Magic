param(
    [switch]$SkipCheck
)

$ErrorActionPreference = 'Stop'

[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
$OutputEncoding = [System.Text.UTF8Encoding]::new()

. (Join-Path $PSScriptRoot '_lib.ps1')
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path

Invoke-WmmaToolMain -Root $root -Name $MyInvocation.MyCommand.Name -ScriptBlock {
$assetRoot = Join-Path $root '05_Активы_персонажей'
$targetPath = Join-Path $assetRoot '00_Индекс_активов.md'

# Rows of the previous index keep their order; new cards go to the end.
$existingRows = (Get-WmmaIndexRows -Text (Read-WmmaText $targetPath -AllowMissing) -FileColumn 4 -HeaderCell 'Владелец').Rows
$assetRows = New-Object 'System.Collections.Generic.List[object]'

foreach ($file in Get-ChildItem -LiteralPath $assetRoot -Recurse -File -Filter '*.md') {
    if ($file.Name -eq '00_Индекс_активов.md' -or $file.Name -eq 'README.md') {
        continue
    }

    $text = Read-WmmaText $file.FullName
    if ((Get-WmmaMeta -Text $text -Field 'type') -ne 'character_asset') {
        continue
    }

    $relativePath = Get-WmmaRelativePath -Root $root $file.FullName
    $owner = Get-WmmaMeta -Text $text -Field 'owner'
    $assetKind = Get-WmmaMeta -Text $text -Field 'asset_kind'
    $status = Get-WmmaMeta -Text $text -Field 'status'

    foreach ($required in @(
        [pscustomobject]@{ Name = 'owner'; Value = $owner },
        [pscustomobject]@{ Name = 'asset_kind'; Value = $assetKind },
        [pscustomobject]@{ Name = 'status'; Value = $status }
    )) {
        if ([string]::IsNullOrWhiteSpace($required.Value)) {
            throw "Character asset card is missing $($required.Name): $relativePath"
        }
    }

    $assetRows.Add([pscustomobject]@{
        Owner = $owner
        Asset = Get-WmmaTitle -Text $text
        Kind = $assetKind
        Status = $status
        File = $relativePath
        Order = if ($existingRows.ContainsKey($relativePath)) { $existingRows[$relativePath].Order } else { 100000 }
    }) | Out-Null
}

$lines = New-WmmaGeneratedHeader -Title 'Индекс активов персонажей' -Type 'character_assets_index' -CanonLevel 'active' -Script 'Собрать_индекс_активов' `
    -Intro 'Этот файл генерируется автоматически из карточек активов персонажей. Подробные правила: `00_Инструкции_для_ИИ/05_Активы_персонажей.md`.'
$lines.Add('')
Add-WmmaMarkdownTable $lines @('Владелец', 'Актив', 'Тип', 'Статус', 'Файл') @(
    $assetRows | Sort-Object Order, Owner, Asset, File | ForEach-Object { ,@($_.Owner, $_.Asset, $_.Kind, $_.Status, "``$($_.File)``") }
) -Escape { param($Value) Format-WmmaTableCell -Empty '-' $Value }

Save-WmmaIndex -Root $root -Path $targetPath -Lines $lines -Label 'character asset index' -SkipCheck:$SkipCheck
}
