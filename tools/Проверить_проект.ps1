$ErrorActionPreference = 'Stop'

[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
$OutputEncoding = [System.Text.UTF8Encoding]::new()

$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$errors = New-Object 'System.Collections.Generic.List[string]'
$warnings = New-Object 'System.Collections.Generic.List[string]'

function Add-Problem {
    param(
        [string]$Kind,
        [string]$Message
    )

    if ($Kind -eq 'Error') {
        $errors.Add($Message) | Out-Null
    } else {
        $warnings.Add($Message) | Out-Null
    }
}

function Get-RelativePath {
    param([string]$Path)

    if ($Path.StartsWith($root, [System.StringComparison]::OrdinalIgnoreCase)) {
        return $Path.Substring($root.Length).TrimStart('\', '/')
    }

    return $Path
}

function Test-ProjectPath {
    param(
        [string]$Reference,
        [string]$FromFile
    )

    if ($Reference -match '^[a-z]+://') {
        return $true
    }

    $normalized = $Reference -replace '/', '\'

    if ([System.IO.Path]::IsPathRooted($normalized)) {
        return Test-Path -LiteralPath $normalized
    }

    $rootCandidate = Join-Path $root $normalized
    if (Test-Path -LiteralPath $rootCandidate) {
        return $true
    }

    $fromDir = Split-Path -Parent $FromFile
    $relativeCandidate = Join-Path $fromDir $normalized
    return Test-Path -LiteralPath $relativeCandidate
}

function Test-PlannedReference {
    param(
        [string]$Reference,
        [string]$FromFile,
        [string]$Text
    )

    if ($Reference -notmatch '\.(jpg|jpeg|png|webp)$') {
        return $false
    }

    if ($Text -match '(?m)^portrait_status:\s*planned\s*$') {
        return $true
    }

    if ($Text -match '(?m)^status:\s*planned\s*$') {
        return $true
    }

    if ($Text -match '(?m)^type:\s*character_portrait_index\s*$') {
        foreach ($line in ($Text -split "\r?\n")) {
            if ($line.Contains($Reference) -and $line -match '\bplanned\b') {
                return $true
            }
        }
    }

    if ($Text -match '(?m)^type:\s*portrait_prompt\s*$' -and $Text -match '(?m)^status:\s*planned\s*$') {
        return $true
    }

    return $false
}

function Get-MetaType {
    param([string]$Text)

    if ($Text -match '(?s)^# .+?\r?\n\r?\n---\r?\n(.+?)\r?\n---') {
        $meta = $Matches[1]
        if ($meta -match '(?m)^type:\s*(.+?)\s*$') {
            return $Matches[1].Trim()
        }
    }

    return $null
}

$mdFiles = Get-ChildItem -LiteralPath $root -Recurse -Force -File -Filter '*.md' |
    Where-Object {
        $_.FullName -notmatch '\\.git\\' -and
        $_.FullName -notmatch '\\[^\\]*_MD_[^\\]*\\'
    }

$imageFiles = Get-ChildItem -LiteralPath $root -Recurse -Force -File |
    Where-Object {
        $_.FullName -notmatch '\\.git\\' -and
        $_.FullName -notmatch '\\[^\\]*_MD_[^\\]*\\' -and
        $_.Extension -match '^\.(jpg|jpeg|png|webp)$'
    }

$filesByType = @{}

foreach ($requiredPath in @(
    'AGENTS.md'
)) {
    if (-not (Test-Path -LiteralPath (Join-Path $root $requiredPath))) {
        Add-Problem Error "Required AI support file is missing: $requiredPath"
    }
}

$gitignorePath = Join-Path $root '.gitignore'
if (-not (Test-Path -LiteralPath $gitignorePath)) {
    Add-Problem Warning '.gitignore file not found.'
} else {
    $gitignore = Get-Content -Raw -Encoding UTF8 -LiteralPath $gitignorePath
    if ($gitignore -notmatch '(?m)^.+_MD_.+/$') {
        Add-Problem Warning '.gitignore does not appear to ignore the temporary Markdown export folder.'
    }
}

foreach ($file in $mdFiles) {
    $text = Get-Content -Raw -Encoding UTF8 -LiteralPath $file.FullName
    $type = Get-MetaType -Text $text
    $relativeFile = Get-RelativePath $file.FullName
    $isTemplateFile = $relativeFile -like '09_*'

    if ($type -and -not $isTemplateFile) {
        if (-not $filesByType.ContainsKey($type)) {
            $filesByType[$type] = New-Object 'System.Collections.Generic.List[object]'
        }

        $filesByType[$type].Add($file) | Out-Null
    }

    $matches = [regex]::Matches($text, '`([^`]+\.(?:md|jpg|jpeg|png|webp))`')

    foreach ($match in $matches) {
        $ref = $match.Groups[1].Value
        if (
            -not (Test-ProjectPath -Reference $ref -FromFile $file.FullName) -and
            -not (Test-PlannedReference -Reference $ref -FromFile $file.FullName -Text $text)
        ) {
            Add-Problem Error "Broken local reference: $(Get-RelativePath $file.FullName) -> $ref"
        }
    }

    if (-not $isTemplateFile -and $text -match '(?s)^# .+?\r?\n\r?\n---\r?\n(.+?)\r?\n---') {
        $meta = $Matches[1]
        foreach ($field in @('type', 'status', 'canon_level')) {
            if ($meta -notmatch "(?m)^$field\s*:\s*\S+") {
                Add-Problem Warning "Missing front matter field '$field': $(Get-RelativePath $file.FullName)"
            }
        }
    }
}

foreach ($requiredType in @(
    'ai_agent_instructions',
    'ai_current_context',
    'alias_index',
    'campaign_summary',
    'active_chapter',
    'decision_log',
    'open_questions',
    'world_state',
    'front_tracker',
    'character_index',
    'character_portrait_index'
)) {
    if (-not $filesByType.ContainsKey($requiredType)) {
        Add-Problem Warning "Required support type not found: $requiredType"
    }
}

if (-not $filesByType.ContainsKey('character_index')) {
    Add-Problem Error 'character_index file not found.'
} else {
    $characterIndexPath = $filesByType['character_index'][0].FullName
    $characterIndex = Get-Content -Raw -Encoding UTF8 -LiteralPath $characterIndexPath
    $characterRefs = [regex]::Matches($characterIndex, '`([^`]+\.md)`') |
        ForEach-Object { $_.Groups[1].Value } |
        Where-Object { $_ -like '03_*' }

    $characterFiles = @()
    if ($filesByType.ContainsKey('character')) {
        $characterFiles = $filesByType['character'] |
            ForEach-Object { (Get-RelativePath $_.FullName) -replace '\\', '/' }
    }

    foreach ($ref in $characterRefs) {
        if (-not (Test-ProjectPath -Reference $ref -FromFile $characterIndexPath)) {
            Add-Problem Error "Character index points to missing file: $ref"
        }
    }

    foreach ($file in $characterFiles) {
        if ($characterRefs -notcontains $file) {
            Add-Problem Error "Character card is missing from index: $file"
        }
    }

    $characterIndexRows = [regex]::Matches(
        $characterIndex,
        '(?m)^\|\s*([^|]+?)\s*\|\s*([^|]+?)\s*\|\s*([^|]+?)\s*\|\s*`([^`]+\.md)`\s*\|\s*$'
    )

    foreach ($row in $characterIndexRows) {
        $characterName = $row.Groups[1].Value.Trim()
        $indexPortraitStatus = $row.Groups[3].Value.Trim()
        $characterRef = $row.Groups[4].Value.Trim()

        if ($characterRef -notlike '03_*') {
            continue
        }

        $normalizedRef = $characterRef -replace '/', '\'
        $characterPath = Join-Path $root $normalizedRef

        if (-not (Test-Path -LiteralPath $characterPath)) {
            $characterPath = Join-Path (Split-Path -Parent $characterIndexPath) $normalizedRef
        }

        if (-not (Test-Path -LiteralPath $characterPath)) {
            continue
        }

        $characterText = Get-Content -Raw -Encoding UTF8 -LiteralPath $characterPath

        if ($characterText -notmatch '(?m)^portrait_status:\s*(available|missing|planned)\s*$') {
            continue
        }

        $portraitStatus = $Matches[1].Trim()
        $indexStatusAvailable = -join [char[]](0x0435, 0x0441, 0x0442, 0x044C)
        $indexStatusMissing = -join [char[]](0x043D, 0x0443, 0x0436, 0x0435, 0x043D)
        $indexStatusPlanned = -join [char[]](0x0437, 0x0430, 0x043F, 0x043B, 0x0430, 0x043D, 0x0438, 0x0440, 0x043E, 0x0432, 0x0430, 0x043D)
        $expectedIndexStatus = switch ($portraitStatus) {
            'available' { $indexStatusAvailable }
            'missing' { $indexStatusMissing }
            'planned' { $indexStatusPlanned }
        }

        if ($indexPortraitStatus -ne $expectedIndexStatus) {
            Add-Problem Warning "Character index portrait status mismatch: $characterName uses '$indexPortraitStatus' but card expects '$expectedIndexStatus': $characterRef"
        }
    }
}

if ($filesByType.ContainsKey('character')) {
    foreach ($file in $filesByType['character']) {
        $text = Get-Content -Raw -Encoding UTF8 -LiteralPath $file.FullName

        if ($text -notmatch '(?m)^portrait:\s*(.+)\s*$') {
            Add-Problem Error "Character card has no portrait field: $(Get-RelativePath $file.FullName)"
            continue
        }

        $portrait = $Matches[1].Trim()

        if ($text -notmatch '(?m)^portrait_status:\s*(available|missing|planned)\s*$') {
            Add-Problem Error "Character card has invalid portrait_status: $(Get-RelativePath $file.FullName)"
            continue
        }

        $portraitStatus = $Matches[1].Trim()

        if ($portraitStatus -eq 'available') {
            if ($portrait -eq 'null' -or -not (Test-ProjectPath -Reference $portrait -FromFile $file.FullName)) {
                Add-Problem Error "portrait_status=available but portrait file is missing: $(Get-RelativePath $file.FullName)"
            }
        }
    }
}

if (-not $filesByType.ContainsKey('character_portrait_index')) {
    Add-Problem Error 'character_portrait_index file not found.'
} else {
    $portraitIndexPath = $filesByType['character_portrait_index'][0].FullName
    $portraitIndex = Get-Content -Raw -Encoding UTF8 -LiteralPath $portraitIndexPath
    $portraitRoot = Split-Path -Parent $portraitIndexPath
    $portraitRefs = [regex]::Matches($portraitIndex, '`([^`]+\.(?:jpg|jpeg|png|webp))`') |
        ForEach-Object { $_.Groups[1].Value }
    $portraitImageFiles = Get-ChildItem -LiteralPath $portraitRoot -Recurse -File |
        Where-Object { $_.Extension -match '^\.(jpg|jpeg|png|webp)$' } |
        ForEach-Object { (Get-RelativePath $_.FullName) -replace '\\', '/' }

    foreach ($ref in $portraitRefs) {
        if (
            -not (Test-ProjectPath -Reference $ref -FromFile $portraitIndexPath) -and
            -not (Test-PlannedReference -Reference $ref -FromFile $portraitIndexPath -Text $portraitIndex)
        ) {
            Add-Problem Error "Portrait index points to missing file: $ref"
        }
    }

    foreach ($file in $portraitImageFiles) {
        if ($portraitRefs -notcontains $file) {
            Add-Problem Error "Portrait image is missing from portrait index: $file"
        }
    }

    foreach ($dir in Get-ChildItem -LiteralPath $portraitRoot -Directory) {
        $hasImage = (Get-ChildItem -LiteralPath $dir.FullName -File |
            Where-Object { $_.Extension -match '^\.(jpg|jpeg|png|webp)$' } |
            Measure-Object).Count -gt 0
        $hasPrompt = $false

        foreach ($promptFile in Get-ChildItem -LiteralPath $dir.FullName -File -Filter '*.md') {
            $promptText = Get-Content -Raw -Encoding UTF8 -LiteralPath $promptFile.FullName
            if ((Get-MetaType -Text $promptText) -eq 'portrait_prompt') {
                $hasPrompt = $true
                break
            }
        }

        if ($hasImage -and -not $hasPrompt) {
            Add-Problem Warning "Portrait folder has image but no portrait_prompt file: $(Get-RelativePath $dir.FullName)"
        }
    }
}

if ($filesByType.ContainsKey('decision_log')) {
    $decisionLogPath = $filesByType['decision_log'][0].FullName
    $decisionLog = Get-Content -Raw -Encoding UTF8 -LiteralPath $decisionLogPath
    $decisionIds = [regex]::Matches($decisionLog, '(?m)^###\s+(DEC(?:-PENDING)?-\d{3})\s*$') |
        ForEach-Object { $_.Groups[1].Value }

    if ($decisionIds.Count -eq 0) {
        Add-Problem Warning 'No decision IDs found in decision_log headings.'
    }

    $duplicateDecisionIds = $decisionIds |
        Group-Object |
        Where-Object { $_.Count -gt 1 } |
        ForEach-Object { $_.Name }

    foreach ($id in $duplicateDecisionIds) {
        Add-Problem Error "Duplicate decision ID in decision log: $id"
    }

    $acceptedDecisionIds = [regex]::Matches($decisionLog, '(?m)^###\s+DEC-(\d{3})\s*$') |
        ForEach-Object {
            [pscustomobject]@{
                Id = [int]$_.Groups[1].Value
                Text = $_.Value.Trim()
            }
        }

    for ($i = 1; $i -lt $acceptedDecisionIds.Count; $i++) {
        if ($acceptedDecisionIds[$i].Id -lt $acceptedDecisionIds[$i - 1].Id) {
            Add-Problem Warning "Decision IDs are out of order: $($acceptedDecisionIds[$i].Text) follows $($acceptedDecisionIds[$i - 1].Text)"
        }
    }
}

if ($filesByType.ContainsKey('open_questions')) {
    $openQuestionsPath = $filesByType['open_questions'][0].FullName
    $openQuestions = Get-Content -Raw -Encoding UTF8 -LiteralPath $openQuestionsPath
    $questionIds = [regex]::Matches($openQuestions, '\bQ-(?:C2|WORLD)-\d{3}\b') |
        ForEach-Object { $_.Value }

    if ($questionIds.Count -eq 0) {
        Add-Problem Warning 'No Q-C2/Q-WORLD IDs found in open_questions.'
    }

    $duplicateQuestionIds = $questionIds |
        Group-Object |
        Where-Object { $_.Count -gt 1 } |
        ForEach-Object { $_.Name }

    foreach ($id in $duplicateQuestionIds) {
        Add-Problem Error "Duplicate open question ID: $id"
    }
}

$result = [pscustomobject]@{
    MarkdownFiles = $mdFiles.Count
    ImageFiles = $imageFiles.Count
    Errors = $errors.Count
    Warnings = $warnings.Count
}

$result | Format-List

if ($warnings.Count -gt 0) {
    "`nWarnings:"
    $warnings | Sort-Object | ForEach-Object { "- $_" }
}

if ($errors.Count -gt 0) {
    "`nErrors:"
    $errors | Sort-Object | ForEach-Object { "- $_" }
    exit 1
}

"`nProject check completed successfully."
