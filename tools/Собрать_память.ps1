param([object]$Data)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '_lib.ps1')
$root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Invoke-VoidlightToolMain -Root $root -Name $MyInvocation.MyCommand.Name -ScriptBlock {
    $Data=Resolve-VoidlightReadModel $root $Data
    $memory=New-VoidlightCharacterMemory $root -Data $Data
    $cards=$memory.characters
    Write-VoidlightJson (Join-Path $root '09_Реестры/Память_персонажей.json') $memory
    $questions=$Data.questions
    $review=@('# Очередь вопросов','','---','type: backlog_review','status: active','canon_level: support','generated_by: tools/Собрать_память.ps1','---','','Очередь внимания не закрывает вопросы и не переписывает их исходный статус. Текущая связь важнее возраста ID.','','| ID | Статус | Внимание | Основание | Условие продолжения |','| --- | --- | --- | --- | --- |')
    foreach($q in $questions.questions|Where-Object {$_.status -ne 'resolved'}){
        $review+=('| '+((@($q.id,$q.status,$q.attention,$q.attention_reason,$q.waiting_for)|ForEach-Object {Convert-VoidlightCell $_}) -join ' | ')+' |')
    }
    Write-VoidlightText (Join-Path $root '10_Обслуживание/Очередь_вопросов.md') (($review -join "`n")+"`n")
    "Character memory: $($cards.Count) cards; unestablished voices remain explicit."
}
