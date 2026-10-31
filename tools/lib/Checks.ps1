# Shared validation primitives: the problem set and report, ID set comparison,
# legacy paths of the old layout and portrait file checks.

# The problem set of the running check lives in its script scope, so Add-Problem needs
# no explicit argument. A nested check (& tools/Проверить_*.ps1) keeps its own set.
function New-VoidlightProblemSet {
    $script:VoidlightProblems=[pscustomobject]@{Errors=[Collections.Generic.List[string]]::new();Warnings=[Collections.Generic.List[string]]::new()}
    return $script:VoidlightProblems
}

function Add-Problem {
    param([string]$Kind,[string]$Message)
    if($Kind -eq 'Error'){$script:VoidlightProblems.Errors.Add($Message)}else{$script:VoidlightProblems.Warnings.Add($Message)}
}

# Summary, then warnings and errors in alphabetical order. Does not stop the script.
function Write-VoidlightProblemReport {
    param([object]$Summary)
    $problems=$script:VoidlightProblems
    if($null -ne $Summary){$Summary|Format-List}
    if($problems.Warnings.Count -gt 0){"`nWarnings:";$problems.Warnings|Sort-Object|ForEach-Object {"- $_"}}
    if($problems.Errors.Count -gt 0){"`nErrors:";$problems.Errors|Sort-Object|ForEach-Object {"- $_"}}
}

# Final report of a standalone check: exit code 1 on errors, otherwise the success message.
function Complete-VoidlightCheck {
    param([object]$Summary,[string]$SuccessMessage)
    Write-VoidlightProblemReport $Summary
    if($script:VoidlightProblems.Errors.Count -gt 0){exit 1}
    if($SuccessMessage){"`n$SuccessMessage"}
}

function Compare-IdSets {
    param([string]$LeftName,[string[]]$LeftIds,[string]$RightName,[string[]]$RightIds)
    $leftUnique=@($LeftIds|Sort-Object -Unique)
    $rightUnique=@($RightIds|Sort-Object -Unique)
    foreach($id in $leftUnique){if($rightUnique -notcontains $id){Add-Problem Error "$LeftName contains $id but $RightName does not."}}
    foreach($id in $rightUnique){if($leftUnique -notcontains $id){Add-Problem Error "$RightName contains $id but $LeftName does not."}}
}

# Files and folders of the old project layout. None may exist, and no text may refer to
# them with either path separator. This file is excluded from the reference scan.
function Get-VoidlightLegacyPaths {
    @(
        '00_НАЧАТЬ_ОТСЮДА.md'
        '00_ТЕКУЩИЙ_КОНТЕКСТ_ДЛЯ_ИИ.md'
        '00_Словарь_имен_и_алиасов.md'
        '05_Текстовые_описания'
        '09_Шаблоны/Шаблон_текстового_описания.md'
        '10_ПРОСТОЙ_РЕЖИМ_ДЛЯ_ТЕБЯ.md'
        '11_Медиа/Иллюстрации_сцен'
        '11_Медиа/Карты_и_схемы'
        '11_Медиа/Портреты_персонажей/Правила_генерации_портретов.md'
        'AI_ПРОМПТ_ДЛЯ_ВЕДЕНИЯ_ИГРЫ.md'
        'AI_ПРОМПТ_ДЛЯ_ФОТО.md'
        'Инструкция_собрать_все_MD_в_одну_папку.md'
    )
}

