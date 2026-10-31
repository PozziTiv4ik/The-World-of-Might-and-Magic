param([Parameter(Mandatory)][string]$Id,[Parameter(Mandatory)][string]$Title)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '_lib.ps1')
$root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Invoke-VoidlightToolMain -Root $root -Name $MyInvocation.MyCommand.Name -ScriptBlock {
    $result=Start-VoidlightTurn -Root $root -Id $Id -Title $Title
    [pscustomobject]@{id=$Id;status=$result.status;workspace=(Join-Path (Get-VoidlightTurnPath $root $Id) 'stage')} | ConvertTo-Json
}
