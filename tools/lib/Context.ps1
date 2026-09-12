function Get-WmmaSelection {
    param([string]$Root, [string]$Branch = '', [string]$Query = '')
    $questions = Read-WmmaJson (Join-Path $Root '09_Реестры/Вопросы.json')
    $decisions = Read-WmmaJson (Join-Path $Root '09_Реестры/Решения.json')
    $fronts = Read-WmmaJson (Join-Path $Root '09_Реестры/Фронты.json')
    $chapter = Get-WmmaCurrentChapter $Root
    $statePath = Join-Path $Root '09_Реестры/Контекст.json'
    $state = if (Test-Path -LiteralPath $statePath) { Read-WmmaJson $statePath } else { $null }
    $branchState = @($state.branches | Where-Object { $_.name -eq $Branch -or $_.character_id -eq $Branch }) | Select-Object -First 1
    if ($Branch -and -not $branchState) { throw "Unknown branch: $Branch" }
    $focusIds = @($state.focus_ids)
    if ($branchState) { $focusIds += @($branchState.focus_ids) }
    $terms = @($Query -split '\s+' | Where-Object { $_.Length -ge 3 })
    $rank = {
        $item = $_; $score = $(switch($item.priority){'критический'{400};'высокий'{300};'средний'{200};default{100}})
        if ($focusIds -contains $item.id) { $score += 1000 }
        if ($branchState -and ([string]$item.owner).Contains($branchState.name)) { $score += 300 }
        $haystack = $item | ConvertTo-Json -Depth 8 -Compress
        foreach ($term in $terms) { if ($haystack.IndexOf($term, [StringComparison]::OrdinalIgnoreCase) -ge 0) { $score += 150 } }
        if ($item.id -like ('Q-C' + $chapter + '-*')) { $score += 50 }
        $score
    }.GetNewClosure()
    $sort = @(@{Expression=$rank;Descending=$true}, @{Expression={$_.id};Descending=$false})
    $qs = @($questions.questions | Where-Object { $_.status -eq 'active' } | Sort-Object -Property $sort)
    $ds = @($decisions.decisions | Where-Object { $_.state -eq 'pending' -and $_.panel_status -eq 'active' } | Sort-Object -Property $sort)
    $fs = @($fronts.urgent_forks | Where-Object { $_.priority -in @('критический','высокий') } | Sort-Object -Property $sort)
    $ts = @($fronts.timers | Where-Object {
        if ($_.lifecycle) { $_.lifecycle -eq 'active' } else { $_.status -notmatch '^(закрыт|изучен|сработал)' }
    } | Sort-Object @{Expression={if ($focusIds -contains $_.id) {1000} elseif ($_.status -match 'критич') {500} else {0}};Descending=$true}, id, timer)
    return [pscustomobject]@{questions=$qs;decisions=$ds;fronts=$fs;timers=$ts;state=$state;branch=$branchState}
}

function Get-WmmaMemory {
    param([string]$Root, [object]$Entity)
    $text = Read-WmmaText (Join-Path $Root $Entity.path)
    $sections = [ordered]@{}
    foreach ($heading in @('Кратко','Характер','Известные черты','Цели','Связи','Для ведения','Текущее положение')) {
        $section = Get-WmmaSection $text $heading
        if ($section) { $sections[$heading] = $section }
    }
    return $sections
}

