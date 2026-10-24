param([switch]$KeepFixture)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '_lib.ps1')
. (Join-Path $PSScriptRoot 'lib/Testing.ps1')
$root=(Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$fixture=Resolve-WmmaPath $root ('.wmma/link-tests/'+[guid]::NewGuid().ToString('N'))
foreach($directory in @('01_Кампания/Главы','01_Кампания/Ветки','02_Лор','03_Персонажи','04_Локации','05_Активы_персонажей','08_Источники')){
    [void][IO.Directory]::CreateDirectory((Join-Path $fixture $directory))
}
function Write-FixtureDocument([string]$Path,[string]$Type,[string]$Id,[string]$Body,[string[]]$Meta=@()){
    $text=(@("# $Id",'','---',"type: $Type",'status: active',"id: $Id")+$Meta+@('---','',$Body,'')) -join "`n"
    Write-WmmaText (Join-Path $fixture $Path) $text
}
Write-FixtureDocument '01_Кампания/Главы/Глава.md' 'chapter' 'CHAPTER-test' 'Техническая фикстура.' @('chapter: 1')
Write-FixtureDocument '03_Персонажи/A.md' 'character' 'CHAR-a' @'
## Кратко
Тестовая карточка A.
## Связи
[B](B.md), [повтор](B.md), [сам](A.md), [упоминание](../04_Локации/Mention.md).
'@ @('aliases: ["Тёзка"]')
Write-FixtureDocument '03_Персонажи/B.md' 'character' 'CHAR-b' '## Кратко
Тестовая карточка B. [A](A.md).' @('aliases: ["Тёзка"]')
Write-FixtureDocument '04_Локации/Mention.md' 'location' 'LOC-mention' 'Только навигация.'
Write-FixtureDocument '03_Персонажи/C.md' 'character' 'CHAR-c' '## Кратко
Карточка без связанных сцен и фактов.'
Write-FixtureDocument '04_Локации/Past.md' 'location' 'LOC-past' 'Только историческое упоминание.'
Write-FixtureDocument '08_Источники/Source.md' 'source_note' 'SRC-test' 'Технический источник. [Сцена](../01_Кампания/Ветки/Scene.md).'
Write-FixtureDocument '01_Кампания/Ветки/Scene.md' 'scene' 'SCENE-test' @'
## Что известно персонажу
Тестовый фрагмент. [Источник](../../08_Источники/Source.md).
## Участники
[A](../../03_Персонажи/A.md), [B](../../03_Персонажи/B.md).
'@ @('chapter: 1','source_ids: ["SRC-test"]','participant_ids: ["CHAR-a", "CHAR-b"]','front_ids: ["FRONT-TEST"]')
# A duplicate merged into A: a redirect card that another document still mentions.
Write-WmmaText (Join-Path $fixture '03_Персонажи/D.md') ((@('# Дубль A','','---','type: character','status: merged','id: CHAR-d','aliases: []','merged_into: CHAR-a','---','','## Объединено','Перенаправление на [A](A.md).','') -join "`n"))
Write-FixtureDocument '04_Локации/Ref.md' 'location' 'LOC-ref' 'Старая ссылка: [дубль](../03_Персонажи/D.md).'
Write-WmmaText (Join-Path $fixture '07_Черновики_и_идеи/Входящие_сообщения.md') '# Входящие'
Write-WmmaText (Join-Path $fixture '03_Персонажи/История/A.md') '# История

## Связи
[Старое место](../../04_Локации/Past.md).
'
Write-WmmaJson (Join-Path $fixture '10_Обслуживание/Миграция_v2.json') @{
    history=@(@{original_path='03_Персонажи/A.md';history_path='03_Персонажи/История/A.md'})
}
$graph=New-WmmaGraph $fixture
Write-WmmaJson (Join-Path $fixture '09_Реестры/Сущности.json') $graph
Write-WmmaJson (Join-Path $fixture '09_Реестры/Решения.json') @{
    decisions=@(@{id='DEC-007';state='accepted';choice='Технический выбор';resolved_from='DEC-PENDING-007';scene_ids=@('SCENE-test');link_paths=@('03_Персонажи/A.md')})
}
Write-WmmaJson (Join-Path $fixture '09_Реестры/Вопросы.json') @{
    questions=@(@{id='Q-WORLD-900';text='Технический вопрос';status='active';priority='высокий';owner='A'})
}
Write-WmmaJson (Join-Path $fixture '09_Реестры/Фронты.json') @{
    fronts=@(@{id='FRONT-TEST';name='Технический фронт'});urgent_forks=@();timers=@()
}
$state=@{
    branches=@(
        @{name='A';character_id='CHAR-a';scene_ids=@('SCENE-test');fact_ids=@('FACT-001','FACT-002','FACT-003');focus_ids=@('Q-WORLD-900','DEC-PENDING-007');situation='Техническая ветка A.'},
        @{name='B';character_id='CHAR-b';scene_ids=@('SCENE-test');fact_ids=@('FACT-001','FACT-002','FACT-003');focus_ids=@();situation='Техническая ветка B.'}
    );scene_ids=@('SCENE-test');fact_ids=@('FACT-001');focus_ids=@()
}
Write-WmmaJson (Join-Path $fixture '09_Реестры/Контекст.json') $state
Write-WmmaJson (Join-Path $fixture '09_Реестры/Знания.json') @{
    facts=@(
        @{id='FACT-001';text='PUBLIC_TOKEN';truth='confirmed';visibility='public';known_to=@();subject_ids=@('CHAR-a');evidence_ids=@('SRC-test')},
        @{id='FACT-002';text='PRIVATE_TOKEN';truth='reported';visibility='restricted';known_to=@('CHAR-a');subject_ids=@('CHAR-a');evidence_ids=@('SCENE-test')},
        @{id='FACT-003';text='GM_TOKEN';truth='unknown';visibility='gm';known_to=@('CHAR-a');subject_ids=@('CHAR-a');evidence_ids=@('SRC-test')}
    )
}
$data=New-WmmaReadModel $fixture
Step 'Graph partitions mentions without losing provenance' {
    Assert-True ($graph.schema_version -eq 3) 'Wrong graph version'
    Assert-True (@($graph.edges|Where-Object kind -eq references).Count -eq 0) 'Mention entered semantic edges'
    Assert-True (@($graph.references|Where-Object kind -ne references).Count -eq 0) 'Typed relationship became a mention'
    Assert-True (@($graph.edges|Where-Object {$_.from -eq 'SCENE-test' -and $_.to -eq 'SRC-test'}).Count -eq 2) 'Explicit and textual source evidence must both survive'
    Assert-True ($data.entities_by_id['SCENE-test'].source_ids -contains 'SRC-test') 'Source projection was lost'
    Assert-True (@($graph.references|Where-Object {$_.from -eq $_.to}).Count -eq 0) 'Self navigation retained'
    $refs=@(Get-WmmaDocumentReferences $fixture '03_Персонажи/A.md' '[B](B.md) [again](B.md) [escape](../../../outside.md)' -KnownPaths $data.entities_by_path)
    Assert-True ($refs.Count -eq 1 -and $refs[0] -eq '03_Персонажи/B.md') 'Indexed resolution duplicated or escaped a path'
}
Step 'Nearest connections join registries and return each node once' {
    $result=Get-WmmaConnections $fixture CHAR-a -MaxNodes 200 -Data $data
    $ids=@($result.connections.id)
    foreach($id in @('SCENE-test','FACT-002','Q-WORLD-900','DEC-007')){Assert-True ($ids -contains $id) "Missing explicit registry connection: $id"}
    Assert-True ($ids -notcontains 'LOC-mention' -and $ids -notcontains 'LOC-past') 'Default expanded document mentions'
    Assert-True (($ids|Sort-Object -Unique).Count -eq $ids.Count) 'Duplicate connected node'
    $source=Get-WmmaConnections $fixture SCENE-test -MaxNodes 200 -Data $data
    Assert-True (@($source.connections|Where-Object id -eq SRC-test).Count -eq 1) 'Multiple proofs produced duplicate source nodes'
    Assert-True (@(($source.connections|Where-Object id -eq SRC-test).relations).Count -eq 2) 'A direct source proof was discarded'
}
Step 'Mention and history expansion require their own switches' {
    $mentions=Get-WmmaConnections $fixture CHAR-a -IncludeReferences -MaxNodes 200 -Data $data
    Assert-True ($mentions.connections.id -contains 'LOC-mention') 'Requested mention missing'
    Assert-True ($mentions.connections.id -notcontains 'LOC-past') 'History expanded implicitly'
    $history=Get-WmmaConnections $fixture CHAR-a -IncludeReferences -IncludeHistory -MaxNodes 200 -Data $data
    Assert-True ($history.connections.id -contains 'LOC-past') 'Historical reference lost'
    Assert-True (@(($history.connections|Where-Object id -eq LOC-past).relations|Where-Object scope -eq historical).Count -gt 0) 'Historical scope lost'
}
Step 'Reciprocal cycles obey both depth and node limits' {
    $before=$data|ConvertTo-Json -Depth 40 -Compress
    $result=Get-WmmaConnections $fixture CHAR-a -Depth 3 -MaxNodes 200 -IncludeReferences -IncludeHistory -Data $data
    $ids=@($result.connections.id)
    Assert-True ($ids -notcontains 'CHAR-a' -and ($ids|Sort-Object -Unique).Count -eq $ids.Count) 'Traversal revisited a node'
    foreach($node in $result.connections){
        Assert-True ($node.distance -le 3 -and ($node.via -eq 'CHAR-a' -or $ids -contains $node.via)) 'Broken bounded route'
    }
    $limited=Get-WmmaConnections $fixture CHAR-a -Depth 3 -MaxNodes 1 -Data $data
    Assert-True ($limited.connections.Count -eq 1 -and $limited.truncated) 'Node cap was ignored or hidden'
    Assert-True (($data|ConvertTo-Json -Depth 40 -Compress) -ceq $before) 'Read query changed its snapshot'
}
Step 'Ambiguous names fail and decision aliases retain identity' {
    Expect-Failure {Resolve-WmmaEntity $data 'Тёзка'} 'Ambiguous alias was guessed'
    Assert-True ((Resolve-WmmaEntity $data 'CHAR-a').id -eq 'CHAR-a') 'Stable ID did not resolve'
    $resolved=Get-WmmaConnections $fixture DEC-PENDING-007 -Data $data
    Assert-True ($resolved.entity.id -eq 'DEC-007' -and $resolved.requested_id -eq 'DEC-PENDING-007') 'Decision transition lost'
    Expect-Failure {New-WmmaIdIndex @(@{id='CHAR-a'},@{id='CHAR-a'})} 'Duplicate identity accepted'
}
Step 'Context and memory share the same knowledge boundary' {
    $a=Get-WmmaContext $fixture -Branch A -Audience character -Data $data|ConvertTo-Json -Depth 20
    $b=Get-WmmaContext $fixture -Branch B -Audience character -Data $data|ConvertTo-Json -Depth 20
    Assert-True ($a.Contains('PRIVATE_TOKEN') -and $a.Contains('PUBLIC_TOKEN') -and -not $a.Contains('GM_TOKEN')) 'Viewer A knowledge boundary failed'
    Assert-True ($b.Contains('PUBLIC_TOKEN') -and -not $b.Contains('PRIVATE_TOKEN') -and -not $b.Contains('GM_TOKEN')) 'Viewer B knowledge boundary failed'
    $memory=New-WmmaCharacterMemory $fixture -Data $data
    $aMemory=$memory.characters|Where-Object character_id -eq CHAR-a
    Assert-True ($aMemory.known_fact_ids -contains 'FACT-002' -and $aMemory.known_fact_ids -notcontains 'FACT-003') 'Memory disagrees with character context'
    $emptyMemory=$memory.characters|Where-Object character_id -eq CHAR-c
    Assert-True ($emptyMemory.world_fact_ids.Count -eq 0 -and $emptyMemory.scene_reference_ids.Count -eq 0) 'Missing index groups introduced null references'
}
Step 'Read snapshots are explicit and refresh after source changes' {
    $path=Join-Path $fixture '09_Реестры/Вопросы.json'
    $questions=Read-WmmaJson $path;$questions.questions[0].text='REFRESHED_TOKEN';Write-WmmaJson $path $questions
    $fresh=New-WmmaReadModel $fixture
    Assert-True ($fresh.questions_by_id['Q-WORLD-900'].text -eq 'REFRESHED_TOKEN') 'New operation reused stale data'
    Assert-True ($data.questions_by_id['Q-WORLD-900'].text -ne 'REFRESHED_TOKEN') 'Existing snapshot changed underneath a reader'
    Expect-Failure {Resolve-WmmaReadModel $root $data} 'Snapshot crossed project roots'
}
Step 'Long converging chronology is checked without recursion' {
    $relations=[Collections.Generic.List[object]]::new()
    for($i=0;$i -lt 3000;$i++){
        $relations.Add(@{from="EVENT-$i";to=('EVENT-'+($i+1));relation='after'})
        if($i -gt 0){$relations.Add(@{from=('EVENT-'+($i-1));to=('EVENT-'+($i+1));relation='learned_after'})}
    }
    $relations.Add(@{from='EVENT-3000';to='EVENT-0';relation='overlaps'})
    Assert-True (@(Get-WmmaOrderCycles @($relations)).Count -eq 0) 'Converging paths or overlaps produced a cycle'
    $relations.Add(@{from='EVENT-3000';to='EVENT-2999';relation='learned_after'})
    $cycles=@(Get-WmmaOrderCycles @($relations))
    Assert-True ($cycles.Count -eq 1 -and $cycles[0].Contains('EVENT-2999') -and $cycles[0].Contains('EVENT-3000')) 'Ordering cycle was missed or duplicated'
}
Step 'Rebuild dates do not rewrite unchanged views or hide changed data' {
    $path=Join-Path $fixture '.wmma/generated.md'
    $old="# Index`n`ngenerated_real_date: 2026-09-21`n`nOriginal row.`n"
    Write-WmmaText $path $old
    $stamp=(Get-Item -LiteralPath $path).LastWriteTimeUtc
    Write-WmmaGeneratedText $path ($old.Replace('2026-09-21','2026-09-22'))
    Assert-True ((Read-WmmaText $path) -ceq $old -and (Get-Item -LiteralPath $path).LastWriteTimeUtc -eq $stamp) 'Date-only rebuild rewrote a view'
    $changed=$old.Replace('Original row.','New row.').Replace('2026-09-21','2026-09-22')
    Write-WmmaGeneratedText $path $changed
    Assert-True ((Read-WmmaText $path) -ceq $changed) 'Substantive view change was suppressed'
    $path=Join-Path $fixture '.wmma/generated.json'
    $json=[ordered]@{updated_real_date='2026-09-21';record=[ordered]@{updated_real_date='2026-09-21'}}|ConvertTo-Json
    Write-WmmaText $path $json
    $new=$json.Replace('  "updated_real_date": "2026-09-21"','  "updated_real_date": "2026-09-22"')
    # Change just the top-level generation date, then also the record's date.
    $top=[regex]::Replace($json,'(?m)^  "updated_real_date": "2026-09-21"','  "updated_real_date": "2026-09-22"')
    Write-WmmaGeneratedText $path $top -Format json
    Assert-True ((Read-WmmaText $path) -ceq $json) 'JSON generation date caused churn'
    Write-WmmaGeneratedText $path $new -Format json
    Assert-True ((Read-WmmaText $path) -ceq $new) 'Nested source date was ignored'
}
Step 'Shared parsers preserve empty metadata filenames and table cells' {
    $text="# Fixture`n`n---`nstatus:`nnext: value`n---`nstatus: author text"
    Assert-True ((Get-WmmaMeta $text status -Default '-') -ceq '') 'Empty metadata consumed the following field'
    Assert-True ((Get-WmmaMeta $text absent -Default '-') -ceq '-') 'Missing metadata default lost'
    Assert-True ((Format-WmmaTableCell $null -Empty '-') -ceq '-') 'Missing-cell policy changed'
    Assert-True ((Format-WmmaTableCell "a|b`nc") -ceq 'a/b c') 'Table cell formatting changed'
    Assert-True ((Convert-WmmaFileName 'Тест / A?' -Lowercase) -ceq 'тест__a') 'Source filename convention changed'
    Expect-Failure {Read-WmmaRegistry $fixture 'Missing'} 'Missing registry silently reconstructed'
    foreach($script in @('Собрать_решения','Собрать_вопросы','Собрать_фронты')){
        # Parameter metadata only: a fixture suite never runs builders against the real project.
        # Common parameters mark an advanced script, which rejects unknown flags instead of ignoring them.
        $parameters=(Get-Command (Join-Path $root "tools/$script.ps1")).Parameters
        Assert-True ($parameters.ContainsKey('Verbose') -and -not $parameters.ContainsKey('ImportFromMarkdown')) 'Obsolete import flag was silently accepted'
    }
}
Step 'Graph schema rejects mixed layers and missing evidence' {
    $schema=Join-Path $root '09_Реестры/Схемы/Память.schema.json'
    Assert-True (Test-Json -Json ($graph|ConvertTo-Json -Depth 30) -SchemaFile $schema -ErrorAction Stop) 'Valid graph rejected'
    $invalid=$graph|ConvertTo-Json -Depth 30|ConvertFrom-Json
    $invalid.edges+=@($invalid.references[0])
    Expect-Failure {Test-Json -Json ($invalid|ConvertTo-Json -Depth 30) -SchemaFile $schema -ErrorAction Stop} 'Mention accepted as a semantic edge'
    $invalid=$graph|ConvertTo-Json -Depth 30|ConvertFrom-Json
    $invalid.edges[0].evidence=''
    Expect-Failure {Test-Json -Json ($invalid|ConvertTo-Json -Depth 30) -SchemaFile $schema -ErrorAction Stop} 'Unproven relationship accepted'
}
Step 'A merged card redirects names, mentions and navigation to the main card' {
    $fixtureGraph=$graph
    $merged=New-WmmaReadModel $fixture
    $redirect=$merged.entities_by_id['CHAR-d']
    Assert-True ($redirect.merged_into -eq 'CHAR-a' -and $merged.merged_into['CHAR-d'] -eq 'CHAR-a') 'Redirect lost its target'
    Assert-True ((Resolve-WmmaEntity $merged 'CHAR-d').id -eq 'CHAR-a') 'Merged ID did not resolve to the main card'
    Assert-True ((Resolve-WmmaEntity $merged 'Дубль A').id -eq 'CHAR-a') 'Title of the merged card did not resolve to the main card'
    Assert-True (@($fixtureGraph.references|Where-Object {$_.from -eq 'LOC-ref' -and $_.to -eq 'CHAR-a'}).Count -eq 1) 'Mention of the redirect was not counted for the main card'
    Assert-True (@($fixtureGraph.references|Where-Object {$_.to -eq 'CHAR-d'}).Count -eq 0) 'Mention still points at the redirect'
    $connections=Get-WmmaConnections $fixture CHAR-d -IncludeReferences -MaxNodes 200 -Data $merged
    Assert-True ($connections.entity.id -eq 'CHAR-a' -and $connections.requested_id -eq 'CHAR-d') 'Navigation did not start at the main card'
    $memory=New-WmmaCharacterMemory $fixture -Data $merged
    Assert-True (@($memory.characters|Where-Object character_id -eq 'CHAR-d').Count -eq 0) 'Redirect card got its own memory'
    $problems=@(Get-WmmaMergedReferenceProblems -Graph $fixtureGraph -State $merged.state -Knowledge $merged.knowledge -Decisions $merged.decisions -Chronology ([pscustomobject]@{relations=@()}))
    Assert-True ($problems.Count -eq 0) "Clean redirect reported: $($problems.message -join '; ')"
    $knowledge=$merged.knowledge|ConvertTo-Json -Depth 20|ConvertFrom-Json
    $knowledge.facts[0].known_to=@('CHAR-d')
    $problems=@(Get-WmmaMergedReferenceProblems -Graph $fixtureGraph -State $merged.state -Knowledge $knowledge -Decisions $merged.decisions -Chronology ([pscustomobject]@{relations=@()}))
    Assert-True (@($problems|Where-Object {$_.message -like '*merged entity*CHAR-d*use CHAR-a*'}).Count -eq 1) 'Direct reference to the redirect passed'
}
Step 'A replaced fact leaves context packets and keeps its record' {
    $path=Join-Path $fixture '09_Реестры/Знания.json';$original=Read-WmmaText $path
    $statePath=Join-Path $fixture '09_Реестры/Контекст.json';$stateOriginal=Read-WmmaText $statePath
    try{
        $knowledge=Read-WmmaJson $path
        $knowledge.facts=@($knowledge.facts)+@(
            [pscustomobject]@{id='FACT-004';text='OLD_TOKEN';truth='reported';visibility='public';known_to=@();subject_ids=@('CHAR-a');evidence_ids=@('SRC-test')},
            [pscustomobject]@{id='FACT-005';text='NEW_TOKEN';truth='confirmed';visibility='public';known_to=@();subject_ids=@('CHAR-a');evidence_ids=@('SRC-test')})
        Write-WmmaJson $path $knowledge
        $state=Read-WmmaJson $statePath;$state.branches[0].fact_ids=@($state.branches[0].fact_ids)+@('FACT-004');Write-WmmaJson $statePath $state
        $result=Set-WmmaFactSuperseded -Root $fixture -FactId FACT-004 -ReplacedBy FACT-005
        Assert-True ($result.changed -and $result.moved_pointers -eq 1) 'Replacement did not move the pointer'
        Assert-True (@((Read-WmmaJson $statePath).branches[0].fact_ids) -contains 'FACT-005') 'Branch still points at the old fact'
        Expect-Failure {Set-WmmaFactSuperseded -Root $fixture -FactId FACT-005 -ReplacedBy FACT-004} 'Replacement cycle accepted'
        $context=Get-WmmaContext $fixture -Branch A -Audience gm -Data (New-WmmaReadModel $fixture)
        Assert-True ($context.text.Contains('NEW_TOKEN') -and -not $context.text.Contains('OLD_TOKEN')) 'Replaced fact stayed in the packet'
        $old=@((Read-WmmaJson $path).facts|Where-Object id -eq 'FACT-004')[0]
        Assert-True ($old.superseded_by -eq 'FACT-005' -and $old.text -eq 'OLD_TOKEN') 'Replaced fact record changed'
    }finally{Write-WmmaText $path $original;Write-WmmaText $statePath $stateOriginal}
}
Step 'Similar names in a graph are reported unless confirmed distinct' {
    $pairs=@(Get-WmmaSimilarNamePairs @($graph.entities|Where-Object type -eq 'character'))
    Assert-True (@($pairs|Where-Object {($_.ids -join '|') -eq 'CHAR-a|CHAR-b'}).Count -eq 1) 'Shared alias was not reported'
    Assert-True (@($pairs|Where-Object {$_.ids -contains 'CHAR-d'}).Count -eq 0) 'Redirect card was reported as a duplicate'
}
Complete-WmmaTestRun $fixture -KeepFixture:$KeepFixture
