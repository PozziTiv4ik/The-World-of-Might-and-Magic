# resume finishes an interrupted application, restore returns the turn's own changes, and
# rebase moves an unapplied turn (editing, prepared) onto outside edits of other files.
param([Parameter(Mandatory)][string]$Id,[ValidateSet('resume','restore','rebase')][string]$Mode='resume')
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '_lib.ps1')
$root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Invoke-WmmaToolMain -Root $root -Name $MyInvocation.MyCommand.Name -ScriptBlock {
    switch($Mode){
        'restore' {Undo-WmmaTurn -Root $root -Id $Id | Select-Object id,status | ConvertTo-Json}
        'rebase' {
            $result=Rebase-WmmaTurn -Root $root -Id $Id
            [pscustomobject]@{id=$result.id;status=$result.status;rebased_files=@($result.rebased_files);next='Подготовить_ход'} | ConvertTo-Json
        }
        default {Apply-WmmaTurn -Root $root -Id $Id | Select-Object id,status | ConvertTo-Json}
    }
}
