# Shared primitives. All paths are relative to the explicit project root.
function Read-WmmaText {
    param([string]$Path)
    return [IO.File]::ReadAllText($Path, [Text.Encoding]::UTF8)
}

function Write-WmmaText {
    param([string]$Path, [AllowEmptyString()][string]$Text)
    $parent = Split-Path -Parent $Path
    if (-not (Test-Path -LiteralPath $parent)) { [IO.Directory]::CreateDirectory($parent) | Out-Null }
    if ((Test-Path -LiteralPath $Path) -and (Read-WmmaText $Path) -ceq $Text) { return }
    $temporary = "$Path.$([guid]::NewGuid().ToString('N')).tmp"
    [IO.File]::WriteAllText($temporary, $Text, [Text.UTF8Encoding]::new($false))
    if (Test-Path -LiteralPath $Path) { [IO.File]::Replace($temporary, $Path, [NullString]::Value) }
    else { [IO.File]::Move($temporary, $Path) }
}

function Read-WmmaJson {
    param([string]$Path)
    return (Read-WmmaText $Path | ConvertFrom-Json)
}

function Write-WmmaJson {
    param([string]$Path, [object]$Value)
    Write-WmmaText $Path (($Value | ConvertTo-Json -Depth 50).TrimEnd() + "`n")
}

function Get-WmmaHash {
    param([string]$Text)
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($Text)))).Replace('-', '').ToLowerInvariant() }
    finally { $sha.Dispose() }
}

