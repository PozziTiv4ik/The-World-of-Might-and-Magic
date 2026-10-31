param([switch]$KeepFixture)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '_lib.ps1')
. (Join-Path $PSScriptRoot 'lib/Testing.ps1')
$root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$testRoot=New-VoidlightProjectFixture $root 'tests'
Step 'World questions and current scene outrank file order' {
    $s=Get-VoidlightSelection $testRoot
    $ids=@($s.questions|Select-Object -First 8|ForEach-Object {$_.id})
    foreach($id in @('Q-WORLD-044','Q-WORLD-046','Q-C4-001')){Assert-True ($ids -contains $id) "Missing current question: $id"}
    Assert-True ($s.fronts[0].id -eq 'FRONT-THREE-MOONS') 'Main front omitted'
    $qPath=Join-Path $testRoot '09_Реестры/Вопросы.json';$q=Read-VoidlightJson $qPath
    [array]::Reverse($q.questions);Write-VoidlightJson $qPath $q
    $after=@((Get-VoidlightSelection $testRoot).questions|Select-Object -First 8|ForEach-Object {$_.id})
    Assert-True (($ids -join ',') -ceq ($after -join ',')) 'Selection depends on storage order'
}
Step 'Character perspective excludes secrets and GM questions' {
    $a=Get-VoidlightContext -Root $testRoot -Branch 'Александрос' -Audience character
    $m=Get-VoidlightContext -Root $testRoot -Branch 'Михаэль' -Audience character
    Assert-True (-not $a.text.Contains('предполагаемой беременности')) 'Pregnancy leaked to Alexandros'
    Assert-True $m.text.Contains('предполагаемой беременности') 'Michael lost his own knowledge'
    Assert-True (-not $a.text.Contains('DEC-PENDING')) 'GM decisions leaked to character'
    Assert-True (-not $a.text.Contains('женщина в алых')) 'Michael dream leaked'
    $json=$a|ConvertTo-Json -Depth 30
    Assert-True (-not $json.Contains('DEC-PENDING') -and -not $json.Contains('предполагаемой беременности')) 'JSON metadata leaked GM knowledge'
}
Step 'Branch includes shared events and respects budget' {
    $n=Get-VoidlightContext -Root $testRoot -Branch 'Никитосиос' -MaxWords 1000
    Assert-True (@($n.scene_ids).Count -ge 2) 'Shared scene not selected'
    Assert-True ([regex]::Matches($n.text,'\S+').Count -le 1000) 'Word limit exceeded'
    $n=Get-VoidlightContext -Root $testRoot -Branch 'Никитосиос' -Query 'перемирие'
    Assert-True $n.text.Contains('Q-WORLD-046') 'Branch mission lost'
}
Step 'Closing decision preserves prose and stable identity' {
    $hot=Join-Path $testRoot '01_Кампания/00_Текущий_контекст.md'
    $text=Read-VoidlightText $hot
    $hotOriginal=$text
    $decisionRegistryPath=Join-Path $testRoot '09_Реестры/Решения.json'
    $decisionRegistryOriginal=Read-VoidlightText $decisionRegistryPath
    $text += @('','## Проверка абзаца','','Сохрани помилование и доклад Аругала. DEC-PENDING-137 остаётся в этом абзаце.','') -join [char]10
    Write-VoidlightText $hot $text
    $before=@((Read-VoidlightJson (Join-Path $testRoot '09_Реестры/Решения.json')).decisions|Where-Object {$_.id -eq 'DEC-PENDING-137'})[0]
    & (Join-Path $testRoot 'tools/Закрыть_решение.ps1') -PendingId DEC-PENDING-137 -AcceptedId DEC-900 -Choice 'Тестовый выбор' -Effect 'Только временная копия' -SkipCheck|Out-Null
    $after=Read-VoidlightText $hot
    # The current context is a generated view: the closing command leaves it to Собрать_контекст.
    Assert-True ($after -ceq $text) 'Closing a decision edited the generated current context'
    $d=@((Read-VoidlightJson (Join-Path $testRoot '09_Реестры/Решения.json')).decisions|Where-Object {$_.id -eq 'DEC-900'})[0]
    Assert-True ($d.uid -eq $before.uid -and $d.resolved_from -eq 'DEC-PENDING-137') 'Decision identity was lost'
    & (Join-Path $testRoot 'tools/Закрыть_решение.ps1') -PendingId DEC-PENDING-137 -AcceptedId DEC-900 -Choice 'Тестовый выбор' -Effect 'Только временная копия' -SkipCheck|Out-Null
    # This is a mutator regression, not a new canonical player choice. Restore
    # fixture inputs before the independent end-to-end intake scenario below.
    Write-VoidlightText $decisionRegistryPath $decisionRegistryOriginal
    Write-VoidlightText $hot $hotOriginal
}
Step 'Repeated incoming creates exactly one scene in the current chapter' {
    $sceneOptions=@{Branch='Тест_v2';Title='Повторяемое сообщение';Text='Технический текст только для тестовой копии.';SkipCheck=$true}
    & (Join-Path $testRoot 'tools/Сцена_из_входящего.ps1') @sceneOptions|Out-Null
    & (Join-Path $testRoot 'tools/Сцена_из_входящего.ps1') @sceneOptions|Out-Null
    $scenes=@(Get-ChildItem -LiteralPath (Join-Path $testRoot '01_Кампания/Ветки/Тест_v2') -Filter 'Сцена*.md')
    Assert-True ($scenes.Count -eq 1) 'Repeated input created duplicate scenes'
    Assert-True ((Get-VoidlightMeta (Read-VoidlightText $scenes[0].FullName) 'chapter') -eq [string](Get-VoidlightCurrentChapter $testRoot)) 'Wrong chapter default'
    Assert-True (@(Get-VoidlightArrayMeta (Read-VoidlightText $scenes[0].FullName) 'source_ids').Count -eq 1) 'Source association missing'
}
Step 'Intake recovers after source and inbox writes before receipt save' {
    $options=@{Title='Восстановление входящего';Text='Сообщение для проверки прерванной записи.';Mode='source';RequestId='MSG-recovery';SkipCheck=$true}
    & (Join-Path $testRoot 'tools/Принять_сообщение.ps1') @options|Out-Null
    $p=Join-Path $testRoot '09_Реестры/Входящие.json';$v=Read-VoidlightJson $p
    $v.receipts=@($v.receipts|Where-Object {$_.request_id -ne 'MSG-recovery'});Write-VoidlightJson $p $v
    & (Join-Path $testRoot 'tools/Принять_сообщение.ps1') @options|Out-Null
    $inbox=Read-VoidlightText (Join-Path $testRoot '07_Черновики_и_идеи/Входящие_сообщения.md')
    Assert-True ([regex]::Matches($inbox,'Request-ID: MSG-recovery').Count -eq 1) 'Interrupted intake duplicated message'
    Assert-True ($null -ne (Get-VoidlightReceipt $testRoot 'MSG-recovery')) 'Receipt was not recovered'
}
Step 'Character packet of an NPC works without a branch as a minimal filter' {
    $npc=Get-VoidlightContext -Root $testRoot -Entity CHAR-0140 -Audience character -MaxWords 3000
    Assert-True ($npc.text.Contains('Минимальный фильтр') -and $npc.text.Contains('Другие сцены с участием')) 'NPC packet lost its scene list or its limitation note'
    Assert-True (-not $npc.text.Contains('DEC-PENDING') -and -not $npc.text.Contains('Q-C')) 'Registry IDs leaked to a character packet'
    Expect-Failure {Get-VoidlightContext -Root $testRoot -Entity LOC-0094 -Audience character} 'A location was accepted as a viewer'
    Expect-Failure {Get-VoidlightContext -Root $testRoot -Branch 'Александрос' -Entity CHAR-0136 -Audience character} 'Branch and entity of different characters accepted'
    Expect-Failure {Get-VoidlightContext -Root $testRoot -Audience character} 'Character packet without a viewer accepted'
}
Step 'Own branch scenes give the character what he knows, other scenes only a list' {
    $a=Get-VoidlightContext -Root $testRoot -Branch 'Александрос' -Audience character -MaxWords 6000
    $own=@($a.included|Where-Object {$_.reason -like '*participant_ids*' -and $_.path -like '01_Кампания/Ветки/Александрос/*'})
    Assert-True ($own.Count -ge 1) 'No own scene in the character packet'
    Assert-True (@($a.scene_ids|Where-Object {$_ -eq 'SCENE-0106' -or $_ -eq 'SCENE-0117'}).Count -eq 0) 'Another perspective entered as knowledge'
    Assert-True (-not $a.text.Contains('предполагаемой беременности') -and -not $a.text.Contains('DEC-PENDING')) 'Participant filter leaked another perspective'
    Assert-True ($a.text.Contains('Другие сцены с участием персонажа')) 'Participation outside the branch is not listed'
}
Step 'Merged duplicates answer for the main card' {
    $data=New-VoidlightReadModel $testRoot
    Assert-True ((Resolve-VoidlightEntity $data 'CHAR-0058').id -eq 'CHAR-0057' -and (Resolve-VoidlightEntity $data 'Закхерон').id -eq 'CHAR-0057') 'Zakheron redirect lost'
    Assert-True ((Resolve-VoidlightEntity $data 'LOC-0099').id -eq 'LOC-0094' -and (Resolve-VoidlightEntity $data 'Цитадель Света').id -eq 'LOC-0094') 'Citadel redirect lost'
    $context=Get-VoidlightContext -Root $testRoot -Entity CHAR-0058 -MaxWords 800 -Data $data
    Assert-True ($context.included[0].id -eq 'CHAR-0057') 'Context of a merged ID did not open the main card'
    Assert-True (@($data.scenes_by_participant['CHAR-0058']|Where-Object {$_}).Count -eq 0 -and @($data.graph.entities|Where-Object {$_.participant_ids -contains 'CHAR-0058'}).Count -eq 0) 'A scene still names the redirect'
}
Step 'New characters of a batch are visible to the next commands' {
    $first=& (Join-Path $testRoot 'tools/Новый_персонаж.ps1') -Name 'Сценарный Свидетель' -Role 'тест сценариев' -SkipCheck
    $second=& (Join-Path $testRoot 'tools/Новый_персонаж.ps1') -Name 'Сценарный Двойник' -Role 'тест сценариев' -SkipCheck
    $script:witness=([regex]::Match(($first -join "`n"),'\((CHAR-\d{4})\)')).Groups[1].Value
    $script:double=([regex]::Match(($second -join "`n"),'\((CHAR-\d{4})\)')).Groups[1].Value
    Assert-True ($script:witness -and $script:double -and $script:witness -ne $script:double) 'New character IDs were not reported'
    $output=& (Join-Path $testRoot 'tools/Новая_сцена.ps1') -Branch 'Тест_слияния' -Title 'Свидетель и двойник' -Status active -ParticipantIds @($script:witness,$script:double) -SkipCheck
    $script:mergeScene=([regex]::Match(($output -join "`n"),'(?m)^Created scene:\s*(.+?)\s*$')).Groups[1].Value
    $sceneId=Get-VoidlightMeta (Read-VoidlightText (Join-Path $testRoot $script:mergeScene)) 'id'
    & (Join-Path $testRoot 'tools/Новое_знание.ps1') -Text 'Двойник видел свидетеля.' -Truth confirmed -EvidenceIds $sceneId -SubjectIds $script:witness -KnownTo $script:double -KnowledgeEvidence "$($script:double)|$sceneId|Присутствовал в сцене." -SkipCheck|Out-Null
}
Step 'Closing a chapter closes and protects its scenes and opens the next one' {
    $chapterFile=@(Get-ChildItem -LiteralPath (Join-Path $testRoot '01_Кампания/Главы') -Filter 'Глава_04_*.md')[0]
    $archive=Join-Path $testRoot ('06_Архив_канона/Завершенные_главы/'+$chapterFile.Name)
    Write-VoidlightText $archive ((@('# Глава 4. Врата преисподней','','---','type: archived_chapter','chapter: 4','status: closed','canon_level: canon_locked','date_in_story: тест','source_chapter: 01_Кампания/Главы/'+$chapterFile.Name,'---','','## Итог','','Технический итог только для тестовой копии.') -join "`n")+"`n")
    Expect-Failure {& (Join-Path $testRoot 'tools/Закрыть_главу.ps1') -Chapter 3 -NextTitle 'Ошибка' -NextDateInStory 'тест' -SkipCheck} 'A closed chapter was closed again'
    $drafts=@(Get-ChildItem -LiteralPath (Join-Path $testRoot '01_Кампания/Ветки') -Recurse -Filter '*.md'|Where-Object {$text=Read-VoidlightText $_.FullName;(Get-VoidlightMeta $text 'type') -eq 'scene' -and (Get-VoidlightMeta $text 'chapter') -eq '4' -and (Get-VoidlightMeta $text 'status') -eq 'draft'})
    Assert-True ($drafts.Count -ge 1) 'Fixture has no draft scene to block the close'
    $message=Expect-Failure {& (Join-Path $testRoot 'tools/Закрыть_главу.ps1') -Chapter 4 -NextTitle 'Тестовая глава' -NextDateInStory 'после теста' -SkipCheck} 'A draft scene did not block the close' -PassThru
    Assert-True ($message.Contains('draft')) "Unexpected refusal: $message"
    foreach($draft in $drafts){Write-VoidlightText $draft.FullName (Set-VoidlightMeta (Read-VoidlightText $draft.FullName) 'status' 'active')}
    & (Join-Path $testRoot 'tools/Закрыть_главу.ps1') -Chapter 4 -NextTitle 'Тестовая глава' -NextDateInStory 'после теста' -SkipCheck|Out-Null
    Assert-True ((Get-VoidlightCurrentChapter $testRoot) -eq 5) 'Next chapter is not active'
    Assert-True ((Read-VoidlightJson (Join-Path $testRoot '09_Реестры/Контекст.json')).chapter_file -like '*Глава_05_*') 'Context still points at the closed chapter'
    Assert-True ((Read-VoidlightJson (Join-Path $testRoot '09_Реестры/Вопросы.json')).current_chapter -eq 5) 'Question chapter was not moved'
    Assert-True ((Get-VoidlightMeta (Read-VoidlightText (Join-Path $testRoot $script:mergeScene)) 'status') -eq 'closed') 'Scene of the closed chapter is still open'
    $protected=@((Read-VoidlightJson (Join-Path $testRoot '10_Обслуживание/Миграция_v2.json')).protected_files.path)
    Assert-True ($protected -contains $script:mergeScene -and $protected -contains ('01_Кампания/Главы/'+$chapterFile.Name)) 'Closed chapter is not protected'
    Expect-Failure {& (Join-Path $testRoot 'tools/Новая_сцена.ps1') -Branch 'Тест_слияния' -Title 'Сцена закрытой главы' -Chapter 4 -SkipCheck} 'A scene was added to a closed chapter'
}
Step 'Merging moves reference fields and records protected scene changes' {
    $output=& (Join-Path $testRoot 'tools/Объединить_сущности.ps1') -From $script:double -Into $script:witness -Reason 'Тест слияния' -SkipCheck
    Assert-True (($output -join "`n").Contains("Merged entity: $($script:double) -> $($script:witness)")) 'Merge did not report'
    $sceneText=Read-VoidlightText (Join-Path $testRoot $script:mergeScene)
    Assert-True ((@(Get-VoidlightArrayMeta $sceneText 'participant_ids') -join ',') -eq $script:witness) 'Scene participant was not replaced'
    $fact=@((Read-VoidlightJson (Join-Path $testRoot '09_Реестры/Знания.json')).facts|Where-Object {$_.text -eq 'Двойник видел свидетеля.'})[0]
    Assert-True (@($fact.known_to) -contains $script:witness -and @($fact.knowledge_evidence.character_id) -contains $script:witness) 'Knowledge holder was not replaced'
    $redirect=Read-VoidlightText (Join-Path $testRoot '03_Персонажи/Сценарный_Двойник.md')
    Assert-True ((Get-VoidlightMeta $redirect 'merged_into') -eq $script:witness -and (Get-VoidlightMeta $redirect 'status') -eq 'merged' -and (Get-VoidlightMeta $redirect 'id') -eq $script:double) 'Redirect card is wrong'
    $main=Read-VoidlightText (Join-Path $testRoot '03_Персонажи/Сценарный_Свидетель.md')
    Assert-True (@(Get-VoidlightArrayMeta $main 'aliases') -contains 'Сценарный Двойник') 'Title of the merged card is not an alias of the main card'
    $manifest=Read-VoidlightJson (Join-Path $testRoot '10_Обслуживание/Миграция_v2.json')
    Assert-True (@($manifest.amendments|Where-Object {$_.path -eq $script:mergeScene -and $_.reason -like '*Объединение*'}).Count -eq 1) 'Protected scene change was not recorded'
    Assert-True (@($manifest.history|Where-Object {$_.original_path -eq '03_Персонажи/Сценарный_Свидетель.md'}).Count -eq 1) 'Old text was not kept as history'
    Expect-Failure {& (Join-Path $testRoot 'tools/Новая_сцена.ps1') -Branch 'Тест_слияния' -Title 'Ссылка на перенаправление' -ParticipantIds $script:double -SkipCheck} 'A merged ID was accepted as a participant'
}
Step 'Full rebuild of modified fixture' {
    Invoke-VoidlightStageValidation $testRoot -Build
}
Step 'Knowledge and branch tools reject unknown evidence without writes' {
    $path=Join-Path $testRoot '09_Реестры/Знания.json';$before=Read-VoidlightText $path
    Expect-Failure {& (Join-Path $testRoot 'tools/Новое_знание.ps1') -Text 'Тест' -Truth reported -EvidenceIds SCENE-unknown -SkipCheck} 'Unknown knowledge evidence accepted'
    Assert-True ((Read-VoidlightText $path) -ceq $before) 'Invalid knowledge changed registry'
    $path=Join-Path $testRoot '09_Реестры/Контекст.json';$before=Read-VoidlightText $path
    Expect-Failure {& (Join-Path $testRoot 'tools/Обновить_контекст_ветки.ps1') -Branch 'Михаэль' -FocusIds Q-WORLD-999 -SkipCheck} 'Unknown focus accepted'
    Assert-True ((Read-VoidlightText $path) -ceq $before) 'Invalid branch update changed registry'
}
Step 'Validation detects stale branch and cyclic chronology' {
    $profile=Join-Path $testRoot '01_Кампания/Ветки/Александрос/00_Профиль_ветки.md';$original=Read-VoidlightText $profile
    Write-VoidlightText $profile (Set-VoidlightMeta $original 'current_chapter' '3')
    $r=& (Join-Path $testRoot 'tools/Проверить_контекст.ps1') -AsObject
    Assert-True (@($r.Errors|Where-Object {$_ -like '*Stale chapter*'}).Count -gt 0) 'Stale branch passed'
    Write-VoidlightText $profile $original
    $p=Join-Path $testRoot '09_Реестры/Хронология_связей.json';$original=Read-VoidlightText $p;$v=Read-VoidlightJson $p
    $v.relations += [pscustomobject]@{from=$v.relations[0].to;to=$v.relations[0].from;relation='after';evidence_ids=$v.relations[0].evidence_ids;precision='relative'}
    Write-VoidlightJson $p $v
    $r=& (Join-Path $testRoot 'tools/Проверить_контекст.ps1') -AsObject
    Assert-True (@($r.Errors|Where-Object {$_ -like '*Cyclic event*'}).Count -gt 0) 'Temporal cycle passed'
    Write-VoidlightText $p $original
}
Step 'Prepared turn rejects edits made after preparation' {
    Start-VoidlightTurn -Root $testRoot -Id TURN-test-recovery -Title 'Техническая транзакция'|Out-Null
    $turn=Get-VoidlightTurnPath $testRoot TURN-test-recovery
    $stage=Join-Path $turn 'stage'
    $probe=Join-Path $stage '10_Обслуживание/Тест.txt'
    Write-VoidlightText $probe 'Prepared content'
    Prepare-VoidlightTurn -Root $testRoot -Id TURN-test-recovery|Out-Null
    Write-VoidlightText $probe 'Changed after validation'
    Expect-Failure {Apply-VoidlightTurn -Root $testRoot -Id TURN-test-recovery} 'Modified stage was applied'
    Write-VoidlightText $probe 'Prepared content'
    Write-VoidlightText (Join-Path $testRoot '10_Обслуживание/Тест.txt') 'Concurrent user content'
    Expect-Failure {Apply-VoidlightTurn -Root $testRoot -Id TURN-test-recovery} 'Concurrent user edit was overwritten'
    [IO.File]::Delete((Resolve-VoidlightPath $testRoot '10_Обслуживание/Тест.txt'))
}
Step 'Interrupted apply resumes once and restores its own changes' {
    Expect-Failure {Apply-VoidlightTurn -Root $testRoot -Id TURN-test-recovery -FailAfter 1} 'Interruption was not simulated'
    $r=Apply-VoidlightTurn -Root $testRoot -Id TURN-test-recovery
    Assert-True ($r.status -eq 'committed') 'Recovery did not finish'
    $again=Apply-VoidlightTurn -Root $testRoot -Id TURN-test-recovery
    Assert-True ($again.status -eq 'committed') 'Repeat apply is not idempotent'
    $restored=Undo-VoidlightTurn -Root $testRoot -Id TURN-test-recovery
    Assert-True ($restored.status -eq 'restored') 'Restore failed'
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $testRoot '10_Обслуживание/Тест.txt'))) 'New test file was not removed on restoration'
}
Step 'Finished turn copies are listed and cleaned' {
    $turns=@(Get-VoidlightTurns $testRoot -Size)
    Assert-True (@($turns|Where-Object {$_.id -eq 'TURN-test-recovery' -and $_.status -eq 'restored' -and $_.size_mb -gt 0}).Count -eq 1) 'Restored turn is missing from the list'
    Assert-True (@(Get-VoidlightTurnProblems $testRoot).Count -eq 0) 'A finished turn was reported as unfinished'
    $removed=@(Remove-VoidlightFinishedTurns $testRoot)
    Assert-True (@($removed.id) -contains 'TURN-test-recovery' -and -not (Test-Path -LiteralPath (Get-VoidlightTurnPath $testRoot TURN-test-recovery))) 'Finished turn copy was not removed'
}
Step 'Paths cannot escape project' {
    Expect-Failure {Resolve-VoidlightPath $testRoot '../outside.txt'} 'Traversal accepted'
}
Complete-VoidlightTestRun $testRoot -KeepFixture:$KeepFixture
