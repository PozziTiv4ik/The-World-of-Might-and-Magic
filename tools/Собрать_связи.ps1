param([switch]$AssignMissingIds, [switch]$SkipCheck)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '_lib.ps1')
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Invoke-WmmaToolMain -Root $root -Name $MyInvocation.MyCommand.Name -ScriptBlock {
    $docs = @(Get-WmmaDocuments $root | Sort-Object path)
    $byPath = @{}; $byId = @{}; $next = @{}
    foreach ($doc in $docs) {
        if ($doc.id) {
            if ($byId.ContainsKey($doc.id)) { throw "Duplicate entity ID: $($doc.id)" }
            $byId[$doc.id] = $doc
            if ($doc.id -match '^([A-Z]+)-(\d+)$') { $next[$Matches[1]] = [Math]::Max([int]$next[$Matches[1]], [int]$Matches[2]) }
        }
    }
    foreach ($doc in $docs) {
        if (-not $doc.id) {
            if (-not $AssignMissingIds) { throw "Missing ID: $($doc.path). Run with -AssignMissingIds." }
            $prefix = Get-WmmaIdPrefix $doc.type
            $next[$prefix] = [int]$next[$prefix] + 1
            $doc.id = '{0}-{1:D4}' -f $prefix, $next[$prefix]
            $doc.text = Set-WmmaMeta $doc.text 'id' $doc.id
            Write-WmmaText $doc.full_path $doc.text
            $byId[$doc.id] = $doc
        }
        $byPath[$doc.path] = $doc.id
    }
    $entities = @(); $edges = [Collections.Generic.List[object]]::new()
    foreach ($doc in $docs) {
        $name = ([regex]::Match($doc.text, '(?m)^# (.+)')).Groups[1].Value.Trim()
        $fronts = @(Get-WmmaArrayMeta $doc.text 'front_ids')
        $legacyFront = Get-WmmaMeta $doc.text 'front_id'
        if ($legacyFront -and $legacyFront -ne '-') { $fronts += $legacyFront }
        $sources = @(Get-WmmaArrayMeta $doc.text 'source_ids')
        $participants = @(Get-WmmaArrayMeta $doc.text 'participant_ids')
        $references = @([regex]::Matches($doc.text, '`([^`\r\n]+\.md)`') | ForEach-Object { $_.Groups[1].Value.Replace('\', '/') } | Sort-Object -Unique)
        foreach ($ref in $references) {
            if ($byPath.ContainsKey($ref)) { $edges.Add([ordered]@{from=$doc.id;to=$byPath[$ref];kind='references';evidence=$doc.path}) }
        }
        foreach ($id in $sources) { $edges.Add([ordered]@{from=$doc.id;to=$id;kind='sourced_from';evidence=$doc.path}) }
        foreach ($id in $participants) { $edges.Add([ordered]@{from=$doc.id;to=$id;kind='participant';evidence=$doc.path}) }
        $entities += [ordered]@{
            id=$doc.id; type=$doc.type; name=$name; path=$doc.path; status=(Get-WmmaMeta $doc.text 'status')
            chapter=(Get-WmmaMeta $doc.text 'chapter'); branch=(Get-WmmaMeta $doc.text 'branch')
            aliases=@(@($name) + @(Get-WmmaArrayMeta $doc.text 'aliases') | Sort-Object -Unique)
            front_ids=@($fronts | Sort-Object -Unique); source_ids=$sources; participant_ids=$participants
            provenance=$(if ($sources.Count) {'linked'} elseif ($doc.type -like 'source*') {'original'} else {'unresolved'})
            content_sha256=(Get-WmmaHash $doc.text)
        }
    }
    # Explicit reverse links are evidence. Similar names alone never establish provenance.
    $inbox = Read-WmmaText (Join-Path $root '07_Черновики_и_идеи/Входящие_сообщения.md')
    foreach ($entry in [regex]::Matches($inbox, '(?ms)^### .+?\r?\n(.*?)(?=^### |^## |\z)')) {
        $refs = @([regex]::Matches($entry.Value, '`([^`\r\n]+\.md)`') | ForEach-Object { $_.Groups[1].Value.Replace('\', '/') })
        $sourceRefs = @($refs | Where-Object { $_ -like '08_Источники/*' -and $byPath.ContainsKey($_) })
        $sceneRefs = @($refs | Where-Object { $_ -like '01_Кампания/Ветки/*/Сцена*' -and $byPath.ContainsKey($_) })
        foreach ($sceneRef in $sceneRefs) { foreach ($sourceRef in $sourceRefs) {
            $edges.Add([ordered]@{from=$byPath[$sceneRef];to=$byPath[$sourceRef];kind='sourced_from';evidence='07_Черновики_и_идеи/Входящие_сообщения.md'})
        } }
    }
    foreach ($entity in $entities) {
        $entity.source_ids = @(@($entity.source_ids) + @($edges | Where-Object { $_.from -eq $entity.id -and $_.kind -eq 'sourced_from' } | ForEach-Object {$_.to}) | Sort-Object -Unique)
        if ($entity.source_ids.Count) { $entity.provenance = 'linked' }
    }
    Write-WmmaJson (Join-Path $root '09_Реестры/Сущности.json') ([ordered]@{schema_version=2;type='entity_graph';generated_by='tools/Собрать_связи.ps1';entities=$entities;edges=@($edges)})
    "Entity graph: $($entities.Count) entities, $($edges.Count) relationships."
    if (-not $SkipCheck) { & (Join-Path $root 'tools/Проверить_контекст.ps1') }
}
