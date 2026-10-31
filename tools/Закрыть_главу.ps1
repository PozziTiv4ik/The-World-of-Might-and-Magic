param(
    [Parameter(Mandatory = $true)]
    [int]$Chapter,

    [Parameter(Mandatory = $true)]
    [string]$NextTitle,

    [Parameter(Mandatory = $true)]
    [string]$NextDateInStory,

    [switch]$SkipCheck
)

$ErrorActionPreference = 'Stop'

[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
$OutputEncoding = [System.Text.UTF8Encoding]::new()

. (Join-Path $PSScriptRoot '_lib.ps1')
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path

# Closes the active chapter after its summary is written to the archive: the chapter and
# its scenes become closed, the next chapter opens from the template, the chapter pointers
# move, and the chapter, its archive copy and its scenes join the protected canon.
Invoke-VoidlightToolMain -Root $root -Name $MyInvocation.MyCommand.Name -ScriptBlock {
    Assert-VoidlightSingleLine $NextTitle 'NextTitle'
    Assert-VoidlightSingleLine $NextDateInStory 'NextDateInStory'
    $today = Get-Date -Format 'yyyy-MM-dd'
    $current = Get-VoidlightCurrentChapter $root
    if ($Chapter -ne $current) { throw "Only the active chapter $current can be closed." }

    $chapterFiles = @(Get-ChildItem -LiteralPath (Join-Path $root '01_Кампания/Главы') -File -Filter '*.md' | Where-Object {
        (Get-VoidlightMeta (Read-VoidlightText $_.FullName) 'chapter') -eq [string]$Chapter
    })
    if ($chapterFiles.Count -ne 1) { throw "Expected one chapter file for chapter $Chapter, found $($chapterFiles.Count)." }
    $chapterFile = $chapterFiles[0]
    $chapterRelative = Get-VoidlightRelativePath $root $chapterFile.FullName
    $archiveRelative = '06_Архив_канона/Завершенные_главы/' + $chapterFile.Name
    $archivePath = Join-Path $root $archiveRelative
    if (-not (Test-Path -LiteralPath $archivePath)) {
        throw "Write the chapter summary to $archiveRelative first (type: archived_chapter, source_chapter: $chapterRelative)."
    }
    $archive = Read-VoidlightText $archivePath
    if ((Get-VoidlightMeta $archive 'type') -ne 'archived_chapter') { throw "The archive copy needs type: archived_chapter: $archiveRelative" }

    $nextNumber = $Chapter + 1
    $nextRelative = '01_Кампания/Главы/{0}' -f ('Глава_{0:D2}_{1}.md' -f $nextNumber, (Convert-VoidlightFileName $NextTitle))
    if (Test-Path -LiteralPath (Join-Path $root $nextRelative)) { throw "Next chapter file already exists: $nextRelative" }

    $scenes = @(Get-ChildItem -LiteralPath (Join-Path $root '01_Кампания/Ветки') -Recurse -File -Filter '*.md' | ForEach-Object {
        $text = Read-VoidlightText $_.FullName
        if ((Get-VoidlightMeta $text 'type') -eq 'scene' -and (Get-VoidlightMeta $text 'chapter') -eq [string]$Chapter) {
            [pscustomobject]@{ FullName = $_.FullName; Path = (Get-VoidlightRelativePath $root $_.FullName); Text = $text; Status = (Get-VoidlightMeta $text 'status') }
        }
    })
    $drafts = @($scenes | Where-Object { $_.Status -notin @('active', 'closed') })
    if ($drafts.Count) {
        throw ("Finish or move the draft scenes before closing chapter ${Chapter}:`n" + (($drafts | ForEach-Object { "- $($_.Path) ($($_.Status))" }) -join "`n"))
    }

    # Scenes of a closed chapter are closed; a pending decision stays open in its registry.
    foreach ($scene in @($scenes | Where-Object { $_.Status -ne 'closed' })) {
        Write-VoidlightText $scene.FullName (Set-VoidlightMeta $scene.Text 'status' 'closed')
    }
    $chapterText = Read-VoidlightText $chapterFile.FullName
    $chapterText = Set-VoidlightMeta $chapterText 'status' 'closed'
    $chapterText = Set-VoidlightMeta $chapterText 'closed_real_date' $today
    Write-VoidlightText $chapterFile.FullName $chapterText
    $archive = Set-VoidlightMeta $archive 'status' 'closed'
    if (-not (Get-VoidlightMeta $archive 'closed_real_date')) { $archive = Set-VoidlightMeta $archive 'closed_real_date' $today }
    if (-not (Get-VoidlightMeta $archive 'source_chapter')) { $archive = Set-VoidlightMeta $archive 'source_chapter' $chapterRelative }
    Write-VoidlightText $archivePath $archive

    # The next chapter starts from the template; its story is written from the author's text.
    $template = Read-VoidlightText (Join-Path $root '09_Шаблоны/Шаблон_главы.md')
    $heading = "# Глава $nextNumber. $NextTitle"
    $next = [regex]::Replace($template, '\A(?:﻿)?# [^\r\n]*', [System.Text.RegularExpressions.MatchEvaluator] { param($match) $heading })
    foreach ($pair in @(@('chapter', [string]$nextNumber), @('status', 'active'), @('date_in_story', $NextDateInStory), @('opened_real_date', $today))) {
        $next = Set-VoidlightMeta $next $pair[0] $pair[1]
    }
    Write-VoidlightText (Join-Path $root $nextRelative) $next

    $state = Read-VoidlightRegistry $root 'Контекст'
    $state.chapter_file = $nextRelative
    Write-VoidlightRegistry $root 'Контекст' $state
    foreach ($name in @('Вопросы', 'Фронты')) {
        $registry = Read-VoidlightRegistry $root $name
        $registry.current_chapter = $nextNumber
        $registry.updated_real_date = $today
        Write-VoidlightRegistry $root $name $registry
    }
    $activePath = Join-Path $root '01_Кампания/01_Активная_глава.md'
    if (Test-Path -LiteralPath $activePath) {
        $active = Read-VoidlightText $activePath
        foreach ($pair in @(@('chapter', [string]$nextNumber), @('last_closed_chapter', [string]$Chapter), @('title', $NextTitle), @('date_in_story', $NextDateInStory), @('opened_real_date', $today))) {
            $active = Set-VoidlightMeta $active $pair[0] $pair[1]
        }
        Write-VoidlightText $activePath $active
    }

    $protectedPaths = @($chapterRelative, $archiveRelative) + @($scenes | ForEach-Object { $_.Path })
    [void](Update-VoidlightProtectedFiles -Root $root -Paths $protectedPaths -Reason "Закрытие главы $Chapter" -Add)

    Complete-VoidlightMutation -Root $root -Rebuild @('Собрать_индекс_сцен', 'Собрать_вопросы', 'Собрать_фронты') -SkipCheck:$SkipCheck

    "Closed chapter ${Chapter}: $chapterRelative; $($scenes.Count) scenes closed and protected."
    "Opened chapter ${nextNumber}: $nextRelative"
    'Update the prose of 01_Кампания/01_Активная_глава.md and the new chapter from the author''s text.'
}
