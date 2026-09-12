param(
    [Parameter(Mandatory)][string]$Text,
    [Parameter(Mandatory)][ValidateSet('confirmed','reported','rumor','dream','legend','inference','unknown')][string]$Truth,
    [Parameter(Mandatory)][string[]]$EvidenceIds,
    [string[]]$SubjectIds=@(),[string[]]$KnownTo=@(),
    [ValidateSet('public','restricted','gm')][string]$Visibility='restricted',
    [string]$StoryTime='см. событие-основание',[string]$ReportedBy='',
    [switch]$SkipCheck
)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '_lib.ps1')
$root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Invoke-WmmaToolMain -Root $root -Name $MyInvocation.MyCommand.Name -ScriptBlock {
    $graph=Read-WmmaJson (Join-Path $root '09_Реестры/Сущности.json');$byId=@{}
    foreach($e in $graph.entities){$byId[$e.id]=$e}
    foreach($id in @($EvidenceIds)+@($SubjectIds)+@($KnownTo)+@($ReportedBy)|Where-Object {$_}){if(-not $byId.ContainsKey($id)){throw "Unknown evidence/entity ID: $id"}}
    foreach($id in $KnownTo){if($byId[$id].type -ne 'character'){throw "Knowledge holder must be a character: $id"}}
    $path=Join-Path $root '09_Реестры/Знания.json';$registry=Read-WmmaJson $path
    $max=0;foreach($fact in $registry.facts){if($fact.id -match '^FACT-(\d+)$'){$max=[Math]::Max($max,[int]$Matches[1])}}
    $id='FACT-{0:D3}' -f ($max+1)
    $registry.facts=@($registry.facts)+@([pscustomobject]@{id=$id;text=$Text;truth=$Truth;evidence_ids=$EvidenceIds;subject_ids=$SubjectIds;known_to=$KnownTo;visibility=$Visibility;story_time=$StoryTime;reported_by=$(if($ReportedBy){$ReportedBy}else{$null});recorded_on=(Get-Date -Format 'yyyy-MM-dd')})
    Write-WmmaJson $path $registry
    if(-not $SkipCheck){& (Join-Path $root 'tools/Завершить_ход.ps1')}
    "Created knowledge record: $id"
}