function Get-VoidlightLegacyReferences {
    foreach($path in Get-VoidlightLegacyPaths){
        # Only the root-level dictionary is legacy; 03_Персонажи/00_Словарь_имен_и_алиасов.md is current.
        if($path -eq '00_Словарь_имен_и_алиасов.md'){continue}
        $path
        if($path.Contains('/')){$path.Replace('/','\')}
    }
}

# Portrait proportions: 3:4 within 0.04; -AllowLegacy also accepts the old 2:3 portraits.
function Test-VoidlightPortraitRatio {
    param([int]$Width,[int]$Height,[switch]$AllowLegacy)
    if($Width -le 0 -or $Height -le 0 -or $Width -ge $Height){return $false}
    $ratio=$Width/$Height
    return ([Math]::Abs($ratio-0.75) -le 0.04 -or ($AllowLegacy -and [Math]::Abs($ratio-(2/3)) -le 0.04))
}

# Portrait file of a card with portrait_status=available. Problem is missing (no file),
# unreadable (no image size), ratio (neither 3:4 nor 2:3) or empty. -CardPath also resolves
# the path from the card's folder. Each caller keeps its own severity per problem.
function Test-VoidlightPortraitFile {
    param([string]$Root,[string]$Portrait,[string]$CardPath)
    $path=if($Portrait -ne 'null'){Resolve-VoidlightReference $Root $Portrait -FromFile $CardPath}
    if(-not $path){return [pscustomobject]@{Problem='missing';Path=$null;Size=$null}}
    $size=Get-VoidlightImageSize $path
    $problem=if(-not $size){'unreadable'}elseif(-not (Test-VoidlightPortraitRatio $size.Width $size.Height -AllowLegacy)){'ratio'}else{''}
    return [pscustomobject]@{Problem=$problem;Path=$path;Size=$size}
}

# Portrait folders (direct children of the portrait root) that contain an image.
function Get-VoidlightPortraitImageFolders {
    param([string]$PortraitRoot)
    Get-ChildItem -LiteralPath $PortraitRoot -Directory|Where-Object {
        @(Get-ChildItem -LiteralPath $_.FullName -File|Where-Object {$_.Extension -match '^\.(jpg|jpeg|png|webp)$'}).Count -gt 0
    }
}

# --- Canon checks shared by Проверить_проект, Проверить_контекст and the scenario suites --
# Each returns problems as objects {kind = Error | Warning; message}.

function New-VoidlightProblem([string]$Kind,[string]$Message){[pscustomobject]@{kind=$Kind;message=$Message}}

# ID format and the prefix that fits the document type (CHAR, LOC, ASSET, SCENE, SRC, LORE).
function Get-VoidlightEntityIdProblems {
    param([object]$Graph)
    foreach($entity in $Graph.entities){
        $problem=Test-VoidlightEntityIdFormat $entity.id $entity.type
        if($problem){New-VoidlightProblem Error "$problem ($($entity.path))"}
    }
}

# A merged card stays as a redirect; nothing may point at it directly.
function Get-VoidlightMergedReferenceProblems {
    param([object]$Graph,[object]$State,[object]$Knowledge,[object]$Decisions,[object]$Chronology)
    $byId=@{};foreach($entity in $Graph.entities){$byId[$entity.id]=$entity}
    $merged=@{}
    foreach($entity in $Graph.entities){
        if(-not $entity.merged_into){continue}
        $target=$entity.merged_into
        if($target -ceq $entity.id){New-VoidlightProblem Error "Entity is merged into itself: $($entity.id)";continue}
        if(-not $byId.ContainsKey($target)){New-VoidlightProblem Error "Merged entity points to an unknown card: $($entity.id) -> $target";continue}
        if($byId[$target].merged_into){New-VoidlightProblem Error "Merged entity points to another redirect: $($entity.id) -> $target; point it at the main card."}
        if($byId[$target].type -ne $entity.type){New-VoidlightProblem Error "Merged entity changes type: $($entity.id) ($($entity.type)) -> $target ($($byId[$target].type))"}
        if($entity.status -ne 'merged'){New-VoidlightProblem Error "Merged entity must have status merged: $($entity.path)"}
        $merged[$entity.id]=$target
    }
    foreach($entity in $Graph.entities){if($entity.status -eq 'merged' -and -not $entity.merged_into){New-VoidlightProblem Error "Status merged needs merged_into: $($entity.path)"}}
    if(-not $merged.Count){return}
    function Test-Ids([object[]]$Ids,[string]$Where){
        foreach($id in @($Ids|Where-Object {$_})){
            if($merged.ContainsKey([string]$id)){New-VoidlightProblem Error "Reference to merged entity: $Where -> $id (use $($merged[[string]$id]))"}
        }
    }
    foreach($entity in $Graph.entities){
        Test-Ids @($entity.participant_ids) "$($entity.path) participant_ids"
        Test-Ids @($entity.distinct_from) "$($entity.path) distinct_from"
    }
    foreach($edge in @($Graph.edges)){if($edge.kind -eq 'sourced_from'){Test-Ids @($edge.to) "$($edge.evidence) source_ids"}}
    foreach($fact in @($Knowledge.facts)){
        Test-Ids (@($fact.subject_ids)+@($fact.known_to)+@($fact.evidence_ids)+@($fact.reported_by)) "09_Реестры/Знания.json $($fact.id)"
        foreach($record in @($fact.knowledge_evidence|Where-Object {$_})){Test-Ids (@($record.character_id)+@($record.evidence_ids)) "09_Реестры/Знания.json $($fact.id) knowledge_evidence"}
    }
    Test-Ids @($State.scene_ids) '09_Реестры/Контекст.json scene_ids'
    foreach($branch in @($State.branches)){Test-Ids (@($branch.character_id)+@($branch.scene_ids)) "09_Реестры/Контекст.json branch $($branch.name)"}
    foreach($decision in @($Decisions.decisions)){Test-Ids @($decision.scene_ids) "09_Реестры/Решения.json $($decision.id)"}
    foreach($relation in @($Chronology.relations|Where-Object {$_})){Test-Ids (@($relation.from)+@($relation.to)+@($relation.evidence_ids)) '09_Реестры/Хронология_связей.json'}
}

# The character status dictionary: values, the alive flag and plain synonyms.
function Get-VoidlightCharacterStatuses {
    param([string]$Root)
    $path=Join-Path $Root '09_Реестры/Статусы_персонажей.json'
    if(-not (Test-Path -LiteralPath $path)){throw 'Character status dictionary is missing: 09_Реестры/Статусы_персонажей.json'}
    $dictionary=Read-VoidlightJson $path
    $values=@{};foreach($status in @($dictionary.statuses)){$values[[string]$status.value]=$status}
    $synonyms=@{};foreach($property in @($dictionary.synonyms.PSObject.Properties)){$synonyms[$property.Name]=[string]$property.Value}
    return [pscustomobject]@{values=$values;synonyms=$synonyms}
}

function Get-VoidlightCharacterStatusProblems {
    param([string]$Root,[object]$Graph)
    $statuses=Get-VoidlightCharacterStatuses $Root
    foreach($entity in @($Graph.entities|Where-Object type -eq 'character')){
        $status=[string]$entity.status
        if($statuses.values.ContainsKey($status)){continue}
        if($statuses.synonyms.ContainsKey($status)){New-VoidlightProblem Error "Character status '$status' is a synonym; write '$($statuses.synonyms[$status])': $($entity.path)";continue}
        New-VoidlightProblem Error "Character status '$status' is not in 09_Реестры/Статусы_персонажей.json: $($entity.path)"
    }
}

# Titles and aliases that may name one entity twice; acknowledged pairs carry distinct_from.
function Get-VoidlightDuplicateNameProblems {
    param([object]$Graph)
    foreach($type in @('character','location','character_asset','lore')){
        $entities=@($Graph.entities|Where-Object type -eq $type)
        foreach($pair in @(Get-VoidlightSimilarNamePairs $entities)){
            New-VoidlightProblem Warning "Possible duplicate ${type}: $($pair.ids -join ' / ') ('$($pair.left)' ~ '$($pair.right)'). Merge with Объединить_сущности or record distinct_from in one card and the name dictionary."
        }
    }
}

# Scenes of a closed chapter are closed and protected by hash in the manifest.
function Get-VoidlightClosedSceneProblems {
    param([string]$Root,[object]$Graph)
    $closed=@{}
    foreach($file in @(Get-ChildItem -LiteralPath (Join-Path $Root '01_Кампания/Главы') -File -Filter '*.md')){
        $text=Read-VoidlightText $file.FullName
        if((Get-VoidlightMeta $text 'status') -eq 'closed'){$closed[[string](Get-VoidlightMeta $text 'chapter')]=$true}
    }
    $manifestPath=Get-VoidlightProtectionManifestPath $Root
    $protected=@{}
    if(Test-Path -LiteralPath $manifestPath){foreach($item in @((Read-VoidlightJson $manifestPath).protected_files)){$protected[$item.path]=$true}}
    foreach($scene in @($Graph.entities|Where-Object {$_.type -eq 'scene' -and $closed.ContainsKey([string]$_.chapter)})){
        if($scene.status -ne 'closed'){New-VoidlightProblem Error "Scene of closed chapter $($scene.chapter) must have status closed: $($scene.path)"}
        if(-not $protected.ContainsKey($scene.path)){New-VoidlightProblem Error "Scene of closed chapter $($scene.chapter) is not protected: $($scene.path). Use Обновить_защиту_канона -Add with a reason."}
    }
}

# Formulations of closed forks that must not return to live context (data file).
function Get-VoidlightStaleContextMarkers {
    param([string]$Root)
    $path=Join-Path $Root '09_Реестры/Маркеры_контекста.json'
    if(-not (Test-Path -LiteralPath $path)){return @()}
    return @((Read-VoidlightJson $path).markers|Where-Object {$_})
}

# Records of Regnum worlds (12_Карты/<world>/data, format: tools/Regnum/docs/FORMAT.md)
# that link a campaign card through their entity field. Read only.
function Get-VoidlightRegnumEntityLinks {
    param([string]$Root,[string]$World='')
    $mapsRoot=Join-Path $Root '12_Карты'
    $worlds=if($World){@((Resolve-Path -LiteralPath $World).Path)}elseif(Test-Path -LiteralPath $mapsRoot){
        @(Get-ChildItem -LiteralPath $mapsRoot -Directory|Where-Object {Test-Path -LiteralPath (Join-Path $_.FullName 'world.json')}|ForEach-Object {$_.FullName})
    }else{@()}
    foreach($worldDir in $worlds){
        $data=Join-Path $worldDir 'data'
        $label=Get-VoidlightRelativePath $Root $worldDir
        $read={param([string]$File,[string]$Field)
            $path=Join-Path $data $File
            if(Test-Path -LiteralPath $path){@((Read-VoidlightJson $path).$Field|Where-Object {$_})}
        }
        $dead=@(& $read 'modifiers.json' 'modifiers'|Where-Object {$_.key -eq 'dead' -or (-not $_.key -and $_.name -eq 'Мертв')}|ForEach-Object {[string]$_.id})
        foreach($record in @(& $read 'provinces.json' 'provinces')){
            if($record.entity){[pscustomobject]@{world=$label;table='provinces';id=$record.id;name=$record.name;kind='province';entity=[string]$record.entity;dead=$null}}
        }
        foreach($record in @(& $read 'factions.json' 'factions')){
            if($record.entity){[pscustomobject]@{world=$label;table='factions';id=$record.id;name=$record.name;kind=$(if($record.kind -eq 'guild'){'guild'}else{'state'});entity=[string]$record.entity;dead=$null}}
        }
        foreach($record in @(& $read 'characters.json' 'characters')){
            if($record.entity){[pscustomobject]@{world=$label;table='characters';id=$record.id;name=$record.name;kind='character';entity=[string]$record.entity;dead=[bool](@($record.modifiers|Where-Object {$dead -contains [string]$_}).Count)}}
        }
        foreach($record in @(& $read 'catalogs.json' 'relics')){
            if($record.entity){[pscustomobject]@{world=$label;table='catalogs.relics';id=$record.id;name=$record.name;kind='relic';entity=[string]$record.entity;dead=$null}}
        }
    }
}

# Links of Regnum worlds against the canon: an existing, not merged card with the prefix of
# the record kind (province or state: LOC; guild: LOC, ASSET or LORE; hero: CHAR; relic:
# ASSET), and the «Мертв» modifier of a hero against the alive flag of the card status.
function Test-VoidlightRegnumLinks {
    param([string]$Root,[object]$Graph,[string]$World='')
    $byId=@{};foreach($entity in $Graph.entities){$byId[$entity.id]=$entity}
    $statuses=Get-VoidlightCharacterStatuses $Root
    $expected=@{province=@('LOC');state=@('LOC');guild=@('LOC','ASSET','LORE');character=@('CHAR');relic=@('ASSET')}
    $links=@(Get-VoidlightRegnumEntityLinks -Root $Root -World $World)
    $problems=[Collections.Generic.List[object]]::new()
    foreach($link in $links){
        $label="$($link.world): $($link.table) $($link.id) «$($link.name)» -> $($link.entity)"
        if(-not $byId.ContainsKey($link.entity)){$problems.Add((New-VoidlightProblem Error "${label}: no such campaign card"));continue}
        $card=$byId[$link.entity]
        if($card.merged_into){$problems.Add((New-VoidlightProblem Error "${label}: the card is merged into $($card.merged_into); relink the record"));continue}
        $prefix=($link.entity -split '-')[0]
        if($expected[$link.kind] -notcontains $prefix){$problems.Add((New-VoidlightProblem Error "${label}: a $($link.kind) links $($expected[$link.kind] -join ' or ') cards, not $prefix"));continue}
        if($link.kind -ne 'character'){continue}
        $value=[string]$card.status
        if($statuses.synonyms.ContainsKey($value)){$value=$statuses.synonyms[$value]}
        $alive=if($statuses.values.ContainsKey($value)){$statuses.values[$value].alive}else{$null}
        if($link.dead -and $alive -eq $true){$problems.Add((New-VoidlightProblem Warning "${label}: the hero has «Мертв», but the card status is $($card.status)"))}
        elseif(-not $link.dead -and $alive -eq $false){$problems.Add((New-VoidlightProblem Warning "${label}: the card status is $($card.status), but the hero has no «Мертв»"))}
    }
    return [pscustomobject]@{links=$links.Count;problems=@($problems)}
}
