$ErrorActionPreference = 'Stop'

[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
$OutputEncoding = [System.Text.UTF8Encoding]::new()

. (Join-Path $PSScriptRoot '_lib.ps1')
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
Invoke-VoidlightToolMain -Root $root -Name $MyInvocation.MyCommand.Name -ScriptBlock {
$archiveRoot = Join-Path $root '06_Архив_канона'
$problems = New-VoidlightProblemSet
$archiveFiles = @()

if (-not (Test-Path -LiteralPath $archiveRoot)) {
    Add-Problem Error 'Archive folder is missing: 06_Архив_канона'
} else {
    $archiveFiles = @(Get-ChildItem -LiteralPath $archiveRoot -Recurse -File -Filter '*.md')
    $legacyReferences = @(Get-VoidlightLegacyReferences)

    foreach ($file in $archiveFiles) {
        $text = Read-VoidlightText $file.FullName
        $relative = Get-VoidlightRelativePath $root $file.FullName

        foreach ($forbiddenReference in $legacyReferences) {
            if ($text.Contains($forbiddenReference)) {
                Add-Problem Error "Archive file contains legacy reference: ${relative} -> $forbiddenReference"
            }
        }

        if ($relative -like '*Завершенные_главы*') {
            if ($text -notmatch '(?m)^type:\s*archived_chapter\s*$') {
                Add-Problem Warning "Archived chapter has no type=archived_chapter: $relative"
            }

            if ($text -notmatch '(?m)^status:\s*closed\s*$') {
                Add-Problem Warning "Archived chapter should use status=closed: $relative"
            }

            if ($text -match '(?m)^status:\s*active\s*$') {
                Add-Problem Error "Archived chapter must not be active: $relative"
            }
        }
    }
}

Complete-VoidlightCheck ([pscustomobject]@{
    ArchiveFiles = $archiveFiles.Count
    Errors = $problems.Errors.Count
    Warnings = $problems.Warnings.Count
}) 'Archive check completed successfully.'
}
