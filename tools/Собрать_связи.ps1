param([switch]$AssignMissingIds,[switch]$SkipCheck)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '_lib.ps1')
$root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Invoke-VoidlightToolMain -Root $root -Name $MyInvocation.MyCommand.Name -ScriptBlock {
    $graph=New-VoidlightGraph -Root $root -AssignMissingIds:$AssignMissingIds
    Write-VoidlightRegistry $root 'Сущности' $graph
    "Entity graph: $($graph.entities.Count) entities, $($graph.edges.Count) relationships; $($graph.references.Count) document mentions kept separately."
    if(-not $SkipCheck){Invoke-VoidlightTool -Root $root -Name 'Проверить_контекст'}
}