function Resolve-WmmaPath {
    param([string]$Root, [string]$Path)
    if ([string]::IsNullOrWhiteSpace($Path) -or [IO.Path]::IsPathRooted($Path)) { throw "Expected a relative project path: $Path" }
    $base = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
    $full = [IO.Path]::GetFullPath((Join-Path $base $Path))
    if (-not $full.StartsWith($base + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw "Path leaves project: $Path" }
    $cursor = $full
    while ($cursor -ne $base) {
        if ((Test-Path -LiteralPath $cursor) -and ((Get-Item -Force -LiteralPath $cursor).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "Reparse points are not writable project paths: $Path" }
        $cursor = Split-Path -Parent $cursor
    }
    return $full
}

function Get-WmmaRelativePath {
    param([string]$Root, [string]$Path)
    return $Path.Substring($Root.TrimEnd('\', '/').Length).TrimStart('\', '/').Replace('\', '/')
}

function Get-WmmaMeta {
    param([string]$Text, [string]$Field)
    $block = [regex]::Match($Text, '(?s)\A(?:\uFEFF)?# [^\r\n]+\r?\n\s*---\r?\n(.*?)\r?\n---')
    if ($block.Success) {
        $entry = [regex]::Match($block.Groups[1].Value, '(?m)^' + [regex]::Escape($Field) + ':\s*([^\r\n]*)')
        if ($entry.Success) { return $entry.Groups[1].Value.Trim() }
    }
    return ''
}

function Set-WmmaMeta {
    param([string]$Text, [string]$Field, [string]$Value)
    $block = [regex]::Match($Text, '(?s)\A(?:\uFEFF)?# [^\r\n]+\r?\n\s*---\r?\n(.*?)\r?\n---')
    if (-not $block.Success) { throw 'Document has no project front matter.' }
    $meta = $block.Groups[1].Value
    $pattern = '(?m)^' + [regex]::Escape($Field) + ':[^\r\n]*'
    if ([regex]::IsMatch($meta, $pattern)) { $meta = [regex]::Replace($meta, $pattern, [Text.RegularExpressions.MatchEvaluator]{ param($m) "${Field}: $Value" }) }
    else { $meta += "`n${Field}: $Value" }
    return $Text.Substring(0, $block.Groups[1].Index) + $meta + $Text.Substring($block.Groups[1].Index + $block.Groups[1].Length)
}

function Get-WmmaSection {
    param([string]$Text, [string]$Heading)
    $match = [regex]::Match($Text, '(?ms)^## ' + [regex]::Escape($Heading) + '\s*\r?\n(.*?)(?=^## |\z)')
    if ($match.Success) { return $match.Groups[1].Value.Trim() }
    return ''
}

function Get-WmmaArrayMeta {
    param([string]$Text, [string]$Field)
    $value = Get-WmmaMeta $Text $Field
    if (-not $value -or $value -in @('null', '-')) { return @() }
    if ($value.StartsWith('[')) { return @($value | ConvertFrom-Json) }
    return @($value)
}

function Get-WmmaCurrentChapter {
    param([string]$Root)
    $active = @(Get-ChildItem -LiteralPath (Join-Path $Root '01_Кампания/Главы') -Filter '*.md' -File | Where-Object { (Get-WmmaMeta (Read-WmmaText $_.FullName) 'status') -eq 'active' })
    if ($active.Count -ne 1) { throw "Expected exactly one active chapter, found $($active.Count)." }
    return [int](Get-WmmaMeta (Read-WmmaText $active[0].FullName) 'chapter')
}

function Get-WmmaDocuments {
    param([string]$Root)
    foreach ($directory in @('01_Кампания/Ветки', '02_Лор', '03_Персонажи', '04_Локации', '05_Активы_персонажей', '08_Источники')) {
        foreach ($file in Get-ChildItem -LiteralPath (Join-Path $Root $directory) -Recurse -File -Filter '*.md') {
            if ($file.FullName -match '[\\/]История[\\/]') { continue }
            $text = Read-WmmaText $file.FullName
            $type = Get-WmmaMeta $text 'type'
            if ($type -in @('character','location','character_asset','scene','source','source_note','source_compilation','lore','faction_index','economy_notes','timeline')) {
                [pscustomobject]@{path=(Get-WmmaRelativePath $Root $file.FullName); full_path=$file.FullName; text=$text; type=$type; id=(Get-WmmaMeta $text 'id')}
            }
        }
    }
}

function Convert-WmmaCell {
    param([object]$Value)
    return ([string]$Value).Replace('|', '&#124;').Replace("`r", '').Replace("`n", '<br>')
}

function Get-WmmaPriority {
    param([string]$Value)
    switch ($Value) { 'критический' { 4 } 'высокий' { 3 } 'средний' { 2 } default { 1 } }
}

function Get-WmmaIdPrefix {
    param([string]$Type)
    switch ($Type) { 'character' { 'CHAR' } 'location' { 'LOC' } 'character_asset' { 'ASSET' } 'scene' { 'SCENE' } { $_ -in @('source','source_note','source_compilation') } { 'SRC' } default { 'LORE' } }
}

function Get-WmmaReceipt {
    param([string]$Root,[string]$Id)
    $path=Join-Path $Root '09_Реестры/Входящие.json'
    if(Test-Path -LiteralPath $path){return @((Read-WmmaJson $path).receipts|Where-Object {$_.request_id -eq $Id})|Select-Object -First 1}
    return $null
}

function Save-WmmaReceipt {
    param([string]$Root,[object]$Receipt)
    $path=Join-Path $Root '09_Реестры/Входящие.json'
    $registry=if(Test-Path -LiteralPath $path){Read-WmmaJson $path}else{[pscustomobject]@{schema_version=1;type='intake_registry';receipts=@()}}
    $registry.receipts=@($registry.receipts|Where-Object {$_.request_id -ne $Receipt.request_id})+@($Receipt)
    Write-WmmaJson $path $registry
}

# Compatibility names used by existing public scripts. Their implementations live here.
function Read-Text {
    param([string]$Path)
    if(-not (Test-Path -LiteralPath $Path)){return ''}
    return Read-WmmaText $Path
}
function Write-Utf8NoBom {
    param([string]$Path,[string]$Text)
    Write-WmmaText $Path $Text
}
function Get-SectionText {
    param([string]$Text,[string]$Heading)
    return Get-WmmaSection $Text $Heading
}
