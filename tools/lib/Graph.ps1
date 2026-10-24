function Get-WmmaDocumentReferences {
    param([string]$Root,[string]$DocumentPath,[string]$Text,[System.Collections.IDictionary]$KnownPaths)
    $tick=[char]96
    $patterns=@(('['+$tick+']([^'+$tick+'\r\n]+\.md)['+$tick+']'),'\[[^\]]*\]\(\s*<?([^<>\r\n]+?\.md)(?:#[^)\s>]*)?>?\s*\)')
    $found=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach($pattern in $patterns){foreach($match in [regex]::Matches($Text,$pattern)){
        $ref=[Uri]::UnescapeDataString($match.Groups[1].Value).Replace('\','/')
        if($ref -match '^[a-z]+://' -or [IO.Path]::IsPathRooted($ref)){continue}
        foreach($candidate in @($ref,((Split-Path -Parent $DocumentPath).Replace('\','/')+'/'+$ref))){
            if($null -ne $KnownPaths){
                # The graph already enumerated its documents. Resolve against that
                # inventory instead of walking the filesystem for every mention.
                try{$full=[IO.Path]::GetFullPath((Join-Path $Root $candidate))}catch{continue}
                $base=[IO.Path]::GetFullPath($Root).TrimEnd('\','/')+[IO.Path]::DirectorySeparatorChar
                if(-not $full.StartsWith($base,[StringComparison]::OrdinalIgnoreCase)){continue}
                $relative=Get-WmmaRelativePath $Root $full
                if($KnownPaths.Contains($relative)){[void]$found.Add($relative);break}
            }else{
                try{$full=Resolve-WmmaPath $Root $candidate}catch{continue}
                if(Test-Path -LiteralPath $full -PathType Leaf){[void]$found.Add((Get-WmmaRelativePath $Root $full));break}
            }
        }
    }}
    return @($found|Sort-Object)
}

function Get-WmmaHistoryIndex {
    param([string]$Root)
    $result=@{};$path=Join-Path $Root '10_Обслуживание/Миграция_v2.json'
    if(Test-Path -LiteralPath $path){foreach($item in (Read-WmmaJson $path).history){$result[$item.original_path]=@($result[$item.original_path])+@($item.history_path)}}
    return $result
}

# Graph node of one document. merged_into (a redirect card) and distinct_from (names
# confirmed to belong to other entities) appear only when the card sets them.
function New-WmmaGraphNode {
    param([object]$Document,[hashtable]$History)
    $name=([regex]::Match($Document.text,'(?m)^# (.+)')).Groups[1].Value.Trim()
    $fronts=@(Get-WmmaArrayMeta $Document.text 'front_ids');$legacy=Get-WmmaMeta $Document.text 'front_id'
    if($legacy -and $legacy -ne '-'){$fronts+=@($legacy)}
    $node=[ordered]@{
        id=$Document.id;type=$Document.type;name=$name;path=$Document.path;status=(Get-WmmaMeta $Document.text 'status')
        chapter=(Get-WmmaMeta $Document.text 'chapter');branch=(Get-WmmaMeta $Document.text 'branch')
        aliases=@(@($name)+@(Get-WmmaArrayMeta $Document.text 'aliases')|Sort-Object -Unique)
        front_ids=@($fronts|Sort-Object -Unique);source_ids=@(Get-WmmaArrayMeta $Document.text 'source_ids');participant_ids=@(Get-WmmaArrayMeta $Document.text 'participant_ids')
        history_paths=@($History[$Document.path]|Where-Object {$_});historical_source_ids=@()
        provenance='unresolved';content_sha256=(Get-WmmaHash $Document.text)
    }
    $mergedInto=Get-WmmaMeta $Document.text 'merged_into'
    if($mergedInto -and $mergedInto -ne '-'){$node.merged_into=$mergedInto}
    $distinct=@(Get-WmmaArrayMeta $Document.text 'distinct_from')
    if($distinct.Count){$node.distinct_from=$distinct}
    return $node
}

function New-WmmaGraph {
    param([string]$Root,[switch]$AssignMissingIds)
    $docs=@(Get-WmmaDocuments $Root|Sort-Object path);$byPath=@{};$byId=@{};$next=@{}
    foreach($doc in $docs){
        if($doc.id){
            if($byId.ContainsKey($doc.id)){throw "Duplicate entity ID: $($doc.id)"}
            $byId[$doc.id]=$doc
            if($doc.id -match '^([A-Z]+)-(\d+)$'){$next[$Matches[1]]=[Math]::Max([int]$next[$Matches[1]],[int]$Matches[2])}
        }
    }
    foreach($doc in $docs){
        if(-not $doc.id){
            if(-not $AssignMissingIds){throw "Missing ID: $($doc.path)"}
            $prefix=Get-WmmaIdPrefix $doc.type;$next[$prefix]=[int]$next[$prefix]+1;$doc.id='{0}-{1:D4}' -f $prefix,$next[$prefix]
            Write-WmmaText $doc.full_path (Set-WmmaMeta $doc.text 'id' $doc.id)
            $doc.text=Read-WmmaText $doc.full_path;$byId[$doc.id]=$doc
        }
        $byPath[$doc.path]=$doc.id
    }
    # A merged card is a redirect: mentions of its path count for the card it joined.
    $merged=@{}
    foreach($doc in $docs){$target=Get-WmmaMeta $doc.text 'merged_into';if($target -and $target -ne '-'){$merged[$doc.id]=$target}}
    $history=Get-WmmaHistoryIndex $Root
    $nodes=[Collections.Generic.List[object]]::new();$edges=[Collections.Generic.List[object]]::new()
    $keys=[Collections.Generic.HashSet[string]]::new()
    function Add-GraphEdge([string]$From,[string]$To,[string]$Kind,[string]$Evidence,[string]$Scope='current'){
        if($Kind -in @('references','source_reference') -and $merged.ContainsKey($To)){$To=$merged[$To]}
        if($From -eq $To -and $Kind -in @('references','source_reference')){return}
        $key="$From|$To|$Kind|$Evidence|$Scope"
        if($keys.Add($key)){$edges.Add([ordered]@{from=$From;to=$To;kind=$Kind;evidence=$Evidence;scope=$Scope})}
    }
    foreach($doc in $docs){
        $node=New-WmmaGraphNode $doc $history
        foreach($id in $node.source_ids){Add-GraphEdge $doc.id $id 'sourced_from' $doc.path}
        foreach($id in $node.participant_ids){Add-GraphEdge $doc.id $id 'participant' $doc.path}
        $documents=@([pscustomobject]@{path=$doc.path;text=$doc.text;scope='current'})
        foreach($hp in @($history[$doc.path]|Where-Object {$_})){
            $documents+=[pscustomobject]@{path=$hp;text=(Read-WmmaText (Join-Path $Root $hp));scope='historical'}
        }
        foreach($item in $documents){foreach($ref in @(Get-WmmaDocumentReferences $Root $item.path $item.text -KnownPaths $byPath)){
            if(-not $byPath.ContainsKey($ref)){continue}
            $target=$byPath[$ref]
            $kind=if($byId[$target].type -like 'source*'){'source_reference'}else{'references'}
            Add-GraphEdge $doc.id $target $kind $item.path $item.scope
        }}
        $nodes.Add($node)
    }
    $inboxPath='07_Черновики_и_идеи/Входящие_сообщения.md'
    foreach($entry in @(Get-WmmaInboxEntries (Read-WmmaText (Join-Path $Root $inboxPath)))){
        $sourceRefs=@(Get-WmmaDocumentReferences $Root $inboxPath (Get-WmmaEntryField $entry.Value 'Источник') -KnownPaths $byPath|Where-Object {$byId[$byPath[$_]].type -like 'source*'})
        $sceneRefs=@(Get-WmmaDocumentReferences $Root $inboxPath (Get-WmmaEntryField $entry.Value 'Связано') -KnownPaths $byPath|Where-Object {$byId[$byPath[$_]].type -eq 'scene'})
        foreach($scene in $sceneRefs){foreach($source in $sourceRefs){Add-GraphEdge $byPath[$scene] $byPath[$source] 'sourced_from' $inboxPath}}
    }
    $direct=@{};$past=@{}
    foreach($edge in $edges){if($edge.kind -in @('sourced_from','source_reference')){
        if($edge.scope -eq 'historical'){$past[$edge.from]=@($past[$edge.from])+@($edge.to)}
        else{$direct[$edge.from]=@($direct[$edge.from])+@($edge.to)}
    }}
    foreach($node in $nodes){
        $node.source_ids=@($direct[$node.id]|Where-Object {$_}|Sort-Object -Unique)
        $node.historical_source_ids=@($past[$node.id]|Where-Object {$_}|Sort-Object -Unique)
        $node.provenance=if($node.type -like 'source*'){'original'}elseif($node.source_ids.Count){'linked'}elseif($node.historical_source_ids.Count){'historical'}else{'unresolved'}
    }
    $ordered=@($edges|Sort-Object from,to,kind,evidence,scope)
    return [pscustomobject][ordered]@{
        schema_version=3;type='entity_graph';generated_by='tools/Собрать_связи.ps1'
        entities=@($nodes)
        edges=@($ordered|Where-Object kind -ne 'references')
        references=@($ordered|Where-Object kind -eq 'references')
    }
}

# Fast registration for a batch of write commands run with -SkipCheck: upserts the nodes
# and the direct relationships of the given documents in Сущности.json, so the next command
# sees their IDs. Собрать_связи (in Завершить_ход and Подготовить_ход) later rebuilds the
# whole graph from all documents; until then mentions of the new documents by other
# documents and inbox provenance stay unrecorded.
function Register-WmmaDocuments {
    param([string]$Root,[string[]]$Paths)
    $graphPath=Join-Path $Root '09_Реестры/Сущности.json'
    $graph=Read-WmmaJson $graphPath
    $history=Get-WmmaHistoryIndex $Root
    $entities=[Collections.Generic.List[object]]::new();foreach($entity in @($graph.entities)){$entities.Add($entity)}
    $edges=[Collections.Generic.List[object]]::new();foreach($edge in @($graph.edges)+@($graph.references)){if($edge){$edges.Add($edge)}}
    $byPath=@{};$types=@{};foreach($entity in $entities){$byPath[$entity.path]=$entity.id;$types[$entity.id]=$entity.type}
    foreach($path in @($Paths|Where-Object {$_})){
        $full=Resolve-WmmaPath $Root $path
        $relative=Get-WmmaRelativePath $Root $full
        $text=Read-WmmaText $full
        $document=[pscustomobject]@{path=$relative;full_path=$full;text=$text;type=(Get-WmmaMeta $text 'type');id=(Get-WmmaMeta $text 'id')}
        if(-not $document.id){throw "Document has no ID yet: $relative"}
        $clash=@($entities|Where-Object {$_.id -ceq $document.id -and $_.path -cne $relative})
        if($clash.Count){throw "Duplicate entity ID: $($document.id) ($relative, $($clash[0].path))"}
        foreach($old in @($entities|Where-Object {$_.path -ceq $relative -or $_.id -ceq $document.id})){[void]$entities.Remove($old)}
        foreach($old in @($edges|Where-Object {$_.evidence -ceq $relative})){[void]$edges.Remove($old)}
        $node=New-WmmaGraphNode $document $history
        $byPath[$relative]=$document.id;$types[$document.id]=$document.type
        $refs=@(Get-WmmaDocumentReferences $Root $relative $text -KnownPaths $byPath)
        $sources=@($node.source_ids)
        foreach($ref in $refs){
            $target=$byPath[$ref]
            if(-not $target -or $target -ceq $document.id){continue}
            $kind=if($types[$target] -like 'source*'){'source_reference'}else{'references'}
            $edges.Add([pscustomobject][ordered]@{from=$document.id;to=$target;kind=$kind;evidence=$relative;scope='current'})
            if($kind -eq 'source_reference'){$sources+=$target}
        }
        foreach($id in @($node.source_ids)){$edges.Add([pscustomobject][ordered]@{from=$document.id;to=$id;kind='sourced_from';evidence=$relative;scope='current'})}
        foreach($id in @($node.participant_ids)){$edges.Add([pscustomobject][ordered]@{from=$document.id;to=$id;kind='participant';evidence=$relative;scope='current'})}
        $node.source_ids=@($sources|Where-Object {$_}|Sort-Object -Unique)
        $node.provenance=if($node.type -like 'source*'){'original'}elseif($node.source_ids.Count){'linked'}else{'unresolved'}
        $entities.Add([pscustomobject]$node)
    }
    $ordered=@($edges|Sort-Object from,to,kind,evidence,scope)
    $graph.entities=@($entities|Sort-Object path)
    $graph.edges=@($ordered|Where-Object kind -ne 'references')
    $graph.references=@($ordered|Where-Object kind -eq 'references')
    Write-WmmaJson $graphPath $graph
}
