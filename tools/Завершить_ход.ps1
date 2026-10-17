[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
[Console]::OutputEncoding=[Text.UTF8Encoding]::new()
$OutputEncoding=[Text.UTF8Encoding]::new()
. (Join-Path $PSScriptRoot '_lib.ps1')
$root=(Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path

Invoke-WmmaToolMain -Root $root -Name $MyInvocation.MyCommand.Name -ScriptBlock {
    function Invoke-BuildStep([string]$Script,[hashtable]$Arguments=@{}){
        "`n== $Script =="
        Invoke-WmmaTool -Root $root -Name $Script -Arguments $Arguments
    }

    # Sources -> graph/indices/registry views -> context/memory -> audit -> checks.
    # Each builder runs once. Only disables legacy dependent rebuilds.
    Invoke-BuildStep 'Собрать_связи' @{AssignMissingIds=$true;SkipCheck=$true}
    foreach($script in @('Собрать_индекс_сцен','Собрать_индекс_источников','Собрать_индекс_персонажей','Собрать_индекс_локаций','Собрать_индекс_активов')){
        Invoke-BuildStep $script @{SkipCheck=$true}
    }
    Invoke-BuildStep 'Собрать_решения' @{Only=$true}
    Invoke-BuildStep 'Собрать_вопросы' @{Only=$true}
    Invoke-BuildStep 'Собрать_срезы_реестров' @{SkipCheck=$true}
    Invoke-BuildStep 'Собрать_фронты' @{SkipCheck=$true}

    # No source writes below this point; all readers share this explicit snapshot.
    $data=New-WmmaReadModel $root
    Invoke-BuildStep 'Собрать_панель_хода' @{Data=$data;SkipCheck=$true}
    Invoke-BuildStep 'Собрать_контекст' @{Data=$data;SkipCheck=$true}
    Invoke-BuildStep 'Собрать_память' @{Data=$data}
    Invoke-BuildStep 'Собрать_аудит_данных' @{Data=$data}

    Invoke-BuildStep 'Проверить_архив'
    Invoke-BuildStep 'Проверить_портреты'
    Invoke-BuildStep 'Проверить_проект'
    "`nTurn workspace is ready."
}
