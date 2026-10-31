param(
    [switch]$SkipCheck
)

$ErrorActionPreference = 'Stop'

[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
$OutputEncoding = [System.Text.UTF8Encoding]::new()

. (Join-Path $PSScriptRoot '_lib.ps1')
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
Invoke-VoidlightToolMain -Root $root -Name $MyInvocation.MyCommand.Name -ScriptBlock {
$branchesRoot = Join-Path $root '01_Кампания\Ветки'
$archiveRoot = Join-Path $root '06_Архив_канона'
$targetPath = Join-Path $root '01_Кампания\00_Индекс_сцен.md'

$sceneRows = New-Object 'System.Collections.Generic.List[object]'

foreach ($file in Get-ChildItem -LiteralPath $branchesRoot -Recurse -File -Filter '*.md') {
    $text = Read-VoidlightText $file.FullName
    if ($text -notmatch '(?m)^type:\s*scene\s*$') {
        continue
    }

    $branch = Get-VoidlightMeta -Default '-' -Text $text -Field 'branch'
    if ($branch -eq '-') {
        $branch = Split-Path -Leaf (Split-Path -Parent $file.FullName)
    }

    $sceneRows.Add([pscustomobject]@{
        Branch = $branch
        Chapter = Get-VoidlightMeta -Default '-' -Text $text -Field 'chapter'
        Status = Get-VoidlightMeta -Default '-' -Text $text -Field 'status'
        FrontId = Get-VoidlightMeta -Default '-' -Text $text -Field 'front_id'
        Title = Get-VoidlightTitle -Text $text
        File = Get-VoidlightRelativePath -Root $root $file.FullName
    }) | Out-Null
}

function ConvertTo-SceneCells([object]$Row) {
    ,@($Row.Branch, $Row.Chapter, $Row.Status, $Row.FrontId, $Row.Title, "``$($Row.File)``")
}

$activeRows = @(
    $sceneRows |
        Where-Object { $_.Status -notin @('closed', 'archived') } |
        Sort-Object Branch, Chapter, File |
        ForEach-Object { ConvertTo-SceneCells $_ }
)
if ($activeRows.Count -eq 0) {
    $activeRows = @(,@('-', '-', '-', '-', 'Активных сцен не найдено', '-'))
}

$allRows = @($sceneRows | Sort-Object Branch, Chapter, File | ForEach-Object { ConvertTo-SceneCells $_ })
$archiveRows = @()
if (Test-Path -LiteralPath $archiveRoot) {
    $archiveRows = @(
        Get-ChildItem -LiteralPath $archiveRoot -Recurse -File -Filter '*.md' |
            ForEach-Object {
                $text = Read-VoidlightText $_.FullName
                if ($text -match '(?m)^type:\s*archived_chapter\s*$') {
                    [pscustomobject]@{
                        Title = Get-VoidlightTitle -Text $text
                        Status = Get-VoidlightMeta -Default '-' -Text $text -Field 'status'
                        File = Get-VoidlightRelativePath -Root $root $_.FullName
                    }
                }
            } |
            Sort-Object File |
            ForEach-Object { ,@($_.Title, $_.Status, "``$($_.File)``") }
    )
}

$sceneHeader = @('Ветка', 'Глава', 'Статус', 'FRONT-ID', 'Сцена', 'Файл')
$lines = New-VoidlightGeneratedHeader -Title 'Индекс сцен' -Type 'scene_index' -CanonLevel 'support' -Script 'Собрать_индекс_сцен' `
    -Intro 'Этот файл генерируется автоматически. Для пересборки используй `.\tools\Собрать_индекс_сцен.ps1`.'
$lines.Add('')
$lines.Add('## Активные сцены')
$lines.Add('')
Add-VoidlightMarkdownTable $lines $sceneHeader $activeRows
$lines.Add('')
$lines.Add('## Все сцены по веткам')
$lines.Add('')
Add-VoidlightMarkdownTable $lines $sceneHeader $allRows
$lines.Add('')
$lines.Add('## Архивные главы')
$lines.Add('')
Add-VoidlightMarkdownTable $lines @('Глава', 'Статус', 'Файл') $archiveRows

Save-VoidlightIndex -Root $root -Path $targetPath -Lines $lines -Label 'scene index' -SkipCheck:$SkipCheck
}
