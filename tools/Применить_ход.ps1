param([Parameter(Mandatory)][string]$Id)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '_lib.ps1')
$root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Invoke-VoidlightToolMain -Root $root -Name $MyInvocation.MyCommand.Name -ScriptBlock {
    Apply-VoidlightTurn -Root $root -Id $Id | Select-Object id,status | ConvertTo-Json
}
