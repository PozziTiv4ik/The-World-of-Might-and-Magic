param(
    [switch]$SkipCheck
)

$ErrorActionPreference = 'Stop'

[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
$OutputEncoding = [System.Text.UTF8Encoding]::new()

. (Join-Path $PSScriptRoot '_lib.ps1')
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path

Invoke-WmmaToolMain -Root $root -Name $MyInvocation.MyCommand.Name -ScriptBlock {
$characterRoot = Join-Path $root '03_Персонажи'
$targetPath = Join-Path $characterRoot '00_Индекс_персонажей.md'
$visualSection = 'Визуальные материалы'
$toolGroup = 'Добавлено инструментом'

# Sections of the previous index are navigation groups: they keep their order and rows.
$existingIndex = Get-WmmaIndexRows -Text (Read-WmmaText $targetPath -AllowMissing) -FileColumn 3 -HeaderCell 'Персонаж'
$existingRows = $existingIndex.Rows
$groupOrder = New-Object 'System.Collections.Generic.List[string]'
foreach ($group in @($existingIndex.Sections) + @($toolGroup)) {
    if ($group -ne $visualSection -and $groupOrder -notcontains $group) {
        $groupOrder.Add($group) | Out-Null
    }
}

$characterRows = New-Object 'System.Collections.Generic.List[object]'

foreach ($file in Get-ChildItem -LiteralPath $characterRoot -File -Filter '*.md') {
    if ($file.Name -like '00_*') {
        continue
    }

    $text = Read-WmmaText $file.FullName
    if ((Get-WmmaMeta -Text $text -Field 'type') -ne 'character') {
        continue
    }

    $relativePath = Get-WmmaRelativePath -Root $root $file.FullName
    $existing = $null
    if ($existingRows.ContainsKey($relativePath)) {
        $existing = $existingRows[$relativePath]
    }

    $role = Get-WmmaMeta -Text $text -Field 'role'
    if (
        ([string]::IsNullOrWhiteSpace($role) -or $role -match '^(уточнить|Уточнить\.?)$') -and
        $existing -and
        -not [string]::IsNullOrWhiteSpace($existing.Cells[1])
    ) {
        $role = $existing.Cells[1]
    }
    if ([string]::IsNullOrWhiteSpace($role)) {
        $role = 'Уточнить.'
    }

    $group = $toolGroup
    $order = 100000
    $portraitFallback = $null
    if ($existing) {
        if (-not [string]::IsNullOrWhiteSpace($existing.Section) -and $existing.Section -ne $visualSection) {
            $group = $existing.Section
        }
        $order = $existing.Order
        $portraitFallback = $existing.Cells[2]
    }

    if ($groupOrder -notcontains $group) {
        $groupOrder.Add($group) | Out-Null
    }

    $characterRows.Add([pscustomobject]@{
        Name = Get-WmmaTitle -Text $text
        Role = $role
        Portrait = Get-WmmaPortraitStatusWord -Status (Get-WmmaMeta -Text $text -Field 'portrait_status') -Fallback $portraitFallback
        File = $relativePath
        Group = $group
        Order = $order
    }) | Out-Null
}

$lines = New-WmmaGeneratedHeader -Title 'Индекс персонажей' -Type 'character_index' -CanonLevel 'active' -Script 'Собрать_индекс_персонажей' `
    -Intro "Этот файл генерируется автоматически из карточек персонажей. Текущие разделы сохраняются как навигационные группы; новые карточки без прежней строки попадают в ``$toolGroup``."

foreach ($group in $groupOrder) {
    $rows = @(
        $characterRows |
            Where-Object { $_.Group -eq $group } |
            Sort-Object Order, Name, File |
            ForEach-Object { ,@($_.Name, $_.Role, $_.Portrait, "``$($_.File)``") }
    )

    if ($rows.Count -eq 0) {
        continue
    }

    $lines.Add('')
    $lines.Add("## $group")
    $lines.Add('')
    Add-WmmaMarkdownTable $lines @('Персонаж', 'Роль', 'Портрет', 'Файл') $rows -Escape { param($Value) Format-WmmaTableCell -Empty 'Уточнить' $Value }
}

$lines.Add('')
$lines.Add("## $visualSection")
$lines.Add('')
$lines.Add('Индекс портретов: `11_Медиа/Портреты_персонажей/Индекс_портретов.md`.')

Save-WmmaIndex -Root $root -Path $targetPath -Lines $lines -Label 'character index' -SkipCheck:$SkipCheck
}
