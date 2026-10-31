# One read-only snapshot per operation. Pass it explicitly to cooperating readers;
# never reuse it after a write or keep it in process-global state.
function Add-VoidlightIndexItem {
    param([hashtable]$Index,[string]$Key,[object]$Value)
    if(-not $Key){return}
    if(-not $Index.ContainsKey($Key)){$Index[$Key]=[Collections.Generic.List[object]]::new()}
    $Index[$Key].Add($Value)
}

function New-VoidlightIdIndex {
    param([object[]]$Items)
    $index=@{}
    foreach($item in $Items){
        if(-not $item.id){throw 'An indexed record needs an ID.'}
        if($index.ContainsKey($item.id)){throw "Duplicate record ID: $($item.id)"}
        $index[$item.id]=$item
    }
    return $index
}

function Test-VoidlightFactVisible {
    param([object]$Fact,[string]$Viewer,[ValidateSet('gm','character')][string]$Audience='character')
    return $Audience -eq 'gm' -or ($Fact.visibility -ne 'gm' -and (
        $Fact.visibility -eq 'public' -or ($Viewer -and $Fact.known_to -contains $Viewer)))
}

function New-VoidlightReadModel {
    param([string]$Root)
    $resolved=(Resolve-Path -LiteralPath $Root).Path
    $registries=@{}
    foreach($name in @('Сущности','Контекст','Знания','Решения','Вопросы','Фронты')){
        $registries[$name]=Read-VoidlightJson (Join-Path $resolved "09_Реестры/$name.json")
    }
    $graph=$registries['Сущности'];$knowledge=$registries['Знания']
    $byId=New-VoidlightIdIndex @($graph.entities)
    # A merged card is a redirect: its ID, title and aliases answer for the main card.
    $mergedInto=@{}
    foreach($entity in $graph.entities){if($entity.merged_into -and $byId.ContainsKey($entity.merged_into)){$mergedInto[$entity.id]=$entity.merged_into}}
    $byPath=@{};$byAlias=@{};$byType=@{};$scenesByParticipant=@{};$factsBySubject=@{}
    foreach($entity in $graph.entities){
        $byPath[$entity.path]=$entity
        Add-VoidlightIndexItem $byType $entity.type $entity
        $answer=if($mergedInto.ContainsKey($entity.id)){$byId[$mergedInto[$entity.id]]}else{$entity}
        foreach($alias in @(@($entity.name)+@($entity.aliases)|Sort-Object -Unique)){
            if(-not $alias){continue}
            if($byAlias.ContainsKey($alias) -and $byAlias[$alias].Contains($answer)){continue}
            Add-VoidlightIndexItem $byAlias $alias $answer
        }
        if($entity.type -eq 'scene'){
            foreach($id in @($entity.participant_ids|ForEach-Object {if($mergedInto.ContainsKey($_)){$mergedInto[$_]}else{$_}}|Sort-Object -Unique)){
                Add-VoidlightIndexItem $scenesByParticipant $id $entity
            }
        }
    }
    foreach($fact in $knowledge.facts){
        foreach($id in @($fact.subject_ids|Sort-Object -Unique)){Add-VoidlightIndexItem $factsBySubject $id $fact}
    }
    $decisions=New-VoidlightIdIndex @($registries['Решения'].decisions)
    $decisionAliases=@{}
    foreach($decision in $registries['Решения'].decisions){
        if($decision.resolved_from){
            if($decisions.ContainsKey($decision.resolved_from) -or $decisionAliases.ContainsKey($decision.resolved_from)){
                throw "Ambiguous decision transition: $($decision.resolved_from)"
            }
            $decisionAliases[$decision.resolved_from]=$decision
        }
    }
    return [pscustomobject]@{
        root=$resolved;chapter=(Get-VoidlightCurrentChapter $resolved)
        graph=$graph;state=$registries['Контекст'];knowledge=$knowledge
        decisions=$registries['Решения'];questions=$registries['Вопросы'];fronts=$registries['Фронты']
        entities_by_id=$byId;entities_by_path=$byPath;entities_by_alias=$byAlias;entities_by_type=$byType
        merged_into=$mergedInto
        scenes_by_participant=$scenesByParticipant;facts_by_subject=$factsBySubject
        facts_by_id=(New-VoidlightIdIndex @($knowledge.facts))
        decisions_by_id=$decisions;decision_aliases=$decisionAliases
        questions_by_id=(New-VoidlightIdIndex @($registries['Вопросы'].questions))
        fronts_by_id=(New-VoidlightIdIndex @($registries['Фронты'].fronts))
        history_index=(Get-VoidlightHistoryIndex $resolved)
    }
}

function Resolve-VoidlightReadModel {
    param([string]$Root,[object]$Data)
    if($null -eq $Data){return New-VoidlightReadModel $Root}
    if((Resolve-Path -LiteralPath $Root).Path -ne $Data.root){throw 'Read model belongs to another project.'}
    return $Data
}

# A stable ID, a merged ID (answers with the main card) or an unambiguous title or alias.
function Resolve-VoidlightEntity {
    param([object]$Data,[string]$Entity)
    if($Data.entities_by_id.ContainsKey($Entity)){
        if($Data.merged_into -and $Data.merged_into.ContainsKey($Entity)){return $Data.entities_by_id[$Data.merged_into[$Entity]]}
        return $Data.entities_by_id[$Entity]
    }
    $matches=@($Data.entities_by_alias[$Entity]|Where-Object {$_})
    if($matches.Count -ne 1){throw "Entity is missing or ambiguous: $Entity ($($matches.Count) matches). Use a stable ID."}
    return $matches[0]
}

function Resolve-VoidlightBranch {
    param([object]$Data,[string]$Branch)
    if(-not $Branch){return $null}
    $matches=@($Data.state.branches|Where-Object {$_.name -eq $Branch -or $_.character_id -eq $Branch})
    if($matches.Count -ne 1){throw "Unknown or ambiguous branch: $Branch"}
    return $matches[0]
}
