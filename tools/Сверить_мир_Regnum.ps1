param(
    # A world folder; by default every world in 12_Карты (a folder with world.json).
    [string]$World = '',

    [switch]$Json,

    # Returns the result object to a caller instead of printing it.
    [switch]$AsObject
)

$ErrorActionPreference = 'Stop'

[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
$OutputEncoding = [System.Text.UTF8Encoding]::new()

. (Join-Path $PSScriptRoot '_lib.ps1')
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path

# Read-only reconciliation of Regnum worlds with the canon; works without the editor.
Invoke-WmmaToolMain -Root $root -Name $MyInvocation.MyCommand.Name -ScriptBlock {
    $graph = Read-WmmaRegistry $root 'Сущности'
    $result = Test-WmmaRegnumLinks -Root $root -Graph $graph -World $World
    if ($AsObject) { return $result }
    if ($Json) { $result | ConvertTo-Json -Depth 6 }
    else {
        "Regnum links to campaign cards: $($result.links); problems: $(@($result.problems).Count)."
        foreach ($problem in @($result.problems)) { "- $($problem.kind): $($problem.message)" }
        if (-not @($result.problems).Count) { 'Regnum worlds agree with the canon links.' }
    }
    if (@($result.problems | Where-Object { $_.kind -eq 'Error' }).Count) { exit 1 }
}
