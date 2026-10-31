param(
    [switch]$SkipCheck
)

$ErrorActionPreference = 'Stop'

[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
$OutputEncoding = [System.Text.UTF8Encoding]::new()

. (Join-Path $PSScriptRoot '_lib.ps1')
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path

Invoke-VoidlightToolMain -Root $root -Name $MyInvocation.MyCommand.Name -ScriptBlock {
$characterRoot = Join-Path $root '03_Персонажи'
$targetPath = Join-Path $characterRoot '00_Индекс_персонажей.md'
$visualSection = 'Визуальные материалы'
$toolGroup = 'Добавлено инструментом'
$mergedGroup = 'Объединённые карточки'

# Sections of the previous index are navigation groups: they keep their order and rows.
$existingIndex = Get-VoidlightIndexRows -Text (Read-VoidlightText $targetPath -AllowMissing) -FileColumn 3 -HeaderCell 'Персонаж'
$existingRows = $existingIndex.Rows
$groupOrder = New-Object 'System.Collections.Generic.List[string]'
foreach ($group in @($existingIndex.Sections) + @($toolGroup)) {
    if ($group -ne $visualSection -and $group -ne $mergedGroup -and $groupOrder -notcontains $group) {
        $groupOrder.Add($group) | Out-Null
    }
}

$characterRows = New-Object 'System.Collections.Generic.List[object]'

foreach ($file in Get-ChildItem -LiteralPath $characterRoot -File -Filter '*.md') {
    if ($file.Name -like '00_*') {
        continue
    }

    $text = Read-VoidlightText $file.FullName
    if ((Get-VoidlightMeta -Text $text -Field 'type') -ne 'character') {
        continue
    }

    $relativePath = Get-VoidlightRelativePath -Root $root $file.FullName
    $existing = $null
    if ($existingRows.ContainsKey($relativePath)) {
        $existing = $existingRows[$relativePath]
    }

    $role = Get-VoidlightMeta -Text $text -Field 'role'
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

    # A merged card is a redirect: it stays in the index (its ID is kept) in its own group.
    $portrait = Get-VoidlightPortraitStatusWord -Status (Get-VoidlightMeta -Text $text -Field 'portrait_status') -Fallback $portraitFallback
    if (Get-VoidlightMeta -Text $text -Field 'merged_into') {
        $group = $mergedGroup
        $portrait = '-'
    }

    if ($groupOrder -notcontains $group) {
        $groupOrder.Add($group) | Out-Null
    }

    $characterRows.Add([pscustomobject]@{
        Name = Get-VoidlightTitle -Text $text
        Role = $role
        Portrait = $portrait
        File = $relativePath
        Group = $group
        Order = $order
    }) | Out-Null
}

$lines = New-VoidlightGeneratedHeader -Title 'Индекс персонажей' -Type 'character_index' -CanonLevel 'active' -Script 'Собрать_индекс_персонажей' `
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
    Add-VoidlightMarkdownTable $lines @('Персонаж', 'Роль', 'Портрет', 'Файл') $rows -Escape { param($Value) Format-VoidlightTableCell -Empty 'Уточнить' $Value }
}

$lines.Add('')
$lines.Add("## $visualSection")
$lines.Add('')
$lines.Add('Индекс портретов: `11_Медиа/Портреты_персонажей/Индекс_портретов.md`.')

Save-VoidlightIndex -Root $root -Path $targetPath -Lines $lines -Label 'character index' -SkipCheck:$SkipCheck
}
