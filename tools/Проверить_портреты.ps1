$ErrorActionPreference = 'Stop'

[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
$OutputEncoding = [System.Text.UTF8Encoding]::new()

. (Join-Path $PSScriptRoot '_lib.ps1')
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
Invoke-WmmaToolMain -Root $root -Name $MyInvocation.MyCommand.Name -ScriptBlock {
$characterRoot = Join-Path $root '03_Персонажи'
$portraitRoot = Join-Path $root '11_Медиа\Портреты_персонажей'
$problems = New-WmmaProblemSet
$missing = New-Object 'System.Collections.Generic.List[string]'
$wrongRatio = New-Object 'System.Collections.Generic.List[string]'
$available = 0
$planned = 0

$characterFiles = @(
    Get-ChildItem -LiteralPath $characterRoot -File -Filter '*.md' |
        Where-Object { $_.Name -ne '00_Индекс_персонажей.md' -and $_.Name -ne '00_Словарь_имен_и_алиасов.md' }
)

foreach ($file in $characterFiles) {
    $text = Read-WmmaText $file.FullName
    if ($text -notmatch '(?m)^type:\s*character\s*$') {
        continue
    }

    $relativeFile = Get-WmmaRelativePath $root $file.FullName
    # A merged card is a redirect; the portrait belongs to the main card.
    if (Get-WmmaMeta -Text $text -Field 'merged_into') {
        continue
    }

    if ($text -notmatch '(?m)^portrait_status:\s*(available|missing|planned)\s*$') {
        Add-Problem Error "Invalid or missing portrait_status: $relativeFile"
        continue
    }

    $status = $Matches[1]
    switch ($status) {
        'available' { $available++ }
        'planned' { $planned++ }
        'missing' { $missing.Add($file.BaseName) | Out-Null }
    }

    if ($status -ne 'available') {
        continue
    }

    if ($text -notmatch '(?m)^portrait:\s*(.+)\s*$') {
        Add-Problem Error "Missing portrait path: $relativeFile"
        continue
    }

    # The portrait path is resolved from the project root only; an unreadable image is an error here.
    $portrait = Test-WmmaPortraitFile -Root $root -Portrait $Matches[1].Trim()
    switch ($portrait.Problem) {
        'missing' { Add-Problem Error "portrait_status=available but file is missing: $relativeFile" }
        'unreadable' { Add-Problem Error "Cannot read portrait dimensions: $(Get-WmmaRelativePath $root $portrait.Path)" }
        'ratio' { $wrongRatio.Add("$(Get-WmmaRelativePath $root $portrait.Path) ($($portrait.Size.Width)x$($portrait.Size.Height))") | Out-Null }
    }
}

$portraitFoldersWithImages = @(Get-WmmaPortraitImageFolders $portraitRoot)
$foldersWithoutPrompt = @(
    $portraitFoldersWithImages |
        Where-Object { -not (Test-Path -LiteralPath (Join-Path $_.FullName 'Промпт_портрета.md')) } |
        ForEach-Object { Get-WmmaRelativePath $root $_.FullName }
)

[pscustomobject]@{
    Characters = $characterFiles.Count
    PortraitAvailable = $available
    PortraitPlanned = $planned
    PortraitMissing = $missing.Count
    PortraitFoldersWithImages = $portraitFoldersWithImages.Count
    FoldersWithoutPrompt = $foldersWithoutPrompt.Count
    WrongAspectRatio = $wrongRatio.Count
    Errors = $problems.Errors.Count
} | Format-List

if ($missing.Count -gt 0) {
    "`nMissing portraits:"
    $missing | Sort-Object | ForEach-Object { "- $_" }
}

if ($foldersWithoutPrompt.Count -gt 0) {
    "`nPortrait folders without prompt:"
    $foldersWithoutPrompt | Sort-Object | ForEach-Object { "- $_" }
}

if ($wrongRatio.Count -gt 0) {
    "`nPortraits with invalid aspect ratio:"
    $wrongRatio | Sort-Object | ForEach-Object { "- $_" }
    foreach ($item in $wrongRatio) {
        Add-Problem Error "Invalid portrait aspect ratio: $item"
    }
}

Complete-WmmaCheck
}
