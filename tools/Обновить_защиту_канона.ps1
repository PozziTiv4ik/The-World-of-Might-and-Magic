param(
    # Project-relative paths of protected files (closed chapters, their archive copies and scenes).
    [Parameter(Mandatory = $true)]
    [string[]]$Path,

    # Why the bytes changed: a typo, formatting, a merge of duplicate IDs or an accepted retcon (its ID).
    [Parameter(Mandatory = $true)]
    [string]$Reason,

    # Protects a file that is not in the manifest yet.
    [switch]$Add,

    [switch]$SkipCheck
)

$ErrorActionPreference = 'Stop'

[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
$OutputEncoding = [System.Text.UTF8Encoding]::new()

. (Join-Path $PSScriptRoot '_lib.ps1')
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path

# The only way to accept new bytes of closed canon: the manifest records the new hash with
# the date, the previous hash and the reason (amendments of 10_Обслуживание/Миграция_v2.json).
Invoke-VoidlightToolMain -Root $root -Name $MyInvocation.MyCommand.Name -ScriptBlock {
    Assert-VoidlightSingleLine $Reason 'Reason'
    $count = Update-VoidlightProtectedFiles -Root $root -Paths $Path -Reason $Reason -Add:$Add
    if ($count) { Complete-VoidlightMutation -Root $root -SkipCheck:$SkipCheck }
    if ($count) { "Protected canon updated: $count file(s); the reason is recorded in 10_Обслуживание/Миграция_v2.json." }
    else { 'Protected canon already matches these files.' }
}
