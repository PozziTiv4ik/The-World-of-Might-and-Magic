param(
    [string]$Branch,
    # The shared current point (scene_ids, fact_ids, focus_ids of Контекст.json) instead of a branch.
    [switch]$Global,
    # Adds a branch: -Branch name, -CharacterId, -SceneIds and -Situation are required.
    [switch]$Create,
    [string]$CharacterId,
    [string]$History,
    [string[]]$SceneIds,[string[]]$FactIds,[string[]]$FocusIds,[string]$Situation,[switch]$SkipCheck
)
$ErrorActionPreference='Stop';$provided=$PSBoundParameters
. (Join-Path $PSScriptRoot '_lib.ps1')
$root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Invoke-VoidlightToolMain -Root $root -Name $MyInvocation.MyCommand.Name -ScriptBlock {
    if($Global -and ($Branch -or $Create)){throw 'Use -Global without -Branch and -Create.'}
    if(-not $Global -and -not $Branch){throw 'Name a branch with -Branch or update the shared point with -Global.'}
    if($Global -and $provided.ContainsKey('Situation')){throw 'The shared point has no situation; a situation belongs to a branch.'}
    $data=New-VoidlightReadModel $root
    $state=$data.state
    $graph=$data.graph
    if($Create){
        Assert-VoidlightSingleLine $Branch 'Branch'
        if($Branch -match '[\\/]' -or $Branch -in @('.','..')){throw 'Branch must be a single directory name.'}
        if(@($state.branches|Where-Object {$_.name -eq $Branch}).Count){throw "Branch already exists: $Branch"}
        if(-not $CharacterId){throw 'A new branch needs -CharacterId.'}
        Assert-VoidlightEntityIds $graph @($CharacterId) -Type @('character') -Label 'branch character'
        if(@($state.branches|Where-Object {$_.character_id -eq $CharacterId}).Count){throw "Character $CharacterId already has a branch."}
        if(-not $SceneIds.Count){throw 'A branch needs at least one scene.'}
        if([string]::IsNullOrWhiteSpace($Situation)){throw 'A new branch needs -Situation.'}
        if($History -and -not (Test-VoidlightReference $root $History)){throw "Branch history file is missing: $History"}
        $entry=[pscustomobject][ordered]@{name=$Branch;character_id=$CharacterId;scene_ids=@();fact_ids=@();focus_ids=@();situation='';history=$(if($History){$History}else{'-'})}
        $state.branches=@($state.branches)+@($entry)
    }elseif($Global){$entry=$state}
    else{$entry=Resolve-VoidlightBranch $data $Branch}
    if($provided.ContainsKey('SceneIds')){
        if(-not $Global -and -not $SceneIds.Count){throw 'A branch needs at least one scene.'}
        Assert-VoidlightEntityIds $graph $SceneIds -Type @('scene') -Label 'scene'
        $entry.scene_ids=@($SceneIds)
    }
    if($provided.ContainsKey('FactIds')){
        foreach($id in $FactIds){if(-not $data.facts_by_id.ContainsKey($id)){throw "Unknown fact: $id"}}
        foreach($id in $FactIds){if($data.facts_by_id[$id].superseded_by){throw "Fact $id is replaced by $($data.facts_by_id[$id].superseded_by); point at the newer fact."}}
        $entry.fact_ids=@($FactIds)
    }
    if($provided.ContainsKey('FocusIds')){
        $validFocus=@($data.questions_by_id.Keys)+@($data.decisions_by_id.Keys)+@($data.fronts_by_id.Keys)
        foreach($id in $FocusIds){if($validFocus -notcontains $id){throw "Unknown focus ID: $id"}}
        $entry.focus_ids=@($FocusIds)
    }
    if($provided.ContainsKey('Situation')){if([string]::IsNullOrWhiteSpace($Situation)){throw 'Situation cannot be empty.'};$entry.situation=$Situation}
    Write-VoidlightRegistry $root 'Контекст' $state
    Complete-VoidlightMutation -Root $root -SkipCheck:$SkipCheck
    if($Global){'Updated the shared current point.'}elseif($Create){"Created branch context: $Branch"}else{"Updated branch context: $($entry.name)"}
}