function Get-WmmaContext {
    param([string]$Root, [string]$Branch='', [string]$Query='', [string]$Entity='', [ValidateSet('gm','character')][string]$Audience='gm', [int]$MaxWords=2000)
    if ($Audience -eq 'character' -and -not $Branch) { throw 'Character audience requires an explicit branch.' }
    $selection = Get-WmmaSelection $Root $Branch $Query
    $graph = Read-WmmaJson (Join-Path $Root '09_Реестры/Сущности.json')
    $knowledge = Read-WmmaJson (Join-Path $Root '09_Реестры/Знания.json')
    $byId = @{}; foreach ($node in $graph.entities) { $byId[$node.id]=$node }
    $requested = $null
    if ($Entity) {
        $matches = @($graph.entities | Where-Object { $_.id -eq $Entity -or $_.name -eq $Entity -or $_.aliases -contains $Entity })
        if ($matches.Count -ne 1) { throw "Entity is missing or ambiguous: $Entity ($($matches.Count) matches). Use a stable ID." }
        $requested = $matches[0]
    }
    $sceneIds = @($selection.state.scene_ids)
    if ($selection.branch) { $sceneIds = @($selection.branch.scene_ids) }
    if ($requested -and $requested.type -eq 'scene') { $sceneIds = @($requested.id) }
    if ($requested -and $requested.type -ne 'scene') {
        $relatedScenes=@($graph.entities|Where-Object {$_.type -eq 'scene' -and $_.participant_ids -contains $requested.id}|Sort-Object @{Expression={[int]$_.chapter};Descending=$true},@{Expression={$_.path};Descending=$true}|Select-Object -First 3|ForEach-Object {$_.id})
        $sceneIds=@($sceneIds+$relatedScenes|Select-Object -Unique)
    }
    $entityIds = @($selection.branch.character_id) + @($requested.id)
    foreach ($sceneId in $sceneIds) { if ($sceneId -and $byId.ContainsKey([string]$sceneId)) { $entityIds += @($byId[$sceneId].participant_ids) } }
    $factIds = @($selection.state.fact_ids) + @($selection.branch.fact_ids)
    $facts = @($knowledge.facts | Where-Object {
        ($factIds -contains $_.id -or ($requested -and $_.subject_ids -contains $requested.id)) -and
        ($Audience -eq 'gm' -or ($_.visibility -ne 'gm' -and ($_.visibility -eq 'public' -or $_.known_to -contains $selection.branch.character_id)))
    })
    $lines = [Collections.Generic.List[string]]::new()
    $label = if ($Branch) { $Branch } elseif ($requested) { $requested.name } else { 'Общий мир' }
    $lines.Add("# Контекст: $label"); $lines.Add(''); $lines.Add('---')
    $lines.Add('type: context_packet'); $lines.Add('status: active'); $lines.Add('canon_level: support')
    $lines.Add('generated_by: tools/Получить_контекст.ps1'); $lines.Add("current_chapter: $(Get-WmmaCurrentChapter $Root)"); $lines.Add("audience: $Audience"); $lines.Add('---'); $lines.Add('')
    $lines.Add('## Подтверждённое и неизвестное')
    $omitted = [Collections.Generic.List[object]]::new()
    function Add-ContextBlock([string]$Value, [string]$Reason, [string]$Path) {
        $count = [regex]::Matches(($lines -join "`n") + $Value,'\S+').Count
        if ($count -gt ($MaxWords - 150)) { $omitted.Add([ordered]@{path=$Path;reason=$Reason}); return }
        $lines.Add($Value)
    }
    foreach ($fact in $facts) {
        $refs = @($fact.evidence_ids | Where-Object {$byId.ContainsKey($_)} | ForEach-Object {'`' + $byId[$_].path + '`'}) -join ', '
        Add-ContextBlock "- [$($fact.truth)] $($fact.text) Основание: $refs" 'Факт и его основание' ($fact.evidence_ids -join ', ')
    }
    if ($Audience -eq 'gm') {
        $lines.Add(''); $lines.Add('## Выборы и проблемы')
        foreach ($item in @($selection.decisions | Select-Object -First 6)) { Add-ContextBlock "- $($item.id): $($item.question) [$($item.priority)]" 'Ожидающий выбор' $item.id }
        foreach ($item in @($selection.questions | Select-Object -First 8)) { Add-ContextBlock "- $($item.id): $($item.text) [$($item.priority)]" 'Открытый вопрос' $item.id }
        $lines.Add(''); $lines.Add('## Сцены и участники')
        foreach ($id in @($sceneIds + $entityIds | Where-Object {$_} | Select-Object -Unique)) {
            if (-not $byId.ContainsKey($id)) { continue }
            $node = $byId[$id]
            $reason = if ($sceneIds -contains $id) {'Текущая или общая сцена'} elseif ($requested.id -eq $id) {'Запрошенная сущность'} else {'Участник выбранной сцены'}
            Add-ContextBlock "- $($node.id): $($node.name) — $reason. ``$($node.path)``" $reason $node.path
            if ($node.type -eq 'scene') {
                Add-ContextBlock (Get-WmmaSection (Read-WmmaText (Join-Path $Root $node.path)) 'Что известно персонажу') 'Знания в сцене; проверить перспективу участника' $node.path
            } elseif ($node.type -eq 'character') {
                $memory = Get-WmmaMemory $Root $node
                foreach ($key in @('Кратко','Характер','Цели','Для ведения','Текущее положение')) { if ($memory.Contains($key)) { Add-ContextBlock $memory[$key] $key $node.path } }
            } elseif ($requested -and $node.id -eq $requested.id) {
                $body=Read-WmmaText (Join-Path $Root $node.path)
                foreach($section in [regex]::Matches($body,'(?ms)^## ([^\r\n]+)\r?\n(.*?)(?=^## |\z)')){
                    Add-ContextBlock ($section.Groups[1].Value+': '+$section.Groups[2].Value.Trim()) 'Содержимое запрошенной карточки' $node.path
                }
            }
        }
        $lines.Add(''); $lines.Add('## Фронты')
        foreach ($front in @($selection.fronts | Select-Object -First 3)) { Add-ContextBlock "- $($front.id): $($front.summary) Следующее условие: $($front.trigger)" 'Связанный фронт' $front.id }
    } else {
        $lines.Add(''); $lines.Add('Только явно общедоступные и известные этому персонажу сведения. Неустановленное знание не раскрывается; внутренние вопросы, секреты других лиц и заметки ведущего исключены.')
    }
    if ($omitted.Count) { $lines.Add(''); $lines.Add("Из-за лимита объёма опущено блоков: $($omitted.Count). Для подробностей запросите конкретную сущность или увеличьте MaxWords.") }
    return [pscustomobject]@{text=(($lines -join "`n").TrimEnd()+"`n"); omitted=@($omitted); facts=$facts; scene_ids=$(if($Audience -eq 'character'){@()}else{$sceneIds}); selection=$(if($Audience -eq 'character'){$null}else{$selection})}
}
