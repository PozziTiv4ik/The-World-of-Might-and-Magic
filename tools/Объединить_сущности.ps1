param(
    [Parameter(Mandatory = $true)]
    [string]$From,

    [Parameter(Mandatory = $true)]
    [string]$Into,

    [string]$Reason = 'Одна сущность записана под двумя ID.',

    [switch]$SkipCheck
)

$ErrorActionPreference = 'Stop'

[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
$OutputEncoding = [System.Text.UTF8Encoding]::new()

. (Join-Path $PSScriptRoot '_lib.ps1')
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path

# Merges a duplicate card into the main one. Reference fields of document metadata and
# registries move to the main ID (scene and card prose is never rewritten), titles and
# aliases join the main card, the old text is kept as its protected history, and the
# duplicate card stays as a redirect: its ID is never reused.
Invoke-WmmaToolMain -Root $root -Name $MyInvocation.MyCommand.Name -ScriptBlock {
    if ($From -ceq $Into) { throw 'An entity cannot be merged into itself.' }
    Assert-WmmaSingleLine $Reason 'Reason'
    $today = Get-Date -Format 'yyyy-MM-dd'
    $documents = @(Get-WmmaDocuments $root)
    $source = @($documents | Where-Object { $_.id -ceq $From })
    $target = @($documents | Where-Object { $_.id -ceq $Into })
    if ($source.Count -ne 1) { throw "Unknown entity to merge: $From" }
    if ($target.Count -ne 1) { throw "Unknown main entity: $Into" }
    $source = $source[0]
    $target = $target[0]
    if ($source.type -ne $target.type) { throw "Only entities of one type merge: $From is $($source.type), $Into is $($target.type)." }
    if ($source.type -in @('scene', 'source', 'source_note', 'source_compilation')) { throw "Scenes and sources are events and original texts; link them instead of merging: $From" }
    $targetMerged = Get-WmmaMeta $target.text 'merged_into'
    if ($targetMerged) { throw "$Into is itself merged into $targetMerged; merge into the main ID." }
    $sourceMerged = Get-WmmaMeta $source.text 'merged_into'
    if ($sourceMerged -and $sourceMerged -cne $Into) { throw "$From is already merged into $sourceMerged." }

    $manifest = Read-WmmaJson (Get-WmmaProtectionManifestPath $root)
    $protected = @{}
    foreach ($item in @($manifest.protected_files)) { $protected[$item.path] = $true }
    $changed = [System.Collections.Generic.List[string]]::new()
    $touchedProtected = [System.Collections.Generic.List[string]]::new()

    # 1. Reference fields of document metadata. Text is never rewritten.
    foreach ($doc in $documents) {
        if ($doc.id -ceq $From) { continue }
        $text = Read-WmmaText $doc.full_path
        $updated = $text
        foreach ($field in @('participant_ids', 'source_ids', 'distinct_from')) {
            $values = @(Get-WmmaArrayMeta $updated $field)
            if (@($values | Where-Object { $_ -ceq $From }).Count -eq 0) { continue }
            $next = @(Get-WmmaReplacedIds $values $From $Into)
            if ($field -eq 'distinct_from') { $next = @($next | Where-Object { $_ -cne $doc.id }) }
            $updated = Set-WmmaMeta $updated $field (Format-WmmaMetaArray $next)
        }
        foreach ($field in @('owner_id', 'merged_into')) {
            if ((Get-WmmaMeta $updated $field) -ceq $From) { $updated = Set-WmmaMeta $updated $field $Into }
        }
        if ($updated -cne $text) {
            Write-WmmaText $doc.full_path $updated
            $changed.Add($doc.path)
            if ($protected.ContainsKey($doc.path)) { $touchedProtected.Add($doc.path) }
        }
    }

    # 2. Registries: knowledge, context pointers, chronology and decision scenes.
    $knowledge = Read-WmmaRegistry $root 'Знания'
    $knowledgeChanged = $false
    foreach ($fact in $knowledge.facts) {
        foreach ($field in @('subject_ids', 'known_to', 'evidence_ids')) {
            if (@($fact.$field | Where-Object { $_ -ceq $From }).Count) { $fact.$field = @(Get-WmmaReplacedIds $fact.$field $From $Into); $knowledgeChanged = $true }
        }
        if ($fact.reported_by -ceq $From) { $fact.reported_by = $Into; $knowledgeChanged = $true }
        if (@($fact.knowledge_evidence | Where-Object { $_ -and ($_.character_id -ceq $From -or @($_.evidence_ids) -ccontains $From) }).Count) {
            $records = [ordered]@{}
            foreach ($record in @($fact.knowledge_evidence | Where-Object { $_ })) {
                $holder = if ($record.character_id -ceq $From) { $Into } else { $record.character_id }
                $evidence = @(Get-WmmaReplacedIds $record.evidence_ids $From $Into)
                if ($records.Contains($holder)) {
                    $existing = $records[$holder]
                    $existing.evidence_ids = @(Get-WmmaReplacedIds (@($existing.evidence_ids) + $evidence) $From $Into)
                    if ($record.basis -and -not $existing.basis.Contains($record.basis)) { $existing.basis = $existing.basis + ' ' + $record.basis }
                } else {
                    $records[$holder] = [pscustomobject][ordered]@{ character_id = $holder; evidence_ids = $evidence; basis = $record.basis }
                }
            }
            $fact.knowledge_evidence = @($records.Values)
            $knowledgeChanged = $true
        }
    }
    if ($knowledgeChanged) { Write-WmmaRegistry $root 'Знания' $knowledge; $changed.Add('09_Реестры/Знания.json') }

    $state = Read-WmmaRegistry $root 'Контекст'
    $stateChanged = $false
    foreach ($holder in @($state) + @($state.branches)) {
        if (@($holder.scene_ids | Where-Object { $_ -ceq $From }).Count) { $holder.scene_ids = @(Get-WmmaReplacedIds $holder.scene_ids $From $Into); $stateChanged = $true }
    }
    foreach ($branch in @($state.branches)) {
        if ($branch.character_id -ceq $From) { $branch.character_id = $Into; $stateChanged = $true }
    }
    if ($stateChanged) { Write-WmmaRegistry $root 'Контекст' $state; $changed.Add('09_Реестры/Контекст.json') }

    $chronology = Read-WmmaRegistry $root 'Хронология_связей'
    $chronologyChanged = $false
    foreach ($relation in @($chronology.relations | Where-Object { $_ })) {
        foreach ($field in @('from', 'to')) {
            if ($relation.$field -ceq $From) { $relation.$field = $Into; $chronologyChanged = $true }
        }
        if (@($relation.evidence_ids | Where-Object { $_ -ceq $From }).Count) { $relation.evidence_ids = @(Get-WmmaReplacedIds $relation.evidence_ids $From $Into); $chronologyChanged = $true }
    }
    if ($chronologyChanged) { Write-WmmaRegistry $root 'Хронология_связей' $chronology; $changed.Add('09_Реестры/Хронология_связей.json') }

    $decisions = Read-WmmaRegistry $root 'Решения'
    $decisionsChanged = $false
    foreach ($decision in $decisions.decisions) {
        if (@($decision.scene_ids | Where-Object { $_ -ceq $From }).Count) { $decision.scene_ids = @(Get-WmmaReplacedIds $decision.scene_ids $From $Into); $decisionsChanged = $true }
    }
    if ($decisionsChanged) { Write-WmmaRegistry $root 'Решения' $decisions; $changed.Add('09_Реестры/Решения.json') }

    # 3. Titles and aliases of the duplicate join the main card; so do its front links.
    $targetText = Read-WmmaText $target.full_path
    $targetTitle = Get-WmmaTitle $targetText
    $aliases = [System.Collections.Generic.List[string]]::new()
    foreach ($alias in @(Get-WmmaArrayMeta $targetText 'aliases')) { if (-not $aliases.Contains($alias)) { $aliases.Add($alias) } }
    $knownKeys = @(@($targetTitle) + @($aliases) | ForEach-Object { ConvertTo-WmmaNameKey $_ })
    foreach ($name in @(Get-WmmaTitle $source.text) + @(Get-WmmaArrayMeta $source.text 'aliases')) {
        $key = ConvertTo-WmmaNameKey $name
        if ($key -and $knownKeys -notcontains $key) { $aliases.Add($name); $knownKeys += $key }
    }
    $updatedTarget = $targetText
    if (@($aliases).Count -ne @(Get-WmmaArrayMeta $targetText 'aliases').Count) {
        $updatedTarget = Set-WmmaMeta $updatedTarget 'aliases' (Format-WmmaMetaArray @($aliases))
    }
    $sourceFronts = @(@(Get-WmmaArrayMeta $source.text 'front_ids') + @(Get-WmmaMeta $source.text 'front_id') | Where-Object { $_ -and $_ -ne '-' } | Select-Object -Unique)
    if ($sourceFronts.Count) {
        $targetFrontId = Get-WmmaMeta $updatedTarget 'front_id'
        $targetFronts = @(Get-WmmaArrayMeta $updatedTarget 'front_ids')
        $missingFronts = @($sourceFronts | Where-Object { $_ -cne $targetFrontId -and $targetFronts -cnotcontains $_ })
        if ($missingFronts.Count -eq 1 -and $targetFronts.Count -eq 0 -and $targetFrontId -in @('-', '')) {
            $updatedTarget = Set-WmmaMeta $updatedTarget 'front_id' $missingFronts[0]
        } elseif ($missingFronts.Count) {
            $updatedTarget = Set-WmmaMeta $updatedTarget 'front_ids' (Format-WmmaMetaArray (@($targetFronts) + @($missingFronts)))
        }
    }
    if ($updatedTarget -cne $targetText) {
        Write-WmmaText $target.full_path $updatedTarget
        if (-not $changed.Contains($target.path)) { $changed.Add($target.path) }
        if ($protected.ContainsKey($target.path)) { $touchedProtected.Add($target.path) }
    }

    # 4. The duplicate becomes a redirect; its full text stays as history of the main card.
    if (-not $sourceMerged) {
        $targetFolder = (Split-Path -Parent $target.path).Replace('\', '/')
        $historyPath = '{0}/История/{1}/Объединено_{2}.md' -f $targetFolder, [IO.Path]::GetFileNameWithoutExtension($target.path), [IO.Path]::GetFileNameWithoutExtension($source.path)
        Add-WmmaHistoryRecord -Root $root -OriginalPath $target.path -HistoryPath $historyPath -Text $source.text `
            -Note "Прежняя карточка $From, объединённая с $Into ($today). Причина: $Reason Текст сохранён без изменений; действующие сведения — в основной карточке."
        $redirect = $source.text
        $redirect = Set-WmmaMeta $redirect 'status' 'merged'
        $redirect = Set-WmmaMeta $redirect 'merged_into' $Into
        $redirect = Set-WmmaMeta $redirect 'merged_real_date' $today
        $redirect = Set-WmmaMeta $redirect 'aliases' '[]'
        if (Get-WmmaMeta $redirect 'role') { $redirect = Set-WmmaMeta $redirect 'role' "объединена с карточкой «$targetTitle» ($Into)" }
        if (Get-WmmaMeta $redirect 'front_id') { $redirect = Set-WmmaMeta $redirect 'front_id' '-' }
        foreach ($field in @('front_ids', 'distinct_from')) {
            if (Get-WmmaMeta $redirect $field) { $redirect = Set-WmmaMeta $redirect $field '[]' }
        }
        $header = [regex]::Match($redirect, '(?s)\A(?:﻿)?# [^\r\n]+\r?\n\s*---\r?\n.*?\r?\n---')
        $redirect = $redirect.Substring(0, $header.Index + $header.Length) + "`n`n## Объединено`n`nКарточка объединена с ``$($target.path)`` ($Into). ID $From сохранён как перенаправление и повторно не используется: новые ссылки ставь на $Into. Прежний текст сохранён в истории основной карточки: ``$historyPath``.`n"
        Write-WmmaText $source.full_path $redirect
        $changed.Add($source.path)
        $changed.Add($historyPath)
    }

    # 5. A protected closed-canon file changed only in its ID fields: record the new hash.
    if ($touchedProtected.Count) {
        [void](Update-WmmaProtectedFiles -Root $root -Paths @($touchedProtected) -Reason "Объединение $From → ${Into}: замена ID в полях-ссылках метаданных; текст не менялся.")
    }

    $rebuild = switch ($source.type) {
        'character' { @('Собрать_индекс_персонажей') }
        'location' { @('Собрать_индекс_локаций') }
        'character_asset' { @('Собрать_индекс_активов') }
        default { @() }
    }
    Complete-WmmaMutation -Root $root -Rebuild $rebuild -FullGraph -SkipCheck:$SkipCheck

    # World data of the Regnum editor is the user's working model: report, never rewrite.
    foreach ($link in @(Get-WmmaRegnumEntityLinks -Root $root | Where-Object { $_.entity -ceq $From })) {
        "Regnum still links ${From}: $($link.world) / $($link.table) $($link.id) — relink it to $Into in the editor."
    }
    if ($sourceMerged) { "Merged entity: $From -> $Into (already merged; references refreshed)" }
    else { "Merged entity: $From -> $Into" }
    foreach ($path in $changed) { "- $path" }
    "Record the distinction or the shared name in 03_Персонажи/00_Словарь_имен_и_алиасов.md when the name could mislead."
}
