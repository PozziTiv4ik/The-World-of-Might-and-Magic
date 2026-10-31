param(
    [switch]$SkipCheck
)

$ErrorActionPreference = 'Stop'

[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
$OutputEncoding = [System.Text.UTF8Encoding]::new()

. (Join-Path $PSScriptRoot '_lib.ps1')
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path

Invoke-VoidlightToolMain -Root $root -Name $MyInvocation.MyCommand.Name -ScriptBlock {
$assetRoot = Join-Path $root '05_Активы_персонажей'
$targetPath = Join-Path $assetRoot '00_Индекс_активов.md'

# Rows of the previous index keep their order; new cards go to the end.
$existingRows = (Get-VoidlightIndexRows -Text (Read-VoidlightText $targetPath -AllowMissing) -FileColumn 4 -HeaderCell 'Владелец').Rows
$cards = @(Get-VoidlightAssetCards $root)

foreach ($card in $cards) {
    foreach ($required in @('Owner', 'OwnerId', 'Kind', 'Status')) {
        if ([string]::IsNullOrWhiteSpace($card.$required)) {
            $field = @{ Owner = 'owner'; OwnerId = 'owner_id'; Kind = 'asset_kind'; Status = 'status' }[$required]
            throw "Character asset card is missing ${field}: $($card.File)"
        }
    }
}

$lines = New-VoidlightGeneratedHeader -Title 'Индекс активов персонажей' -Type 'character_assets_index' -CanonLevel 'active' -Script 'Собрать_индекс_активов' `
    -Intro 'Этот файл генерируется автоматически из карточек активов персонажей. Подробные правила: `00_Инструкции_для_ИИ/05_Активы_персонажей.md`.'
$lines.Add('')
Add-VoidlightMarkdownTable $lines @('Владелец', 'Актив', 'Тип', 'Статус', 'Файл') @(
    $cards |
        Sort-Object @{ Expression = { if ($existingRows.ContainsKey($_.File)) { $existingRows[$_.File].Order } else { 100000 } } }, Owner, Asset, File |
        ForEach-Object { ,@($_.Owner, $_.Asset, $_.Kind, $_.Status, "``$($_.File)``") }
) -Escape { param($Value) Format-VoidlightTableCell -Empty '-' $Value }

# Owner indexes: the asset table of each owner is generated from the cards with its owner_id;
# the summary and restrictions of an existing owner index are the author's notes and stay.
foreach ($view in @(Get-VoidlightOwnerAssetIndexViews -Root $root -Cards $cards)) {
    Write-VoidlightText (Join-Path $root $view.Path) $view.Text
}

Save-VoidlightIndex -Root $root -Path $targetPath -Lines $lines -Label 'character asset index' -SkipCheck:$SkipCheck
}
