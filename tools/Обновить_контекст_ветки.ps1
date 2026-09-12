param([Parameter(Mandatory)][string]$Branch,[string[]]$SceneIds,[string[]]$FactIds,[string[]]$FocusIds,[string]$Situation,[switch]$SkipCheck)
$ErrorActionPreference='Stop';$provided=$PSBoundParameters
. (Join-Path $PSScriptRoot '_lib.ps1')
$root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Invoke-WmmaToolMain -Root $root -Name $MyInvocation.MyCommand.Name -ScriptBlock {
    $path=Join-Path $root '09_Реестры/Контекст.json';$state=Read-WmmaJson $path
    $branchState=@($state.branches|Where-Object {$_.name -eq $Branch -or $_.character_id -eq $Branch})
    if($branchState.Count -ne 1){throw 'Unknown or ambiguous branch.'}
    $entry=$branchState[0]
    $graph=Read-WmmaJson (Join-Path $root '09_Реестры/Сущности.json')
    $knownScenes=@($graph.entities|Where-Object {$_.type -eq 'scene'}|ForEach-Object {$_.id})
    $knownFacts=@((Read-WmmaJson (Join-Path $root '09_Реестры/Знания.json')).facts|ForEach-Object {$_.id})
    if($provided.ContainsKey('SceneIds')){if(-not $SceneIds.Count){throw 'A branch needs at least one scene.'};foreach($id in $SceneIds){if($knownScenes -notcontains $id){throw "Unknown scene: $id"}};$entry.scene_ids=$SceneIds}
    if($provided.ContainsKey('FactIds')){foreach($id in $FactIds){if($knownFacts -notcontains $id){throw "Unknown fact: $id"}};$entry.fact_ids=$FactIds}
    if($provided.ContainsKey('FocusIds')){
        $validFocus=@((Read-WmmaJson (Join-Path $root '09_Реестры/Вопросы.json')).questions|ForEach-Object {$_.id})+@((Read-WmmaJson (Join-Path $root '09_Реестры/Решения.json')).decisions|ForEach-Object {$_.id})+@((Read-WmmaJson (Join-Path $root '09_Реестры/Фронты.json')).fronts|ForEach-Object {$_.id})
        foreach($id in $FocusIds){if($validFocus -notcontains $id){throw "Unknown focus ID: $id"}}
        $entry.focus_ids=$FocusIds
    }
    if($provided.ContainsKey('Situation')){if([string]::IsNullOrWhiteSpace($Situation)){throw 'Situation cannot be empty.'};$entry.situation=$Situation}
    Write-WmmaJson $path $state
    if(-not $SkipCheck){& (Join-Path $root 'tools/Завершить_ход.ps1')}
    "Updated branch context: $($entry.name)"
}
