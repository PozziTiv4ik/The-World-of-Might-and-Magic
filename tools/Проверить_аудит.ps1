param([switch]$KeepFixture)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '_lib.ps1')
. (Join-Path $PSScriptRoot 'lib/Testing.ps1')
$root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$testRoot=New-VoidlightProjectFixture $root 'audit-tests'
$nl=[char]10
$tick=[char]96
Step 'Empty metadata stays empty' {
    $doc=@('# Fixture','','---','type: scene','request_id:','source_ids: []','---') -join $nl
    Assert-True ((Get-VoidlightMeta $doc 'request_id') -ceq '') 'Empty scalar consumed the following metadata line'
}
Step 'Images load beyond the Windows legacy path limit' {
    $original=Get-ChildItem -LiteralPath (Join-Path $root '11_Медиа') -Recurse -File -Filter '*.png'|Select-Object -First 1
    $folder=Join-Path $testRoot ('.voidlight/long/'+('nested/'*30))
    [IO.Directory]::CreateDirectory($folder)|Out-Null
    $path=Join-Path $folder 'image.png'
    [IO.File]::Copy($original.FullName,$path)
    $size=Get-VoidlightImageSize $path
    Assert-True ($path.Length -gt 260 -and $size.Width -gt 0 -and $size.Height -gt 0) 'Long-path image failed'
}
Step 'Entity context keeps the requested asset within 500 words' {
    $context=Get-VoidlightContext -Root $testRoot -Entity ASSET-0056 -MaxWords 500
    Assert-True ($context.text.Contains('флагман') -and $context.text.Contains('##')) 'Asset body missing'
    Assert-True (-not $context.text.Contains('Q-WORLD-044')) 'Unrelated global question displaced the asset'
    Assert-True ([regex]::Matches($context.text,'\S+').Count -le 500) 'Entity budget exceeded'
}
Step 'Historical character traits stay available with their evidence' {
    $memory=Read-VoidlightJson (Join-Path $testRoot '09_Реестры/Память_персонажей.json')
    $profiles=@($memory.characters|Where-Object {$_.historical_sections.Count -gt 0})
    Assert-True ($profiles.Count -ge 27) 'Preserved character sections missing from memory'
    foreach($person in $profiles){foreach($section in $person.historical_sections){
        $history=Read-VoidlightText (Join-Path $testRoot $section.path)
        Assert-True ($history.Contains($section.text)) 'Historical excerpt changed'
    }}
    $m=@($memory.characters|Where-Object character_id -eq CHAR-0136)[0]
    foreach($id in @('FACT-003','FACT-004','FACT-005')){Assert-True ($m.known_fact_ids -notcontains $id) "Unsupported audience knowledge: $id"}
}
Step 'Markdown and inline source links resolve once' {
    $source=@((Read-VoidlightJson (Join-Path $testRoot '09_Реестры/Сущности.json')).entities|Where-Object type -eq source_note)[0]
    $text="$tick$($source.path)$tick [same](<$($source.path)>)"
    $refs=@(Get-VoidlightDocumentReferences $testRoot '03_Персонажи/Fixture.md' $text)
    Assert-True ($refs.Count -eq 1 -and $refs[0] -ceq $source.path) 'Explicit source link resolution is incomplete or duplicated'
}
$raw=@('Исходный текст автора.','### Внутренний заголовок','## Обработанные входящие',("$tick"*3)+'json','status: авторский текст','Request-ID: MSG-fake',("$tick"*3),'Конец исходного текста.') -join $nl
Step 'Source intake preserves embedded headings fences and status lines' {
    & (Join-Path $testRoot 'tools/Принять_сообщение.ps1') -Title 'Вложенный Markdown' -Text $raw -RequestId MSG-audit-raw -SkipCheck|Out-Null
    $receipt=Get-VoidlightReceipt $testRoot MSG-audit-raw
    Assert-True ([bool]$receipt.source_path) 'Default intake did not create source'
    & (Join-Path $testRoot 'tools/Обработать_входящее.ps1') -RequestId MSG-audit-raw -Status отложено -SkipCheck|Out-Null
    Assert-True ((Get-VoidlightReceipt $testRoot MSG-audit-raw).state -eq 'deferred') 'Deferral discarded message'
    & (Join-Path $testRoot 'tools/Сцена_из_входящего.ps1') -RequestId MSG-audit-raw -Branch Аудит -Title 'Вложенный Markdown' -SkipCheck|Out-Null
    $receipt=Get-VoidlightReceipt $testRoot MSG-audit-raw
    $source=Read-VoidlightText (Join-Path $testRoot $receipt.source_path)
    Assert-True ((Get-VoidlightRawMessage $source).Replace(([string][char]13),'') -ceq $raw) 'Author payload changed'
    Assert-True ((Get-VoidlightMeta $source 'status') -eq 'processed') 'Source lifecycle did not advance'
    Assert-VoidlightReceiptPayload $testRoot $receipt
    & (Join-Path $testRoot 'tools/Обработать_входящее.ps1') -RequestId MSG-audit-raw -SkipCheck|Out-Null
    Expect-Failure {& (Join-Path $testRoot 'tools/Сцена_из_входящего.ps1') -RequestId MSG-audit-raw -Branch Другая -SkipCheck} 'Retry accepted a different branch'
    Write-VoidlightText (Join-Path $testRoot $receipt.source_path) ($source.Replace('Конец исходного текста.','Повреждение.'))
    Expect-Failure {& (Join-Path $testRoot 'tools/Сцена_из_входящего.ps1') -RequestId MSG-audit-raw -Branch Аудит -SkipCheck} 'Corrupted source passed retry'
    Write-VoidlightText (Join-Path $testRoot $receipt.source_path) $source
}
Step 'Inbox-only payload and duplicate titles remain addressable by request ID' {
    foreach($id in @('MSG-audit-one','MSG-audit-two')){
        & (Join-Path $testRoot 'tools/Принять_сообщение.ps1') -Title 'Одинаковое название' -Text ($raw+$nl+$id) -RequestId $id -Mode inbox -SkipCheck|Out-Null
    }
    & (Join-Path $testRoot 'tools/Сцена_из_входящего.ps1') -RequestId MSG-audit-one -Branch Аудит -Title 'Одинаковое название' -SkipCheck|Out-Null
    & (Join-Path $testRoot 'tools/Обработать_входящее.ps1') -RequestId MSG-audit-two -SkipCheck|Out-Null
    foreach($id in @('MSG-audit-one','MSG-audit-two')){Assert-VoidlightReceiptPayload $testRoot (Get-VoidlightReceipt $testRoot $id)}
}
Step 'Scene replacement and invalid references fail before writing' {
    $r=Get-VoidlightReceipt $testRoot MSG-audit-one
    $scenePath=Join-Path $testRoot $r.scene_path
    $original=Read-VoidlightText $scenePath
    $number=[int]([regex]::Match((Split-Path -Leaf $scenePath),'Сцена_(\d+)').Groups[1].Value)
    Expect-Failure {& (Join-Path $testRoot 'tools/Новая_сцена.ps1') -Branch Аудит -Title 'Одинаковое название' -Number $number -Force -SkipCheck} 'Scene force replaced stable identity'
    Assert-True ((Read-VoidlightText $scenePath) -ceq $original) 'Rejected replacement wrote the scene'
    Expect-Failure {& (Join-Path $testRoot 'tools/Новая_сцена.ps1') -Branch Аудит -Title 'Некорректная ссылка' -SourceIds SRC-missing -SkipCheck} 'Invalid source reference accepted'
}
Step 'Full rebuild validates embedded author messages and receipts' {
    Invoke-VoidlightStageValidation $testRoot -Build
}
Step 'An original source can be requested as context' {
    $receipt=Get-VoidlightReceipt $testRoot MSG-audit-raw
    $id=Get-VoidlightMeta (Read-VoidlightText (Join-Path $testRoot $receipt.source_path)) 'id'
    $context=Get-VoidlightContext -Root $testRoot -Entity $id -MaxWords 500
    Assert-True ($context.text.Contains('Конец исходного текста.')) 'Original source returned an empty context packet'
}
Step 'Full graph validation detects tampering beyond content hashes' {
    $path=Join-Path $testRoot '09_Реестры/Сущности.json'
    $original=Read-VoidlightText $path
    $graph=Read-VoidlightJson $path
    $graph.edges=@($graph.edges|Select-Object -Skip 1)
    Write-VoidlightJson $path $graph
    $result=& (Join-Path $testRoot 'tools/Проверить_контекст.ps1') -AsObject
    Assert-True (@($result.Errors|Where-Object {$_ -like '*graph*'}).Count -gt 0) 'Altered graph passed validation'
    Write-VoidlightText $path $original
}
Step 'Validation detects stale memory and corrupted source payloads' {
    $path=Join-Path $testRoot '09_Реестры/Память_персонажей.json'
    $original=Read-VoidlightText $path
    $memory=Read-VoidlightJson $path
    $memory.characters[0].known_fact_ids=@('FACT-003')
    Write-VoidlightJson $path $memory
    $result=& (Join-Path $testRoot 'tools/Проверить_контекст.ps1') -AsObject
    Assert-True (@($result.Errors|Where-Object {$_ -like '*memory is stale*'}).Count -eq 1) 'Stale character memory passed'
    Write-VoidlightText $path $original
    $receipt=Get-VoidlightReceipt $testRoot MSG-audit-raw
    $path=Join-Path $testRoot $receipt.source_path
    $original=Read-VoidlightText $path
    Write-VoidlightText $path ($original.Replace('Конец исходного текста.','Повреждение.'))
    $result=& (Join-Path $testRoot 'tools/Проверить_контекст.ps1') -AsObject
    Assert-True (@($result.Errors|Where-Object {$_ -like '*Invalid receipt MSG-audit-raw*'}).Count -eq 1) 'Corrupted original source passed validation'
    Write-VoidlightText $path $original
}
Step 'Entity IDs follow the type prefix and the number or GUID format' {
    $graph=[pscustomobject]@{entities=@(
        [pscustomobject]@{id='CHAR-0001';type='character';path='a.md'},
        [pscustomobject]@{id='CHAR-12';type='character';path='b.md'},
        [pscustomobject]@{id='LOC-0002';type='character';path='c.md'},
        [pscustomobject]@{id=('SCENE-'+[guid]::NewGuid().ToString('N'));type='scene';path='d.md'},
        [pscustomobject]@{id='назначается_инструментом';type='character';path='e.md'}
    )}
    $problems=@(Get-VoidlightEntityIdProblems $graph)
    Assert-True ($problems.Count -eq 3) "Wrong ID problems: $($problems.message -join '; ')"
    Assert-True (@($problems|Where-Object {$_.message -like '*prefix*LOC-0002*'}).Count -eq 1) 'Prefix mismatch passed'
    Assert-True (@(Get-VoidlightEntityIdProblems (Read-VoidlightJson (Join-Path $testRoot '09_Реестры/Сущности.json'))).Count -eq 0) 'Project IDs rejected'
}
Step 'Character statuses come from the dictionary and synonyms are refused' {
    $graph=[pscustomobject]@{entities=@(
        [pscustomobject]@{id='CHAR-0001';type='character';status='dead';path='a.md'},
        [pscustomobject]@{id='CHAR-0002';type='character';status='deceased';path='b.md'},
        [pscustomobject]@{id='CHAR-0003';type='character';status='weird';path='c.md'},
        [pscustomobject]@{id='CHAR-0004';type='character';status='historical_dead';path='d.md'}
    )}
    $problems=@(Get-VoidlightCharacterStatusProblems -Root $testRoot -Graph $graph)
    Assert-True ($problems.Count -eq 2) "Wrong status problems: $($problems.message -join '; ')"
    Assert-True (@($problems|Where-Object {$_.message -like "*synonym*'dead'*"}).Count -eq 1) 'Synonym not normalized'
    Assert-True (@(Get-VoidlightCharacterStatusProblems -Root $testRoot -Graph (Read-VoidlightJson (Join-Path $testRoot '09_Реестры/Сущности.json'))).Count -eq 0) 'Project statuses rejected'
}
Step 'Similar names are reported unless merged or confirmed distinct' {
    $a=[pscustomobject]@{id='CHAR-0001';name='Закхерон Багровый';aliases=@('Закхерон Багровый');path='a.md'}
    $b=[pscustomobject]@{id='CHAR-0002';name='Закхерон';aliases=@('Закхерон');path='b.md'}
    $c=[pscustomobject]@{id='CHAR-0003';name='Ёжик';aliases=@();path='c.md'}
    $d=[pscustomobject]@{id='CHAR-0004';name='ежик';aliases=@();path='d.md'}
    $pairs=@(Get-VoidlightSimilarNamePairs @($a,$b,$c,$d))
    Assert-True ($pairs.Count -eq 2) "Similar names missed: $(($pairs|ForEach-Object {$_.ids -join '/'}) -join '; ')"
    $b|Add-Member -NotePropertyName distinct_from -NotePropertyValue @('CHAR-0001')
    $d|Add-Member -NotePropertyName merged_into -NotePropertyValue 'CHAR-0003'
    Assert-True (@(Get-VoidlightSimilarNamePairs @($a,$b,$c,$d)).Count -eq 0) 'Confirmed or merged names still reported'
    Assert-True (@(Find-VoidlightSimilarEntities @($a,$c) @('Закхерон')).id -contains 'CHAR-0001') 'Creation check missed a contained name'
}
Step 'Scenes of closed chapters are closed and protected' {
    Assert-True (@(Get-VoidlightClosedSceneProblems -Root $testRoot -Graph (Read-VoidlightJson (Join-Path $testRoot '09_Реестры/Сущности.json'))).Count -eq 0) 'Project closed scenes rejected'
    $graph=Read-VoidlightJson (Join-Path $testRoot '09_Реестры/Сущности.json')
    $scene=@($graph.entities|Where-Object {$_.type -eq 'scene' -and $_.chapter -eq '1'})[0]
    $scene.status='active'
    $manifestPath=Join-Path $testRoot '10_Обслуживание/Миграция_v2.json';$original=Read-VoidlightText $manifestPath
    $manifest=Read-VoidlightJson $manifestPath;$manifest.protected_files=@($manifest.protected_files|Where-Object {$_.path -ne $scene.path});Write-VoidlightJson $manifestPath $manifest
    try{
        $problems=@(Get-VoidlightClosedSceneProblems -Root $testRoot -Graph $graph)
        Assert-True (@($problems|Where-Object {$_.message -like '*status closed*'}).Count -eq 1 -and @($problems|Where-Object {$_.message -like '*not protected*'}).Count -eq 1) 'Open or unprotected closed scene passed'
    }finally{Write-VoidlightText $manifestPath $original}
}
Step 'Protected canon accepts new bytes only with a recorded reason' {
    $manifestPath=Join-Path $testRoot '10_Обслуживание/Миграция_v2.json';$manifestOriginal=Read-VoidlightText $manifestPath
    $path=@((Read-VoidlightJson $manifestPath).protected_files|Where-Object {$_.path -like '01_Кампания/Ветки/*'})[0].path
    $full=Join-Path $testRoot $path;$original=Read-VoidlightText $full
    try{
        Write-VoidlightText $full ($original+"`n")
        Expect-Failure {Update-VoidlightProtectedFiles -Root $testRoot -Paths @($path) -Reason ''} 'Change without a reason accepted'
        Expect-Failure {Update-VoidlightProtectedFiles -Root $testRoot -Paths @('10_Обслуживание/README.md') -Reason 'Тест'} 'Unprotected file accepted without -Add'
        Assert-True ((Update-VoidlightProtectedFiles -Root $testRoot -Paths @($path) -Reason 'Тест: формат') -eq 1) 'Amendment was not recorded'
        $manifest=Read-VoidlightJson $manifestPath
        $entry=@($manifest.amendments|Where-Object {$_.path -eq $path})[-1]
        Assert-True ($entry.reason -eq 'Тест: формат' -and $entry.before -and $entry.after -eq (@($manifest.protected_files|Where-Object path -eq $path)[0].sha256)) 'Amendment journal is incomplete'
    }finally{Write-VoidlightText $full $original;Write-VoidlightText $manifestPath $manifestOriginal}
}
Step 'Stale context markers are data' {
    $markers=@(Get-VoidlightStaleContextMarkers $testRoot)
    Assert-True ($markers.Count -ge 1 -and $markers[0].pattern) 'Marker data missing'
    Assert-True ([regex]::IsMatch('Нужно решить, куда отправить захваченный линкор.',$markers[0].pattern,'IgnoreCase, Singleline')) 'Marker pattern lost'
    Assert-True (-not (Read-VoidlightText (Join-Path $root 'tools/Проверить_проект.ps1')).Contains('Hector liner')) 'Story marker is still hard-coded'
}
Step 'Regnum reconciliation checks links without the editor' {
    $world=Join-Path $testRoot '12_Карты/Сверка_тест'
    $data=Join-Path $world 'data'
    [IO.Directory]::CreateDirectory($data)|Out-Null
    Write-VoidlightText (Join-Path $world 'world.json') '{"format":"regnum-world"}'
    Write-VoidlightJson (Join-Path $data 'modifiers.json') @{modifiers=@(@{id='m1';name='Мертв';key='dead'},@{id='m2';name='Живой';key='living'})}
    Write-VoidlightJson (Join-Path $data 'characters.json') @{characters=@(
        @{id='c1';name='Живой по канону';entity='CHAR-0059';modifiers=@('m1')},
        @{id='c2';name='Погибший по канону';entity='CHAR-0035';modifiers=@('m2')},
        @{id='c3';name='Неизвестный';entity='CHAR-9999';modifiers=@()},
        @{id='c4';name='Перенаправление';entity='CHAR-0058';modifiers=@()},
        @{id='c5';name='Верная связь';entity='CHAR-0136';modifiers=@('m2')}
    )}
    Write-VoidlightJson (Join-Path $data 'provinces.json') @{provinces=@(@{id='p1';name='Провинция с персонажем';entity='CHAR-0059'},@{id='p2';name='Столица';entity='LOC-0094'})}
    Write-VoidlightJson (Join-Path $data 'factions.json') @{factions=@(@{id='f1';name='Империя';kind='state';entity='LOC-0110'})}
    $result=& (Join-Path $testRoot 'tools/Сверить_мир_Regnum.ps1') -World $world -AsObject
    $messages=@($result.problems|ForEach-Object {"$($_.kind): $($_.message)"})
    Assert-True ($result.links -eq 8) "Wrong link count: $($result.links)"
    foreach($expected in @('Warning:*c1*«Мертв»*','Warning:*c2*no «Мертв»*','Error:*c3*no such*','Error:*c4*merged into CHAR-0057*','Error:*p1*not CHAR*')){
        Assert-True (@($messages|Where-Object {$_ -like $expected}).Count -eq 1) "Missing reconciliation problem: $expected`n$($messages -join "`n")"
    }
    Assert-True ($messages.Count -eq 5) "Unexpected reconciliation problems:`n$($messages -join "`n")"
    $global:LASTEXITCODE=0
    & (Join-Path $testRoot 'tools/Сверить_мир_Regnum.ps1') -World $world|Out-Null
    Assert-True ($LASTEXITCODE -eq 1) 'Broken links did not fail the standalone check'
    $global:LASTEXITCODE=0
    Remove-Item -LiteralPath $world -Recurse -Force
}
# Transaction protocol tests use a tiny independent fixture. Real project build
# and staged validation are exercised by Проверить_сценарии_контекста.ps1.
$tx=Join-Path $testRoot '.voidlight/protocol'
[IO.Directory]::CreateDirectory($tx)|Out-Null
Write-VoidlightText (Join-Path $tx 'tools/Проверить_проект.ps1') 'exit 0'
Write-VoidlightText (Join-Path $tx 'tools/Завершить_ход.ps1') 'exit 0'
Write-VoidlightText (Join-Path $tx 'existing.txt') 'before'
Step 'Interrupted snapshot resumes and refuses changed base files' {
    Start-VoidlightTurn $tx TURN-copy 'Snapshot'|Out-Null
    $turn=Get-VoidlightTurnPath $tx TURN-copy
    $mp=Join-Path $turn 'transaction.json'
    $m=Read-VoidlightJson $mp;$m.status='copying';Write-VoidlightJson $mp $m
    [IO.File]::Delete((Resolve-VoidlightPath (Join-Path $turn 'stage') 'existing.txt'))
    Write-VoidlightText (Join-Path $tx 'existing.txt') 'concurrent'
    Expect-Failure {Start-VoidlightTurn $tx TURN-copy 'Snapshot'} 'Changed source accepted during resume'
    Write-VoidlightText (Join-Path $tx 'existing.txt') 'before'
    $m=Start-VoidlightTurn $tx TURN-copy 'Snapshot'
    Assert-True ($m.status -eq 'editing' -and (Read-VoidlightText (Join-Path $turn 'stage/existing.txt')) -ceq 'before') 'Partial snapshot was not resumed'
}
Step 'Stage failure reports the child diagnostic' {
    $turn=Get-VoidlightTurnPath $tx TURN-copy
    $check=Join-Path $turn 'stage/tools/Проверить_проект.ps1'
    Write-VoidlightText $check "Write-Output 'DIAGNOSTIC-audit-test'; exit 1"
    $message=Expect-Failure {Invoke-VoidlightStageValidation (Join-Path $turn 'stage')} 'Failed stage accepted' -PassThru
    Assert-True ($message.Contains('DIAGNOSTIC-audit-test')) 'Child failure detail swallowed'
    Write-VoidlightText $check 'exit 0'
}
Step 'Plan tampering backups interrupted apply and repeat restore' {
    $turn=Get-VoidlightTurnPath $tx TURN-copy
    Write-VoidlightText (Join-Path $turn 'stage/existing.txt') 'after'
    Write-VoidlightText (Join-Path $turn 'stage/new.txt') 'new'
    Prepare-VoidlightTurn $tx TURN-copy|Out-Null
    $mp=Join-Path $turn 'transaction.json'
    $original=Read-VoidlightText $mp;$m=Read-VoidlightJson $mp
    $m.base.'existing.txt'=Get-VoidlightHash 'tampered';Write-VoidlightJson $mp $m
    Expect-Failure {Apply-VoidlightTurn $tx TURN-copy} 'Tampered base accepted'
    Expect-Failure {Undo-VoidlightTurn $tx TURN-copy} 'Tampered restore plan accepted'
    Write-VoidlightText $mp $original
    Expect-Failure {Apply-VoidlightTurn $tx TURN-copy -FailAfter 1} 'Interruption did not fire'
    Assert-True ((Apply-VoidlightTurn $tx TURN-copy).status -eq 'committed') 'Interrupted apply failed to resume'
    $backup=Join-Path $turn 'backup/existing.txt'
    Write-VoidlightText $backup 'corrupt'
    Expect-Failure {Undo-VoidlightTurn $tx TURN-copy} 'Corrupt backup accepted'
    Assert-True ((Read-VoidlightText (Join-Path $tx 'existing.txt')) -ceq 'after') 'Rejected restore modified target'
    Write-VoidlightText $backup 'before'
    Assert-True ((Undo-VoidlightTurn $tx TURN-copy).status -eq 'restored') 'Restore failed'
    Assert-True ((Undo-VoidlightTurn $tx TURN-copy).status -eq 'restored') 'Repeated restore failed'
    Assert-True ((Read-VoidlightText (Join-Path $tx 'existing.txt')) -ceq 'before' -and -not (Test-Path -LiteralPath (Join-Path $tx 'new.txt'))) 'Restore did not reproduce original files'
}
Step 'An empty prepared turn is valid and repeatable' {
    Start-VoidlightTurn $tx TURN-empty 'Empty'|Out-Null
    Assert-True (@((Prepare-VoidlightTurn $tx TURN-empty).changes).Count -eq 0) 'Empty turn gained changes'
    Assert-True ((Apply-VoidlightTurn $tx TURN-empty).status -eq 'committed') 'Empty apply failed'
    Assert-True ((Undo-VoidlightTurn $tx TURN-empty).status -eq 'restored') 'Empty restore failed'
}
Step 'Rebase moves an unapplied turn over outside edits of other files' {
    Start-VoidlightTurn $tx TURN-rebase 'Rebase'|Out-Null
    $turn=Get-VoidlightTurnPath $tx TURN-rebase
    Write-VoidlightText (Join-Path $turn 'stage/own.txt') 'mine'
    Prepare-VoidlightTurn $tx TURN-rebase|Out-Null
    [IO.Directory]::CreateDirectory((Join-Path $tx 'world/.regnum-backup'))|Out-Null
    Write-VoidlightText (Join-Path $tx 'world/.regnum-backup/autosave.json') '{}'
    Write-VoidlightText (Join-Path $tx 'world/autosave.json') '{"turn":2}'
    $message=Expect-Failure {Apply-VoidlightTurn $tx TURN-rebase} 'Outside edit was ignored' -PassThru
    Assert-True ($message -like '*concurrent*world/autosave.json*') "Unexpected refusal: $message"
    $m=Rebase-VoidlightTurn $tx TURN-rebase
    Assert-True ($m.status -eq 'editing' -and @($m.rebased_files) -contains 'world/autosave.json') 'Rebase did not reset the plan'
    Assert-True ((Read-VoidlightText (Join-Path $turn 'stage/world/autosave.json')) -ceq '{"turn":2}') 'Outside edit missing in the stage'
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $turn 'stage/world/.regnum-backup'))) 'Regnum backup entered the turn'
    Prepare-VoidlightTurn $tx TURN-rebase|Out-Null
    Assert-True ((Apply-VoidlightTurn $tx TURN-rebase).status -eq 'committed') 'Rebased turn did not apply'
    Assert-True ((Read-VoidlightText (Join-Path $tx 'own.txt')) -ceq 'mine' -and (Read-VoidlightText (Join-Path $tx 'world/autosave.json')) -ceq '{"turn":2}') 'Rebased apply lost a change'
}
Step 'Rebase refuses outside edits of the same files' {
    Start-VoidlightTurn $tx TURN-overlap 'Overlap'|Out-Null
    $turn=Get-VoidlightTurnPath $tx TURN-overlap
    Write-VoidlightText (Join-Path $turn 'stage/own.txt') 'stage version'
    Write-VoidlightText (Join-Path $tx 'own.txt') 'outside version'
    $message=Expect-Failure {Rebase-VoidlightTurn $tx TURN-overlap} 'Overlapping edit was merged' -PassThru
    Assert-True ($message.Contains('own.txt')) 'Conflict list lost the file'
    Assert-True ((Read-VoidlightJson (Join-Path $turn 'transaction.json')).status -eq 'editing') 'Refused rebase changed the journal'
    Write-VoidlightText (Join-Path $tx 'own.txt') 'mine'
}
Step 'Turn journals report unfinished turns and clean finished copies' {
    Start-VoidlightTurn $tx TURN-half 'Half'|Out-Null
    $turn=Get-VoidlightTurnPath $tx TURN-half
    Write-VoidlightText (Join-Path $turn 'stage/half.txt') 'half'
    Write-VoidlightText (Join-Path $turn 'stage/half2.txt') 'half'
    Prepare-VoidlightTurn $tx TURN-half|Out-Null
    Expect-Failure {Apply-VoidlightTurn $tx TURN-half -FailAfter 1} 'Interruption did not fire'
    $problems=@(Get-VoidlightTurnProblems $tx)
    Assert-True (@($problems|Where-Object {$_.kind -eq 'Error' -and $_.message -like '*TURN-half*half applied*'}).Count -eq 1) 'Half-applied turn not reported as an error'
    Assert-True (@($problems|Where-Object {$_.kind -eq 'Warning' -and $_.message -like '*TURN-overlap*editing*'}).Count -eq 1) 'Unapplied turn not reported as a warning'
    $list=@(Get-VoidlightTurns $tx -Size)
    Assert-True (@($list|Where-Object {$_.id -eq 'TURN-half' -and $_.status -eq 'applying' -and $null -ne $_.size_mb}).Count -eq 1) 'Turn list lost status or size'
    Expect-Failure {Remove-VoidlightTurn $tx TURN-half -Unapplied} 'Half-applied copy was discarded'
    Expect-Failure {Remove-VoidlightTurn $tx TURN-overlap} 'Unapplied copy was cleaned as finished'
    Assert-True ((Apply-VoidlightTurn $tx TURN-half).status -eq 'committed') 'Half-applied turn did not resume'
    Assert-True ((Remove-VoidlightTurn $tx TURN-overlap -Unapplied) -eq 'editing') 'Unapplied copy was not discarded'
    $removed=@(Remove-VoidlightFinishedTurns $tx)
    Assert-True (@($removed.id) -contains 'TURN-half' -and @($removed.id) -contains 'TURN-rebase') 'Finished copies were not removed'
    Assert-True (@(Get-VoidlightTurns $tx).Count -eq 0 -and @(Get-VoidlightTurnProblems $tx).Count -eq 0) 'Cleaned journals remain'
}
Complete-VoidlightTestRun $testRoot -KeepFixture:$KeepFixture
