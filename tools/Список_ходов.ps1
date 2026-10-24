param(
    # One turn; without it the list covers every journal in .wmma/turns.
    [string]$Id = '',

    # Deletes the copies of finished turns (committed or restored).
    [switch]$Clean,

    # Deletes the copy of one unapplied turn (copying, editing or prepared); needs -Id.
    [switch]$Discard,

    [switch]$Json
)

$ErrorActionPreference = 'Stop'

[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
$OutputEncoding = [System.Text.UTF8Encoding]::new()

. (Join-Path $PSScriptRoot '_lib.ps1')
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path

# Turn journals with status, dates and the size of each copy. A half-applied or
# half-restored turn is never deleted: finish it with Восстановить_ход first.
Invoke-WmmaToolMain -Root $root -Name $MyInvocation.MyCommand.Name -ScriptBlock {
    if ($Clean -and $Discard) { throw 'Use either -Clean or -Discard.' }
    if ($Discard) {
        if (-not $Id) { throw 'Discard needs -Id of an unapplied turn.' }
        $status = Remove-WmmaTurn -Root $root -Id $Id -Unapplied
        "Discarded the copy of unapplied turn $Id ($status). The project files were not changed."
        return
    }
    if ($Clean) {
        $removed = @(Remove-WmmaFinishedTurns -Root $root -Id $Id)
        if (-not $removed.Count) { 'No finished turn copies to remove.' }
        foreach ($turn in $removed) { "Removed finished turn copy: $($turn.id) ($($turn.status), $($turn.size_mb) MB)" }
        return
    }
    $turns = @(Get-WmmaTurns -Root $root -Size | Where-Object { -not $Id -or $_.id -eq $Id })
    if ($Json) { ConvertTo-Json -InputObject @($turns) -Depth 4; return }
    if (-not $turns.Count) { 'No turn journals in .wmma/turns.'; return }
    ($turns | Format-Table id, status, started, updated, size_mb, title -AutoSize | Out-String -Width 220).TrimEnd()
    foreach ($problem in @(Get-WmmaTurnProblems -Root $root | Where-Object { -not $Id -or $_.message.Contains($Id) })) {
        "$($problem.kind): $($problem.message)"
    }
}
