param()
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '_lib.ps1')
$root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Invoke-WmmaToolMain -Root $root -Name $MyInvocation.MyCommand.Name -ScriptBlock {
    $graph=Read-WmmaJson (Join-Path $root '09_Реестры/Сущности.json')
    $knowledge=Read-WmmaJson (Join-Path $root '09_Реестры/Знания.json')
    $cards=@()
    foreach($person in $graph.entities | Where-Object {$_.type -eq 'character'}){
        $text=Read-WmmaText (Join-Path $root $person.path)
        $traits=@();foreach($heading in @('Характер','Известные черты','Цели','Биография и образ')){
            $body=Get-WmmaSection $text $heading
            if($body){$traits+=[ordered]@{section=$heading;text=$body;evidence=$person.path}}
        }
        $scenes=@($graph.entities|Where-Object {$_.type -eq 'scene' -and $_.participant_ids -contains $person.id}|ForEach-Object {$_.id})
        $known=@($knowledge.facts|Where-Object {$_.known_to -contains $person.id -or $_.visibility -eq 'public'}|ForEach-Object {$_.id})
        $voice=Get-WmmaSection $text 'Манера речи'
        $cards+=[ordered]@{character_id=$person.id;name=$person.name;card=$person.path;traits=$traits;voice=[ordered]@{status=$(if($voice){'recorded'}else{'not_established'});text=$voice};known_fact_ids=$known;scene_reference_ids=$scenes;scene_reference_note='Упоминание в участниках может означать докладчика или отсутствующее лицо; личное присутствие и воспоминание проверяются по сцене.';relationships=(Get-WmmaSection $text 'Связи')}
    }
    Write-WmmaJson (Join-Path $root '09_Реестры/Память_персонажей.json') ([ordered]@{schema_version=1;type='character_memory_view';generated_by='tools/Собрать_память.ps1';characters=$cards})
    $questions=Read-WmmaJson (Join-Path $root '09_Реестры/Вопросы.json')
    $review=@('# Очередь вопросов','','---','type: backlog_review','status: active','canon_level: support','generated_by: tools/Собрать_память.ps1','---','','Очередь внимания не закрывает вопросы и не переписывает их исходный статус. Текущая связь важнее возраста ID.','','| ID | Статус | Внимание | Основание | Условие продолжения |','| --- | --- | --- | --- | --- |')
    foreach($q in $questions.questions|Where-Object {$_.status -ne 'resolved'}){
        $review+=('| '+((@($q.id,$q.status,$q.attention,$q.attention_reason,$q.waiting_for)|ForEach-Object {Convert-WmmaCell $_}) -join ' | ')+' |')
    }
    Write-WmmaText (Join-Path $root '10_Обслуживание/Очередь_вопросов.md') (($review -join "`n")+"`n")
    "Character memory: $($cards.Count) cards; unestablished voices remain explicit."
}
