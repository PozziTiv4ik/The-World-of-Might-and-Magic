param(
    [Parameter(Mandatory)][string]$Text,
    [Parameter(Mandatory)][ValidateSet('confirmed','reported','rumor','dream','legend','inference','unknown')][string]$Truth,
    [Parameter(Mandatory)][string[]]$EvidenceIds,
    [string[]]$SubjectIds=@(),[string[]]$KnownTo=@(),
    # One basis per knower: 'CHAR-…|SCENE-…,SRC-…|почему он это знает' or @{character_id=…;evidence_ids=@(…);basis=…}.
    [object[]]$KnowledgeEvidence=@(),
    [ValidateSet('public','restricted','gm')][string]$Visibility='restricted',
    [string]$StoryTime='см. событие-основание',[string]$ReportedBy='',
    # The older fact this record replaces (see Заменить_факт).
    [string]$Supersedes='',
    [switch]$SkipCheck
)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '_lib.ps1')
$root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Invoke-WmmaToolMain -Root $root -Name $MyInvocation.MyCommand.Name -ScriptBlock {
    $data=New-WmmaReadModel $root
    $graph=$data.graph
    if([string]::IsNullOrWhiteSpace($Text) -or @($EvidenceIds|Where-Object {$_}).Count -eq 0){throw 'Knowledge text and at least one evidence ID are required.'}
    Assert-WmmaEntityIds $graph @($EvidenceIds|Where-Object {$_}) -Label 'evidence'
    Assert-WmmaEntityIds $graph @($SubjectIds|Where-Object {$_}) -Label 'subject'
    if($ReportedBy){Assert-WmmaEntityIds $graph @($ReportedBy) -Type @('character') -Label 'reporting character'}
    # Every knower needs a basis of their own: shared participation does not prove that a
    # character learned every episode.
    $proof=[ordered]@{}
    foreach($entry in @($KnowledgeEvidence|Where-Object {$_})){
        if($entry -is [string]){
            $parts=$entry.Split('|',3)
            if($parts.Count -ne 3){throw "Knowledge evidence must be 'CHAR-…|ID,ID|basis': $entry"}
            $record=[pscustomobject]@{character_id=$parts[0].Trim();evidence_ids=@($parts[1].Split(',')|ForEach-Object {$_.Trim()}|Where-Object {$_});basis=$parts[2].Trim()}
        }else{
            $record=[pscustomobject]@{character_id=[string]$entry.character_id;evidence_ids=@($entry.evidence_ids|Where-Object {$_}|ForEach-Object {[string]$_});basis=[string]$entry.basis}
        }
        if($proof.Contains($record.character_id)){throw "Knowledge holder has two bases: $($record.character_id)"}
        if(-not $record.evidence_ids.Count){throw "Knowledge holder needs evidence IDs: $($record.character_id)"}
        Assert-WmmaSingleLine $record.basis "Basis of $($record.character_id)"
        Assert-WmmaEntityIds $graph @($record.character_id) -Type @('character') -Label 'knowledge holder'
        Assert-WmmaEntityIds $graph $record.evidence_ids -Label 'knowledge evidence'
        $proof[$record.character_id]=$record
    }
    $holders=@($KnownTo|Where-Object {$_}|Sort-Object -Unique)
    if(-not $holders.Count){$holders=@($proof.Keys|Sort-Object)}
    Assert-WmmaEntityIds $graph $holders -Type @('character') -Label 'knowledge holder'
    foreach($id in $holders){if(-not $proof.Contains($id)){throw "Knowledge holder $id needs a basis of their own: -KnowledgeEvidence '$id|SCENE-…|почему он это знает'."}}
    foreach($id in @($proof.Keys)){if($holders -notcontains $id){throw "Knowledge evidence names $id, who is not in -KnownTo."}}
    if($Supersedes -and -not $data.facts_by_id.ContainsKey($Supersedes)){throw "Unknown fact to replace: $Supersedes"}
    $registry=$data.knowledge
    $max=0;foreach($fact in $registry.facts){if($fact.id -match '^FACT-(\d+)$'){$max=[Math]::Max($max,[int]$Matches[1])}}
    $id='FACT-{0:D3}' -f ($max+1)
    $registry.facts=@($registry.facts)+@([pscustomobject]@{id=$id;text=$Text;truth=$Truth;evidence_ids=@($EvidenceIds|Where-Object {$_}|Sort-Object -Unique);subject_ids=@($SubjectIds|Where-Object {$_}|Sort-Object -Unique);known_to=$holders;knowledge_evidence=@($holders|ForEach-Object {$proof[$_]});visibility=$Visibility;story_time=$StoryTime;reported_by=$(if($ReportedBy){$ReportedBy}else{$null});recorded_on=(Get-Date -Format 'yyyy-MM-dd')})
    Write-WmmaRegistry $root 'Знания' $registry
    if($Supersedes){[void](Set-WmmaFactSuperseded -Root $root -FactId $Supersedes -ReplacedBy $id)}
    Complete-WmmaMutation -Root $root -SkipCheck:$SkipCheck
    "Created knowledge record: $id"
}
