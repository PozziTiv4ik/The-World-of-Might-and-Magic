param(
    [Parameter(Mandatory = $true)]
    [string]$FactId,

    # The newer fact (create it first with Новое_знание or use Новое_знание -Supersedes).
    [Parameter(Mandatory = $true)]
    [Alias('By')]
    [string]$ReplacedBy,

    [string]$Note = '',

    [switch]$SkipCheck
)

$ErrorActionPreference = 'Stop'

[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
$OutputEncoding = [System.Text.UTF8Encoding]::new()

. (Join-Path $PSScriptRoot '_lib.ps1')
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path

# Marks a fact as replaced (superseded_by): the old record keeps its text, evidence and
# knowers as history, context packets stop showing it, and the context pointers move on.
Invoke-VoidlightToolMain -Root $root -Name $MyInvocation.MyCommand.Name -ScriptBlock {
    Assert-VoidlightSingleLine $Note 'Note' -AllowEmpty
    $result = Set-VoidlightFactSuperseded -Root $root -FactId $FactId -ReplacedBy $ReplacedBy -Note $Note
    if ($result.changed) { Complete-VoidlightMutation -Root $root -SkipCheck:$SkipCheck }
    "Marked fact as replaced: $FactId -> $ReplacedBy; context pointers moved: $($result.moved_pointers)."
}
