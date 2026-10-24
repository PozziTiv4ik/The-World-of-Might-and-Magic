# Shared primitives. All paths are relative to the explicit project root.
function Read-WmmaText {
    param([string]$Path,[switch]$AllowMissing)
    # -AllowMissing: an absent optional view reads as empty text.
    if ($AllowMissing -and -not (Test-Path -LiteralPath $Path)) { return '' }
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

function Read-WmmaRegistry {
    param([string]$Root,[string]$Name)
    $path=Resolve-WmmaPath $Root "09_Реестры/$Name.json"
    if(-not (Test-Path -LiteralPath $path)){
        throw "Registry missing: $Name.json. Restore the authoritative JSON from a verified backup."
    }
    return Read-WmmaJson $path
}

function Write-WmmaJson {
    param([string]$Path, [object]$Value)
    Write-WmmaText $Path (($Value | ConvertTo-Json -Depth 50).TrimEnd() + "`n")
}

function Write-WmmaRegistry {
    param([string]$Root,[string]$Name,[object]$Value)
    Write-WmmaJson (Resolve-WmmaPath $Root "09_Реестры/$Name.json") $Value
}

function Get-WmmaHash {
    param([string]$Text)
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($Text)))).Replace('-', '').ToLowerInvariant() }
    finally { $sha.Dispose() }
}

# Identity of an author message: line endings normalized to LF, outer whitespace trimmed.
function Get-WmmaMessageHash {
    param([string]$Text)
    return Get-WmmaHash ($Text.Replace("`r`n", "`n").Trim())
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
    $base = $Root.TrimEnd('\', '/')
    # A path outside the project (an absolute reference in a card) is reported as is.
    if (-not $Path.StartsWith($base, [StringComparison]::OrdinalIgnoreCase)) { return $Path }
    return $Path.Substring($base.Length).TrimStart('\', '/').Replace('\', '/')
}

# A document reference: absolute, from the project root or, with -FromFile, from the
# referencing document's folder. Returns the full path of the first existing candidate.
function Resolve-WmmaReference {
    param([string]$Root, [string]$Reference, [string]$FromFile)
    if ([string]::IsNullOrWhiteSpace($Reference)) { return $null }
    $normalized = [regex]::Replace($Reference, '[\\/]', [string][IO.Path]::DirectorySeparatorChar)
    $candidates = if ([IO.Path]::IsPathRooted($normalized)) { @($normalized) } else {
        @(Join-Path $Root $normalized) + @(if ($FromFile) { Join-Path (Split-Path -Parent $FromFile) $normalized })
    }
    foreach ($candidate in $candidates) { if (Test-Path -LiteralPath $candidate) { return [IO.Path]::GetFullPath($candidate) } }
    return $null
}

function Test-WmmaReference {
    param([string]$Root, [string]$Reference, [string]$FromFile)
    if ($Reference -match '^[a-z][a-z0-9+.-]*://') { return $true }
    return $null -ne (Resolve-WmmaReference $Root $Reference -FromFile $FromFile)
}

# Inline project reference for generated Markdown: backticked path with forward slashes;
# URLs and already backticked values are kept.
function Format-WmmaProjectReference {
    param([string]$Value)
    if ([string]::IsNullOrWhiteSpace($Value)) { return $null }
    $reference = $Value.Trim() -replace '\\', '/'
    if ($reference -match '^`.+`$' -or $reference -match '^[a-z]+://') { return $reference }
    return "``$reference``"
}

function Get-WmmaMeta {
    param([string]$Text, [string]$Field, [AllowNull()][object]$Default = '')
    $block = [regex]::Match($Text, '(?s)\A(?:\uFEFF)?# [^\r\n]+\r?\n\s*---\r?\n(.*?)\r?\n---')
    if ($block.Success) {
        $entry = [regex]::Match($block.Groups[1].Value, '(?m)^' + [regex]::Escape($Field) + ':[ \t]*([^\r\n]*)')
        if ($entry.Success) { return $entry.Groups[1].Value.Trim() }
    }
    return $Default
}

function Get-WmmaTitle {
    param([string]$Text)
    $heading=[regex]::Match($Text,'(?m)^#\s+(.+?)\s*$')
    if($heading.Success){return $heading.Groups[1].Value.Trim()}
    return 'Без названия'
}

function Convert-WmmaTableRow {
    param([string]$Line)
    if($Line -notmatch '^\|.+\|$' -or $Line -match '^\|\s*-'){return $null}
    return ,($Line.Trim('|') -split '\|' | ForEach-Object {$_.Trim()})
}

function Format-WmmaTableCell {
    param([AllowNull()][object]$Value,[string]$Empty='')
    if($null -eq $Value -or [string]::IsNullOrWhiteSpace([string]$Value)){return $Empty}
    return (([string]$Value -replace '\|','/') -replace '\r?\n',' ').Trim()
}

function Convert-WmmaFileName {
    param([string]$Value,[switch]$Lowercase)
    $safe=([regex]::Replace($Value.Trim(),'\s+','_') -replace '[\\/:*?"<>|]','').Trim('_','.',' ')
    if(-not $safe){throw 'Cannot build a file name from an empty title.'}
    if($Lowercase){return $safe.ToLowerInvariant()}
    return $safe
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
    $section=@(Get-WmmaMarkdownSections $Text 2 | Where-Object {$_.heading -ceq $Heading})|Select-Object -First 1
    if ($section) { return $section.text.Trim() }
    return ''
}

# A generated view changes only when its contents change. Keep the previous
# generation date and bytes if a rebuild merely proposes a newer date.
function Write-WmmaGeneratedText {
    param([string]$Path,[string]$Text,[ValidateSet('markdown','json')][string]$Format='markdown')
    if(Test-Path -LiteralPath $Path){
        $previous=Read-WmmaText $Path
        $pattern=if($Format -eq 'json'){'(?m)^  "updated_real_date": "\d{4}-\d{2}-\d{2}"'}else{'(?m)^generated_real_date: \d{4}-\d{2}-\d{2}'}
        $before=[regex]::Replace($previous.Replace("`r`n","`n"),$pattern,'<generation-date>')
        $after=[regex]::Replace($Text.Replace("`r`n","`n"),$pattern,'<generation-date>')
        if($before -ceq $after){return}
    }
    Write-WmmaText $Path $Text
}

function Get-WmmaMarkdownSections {
    param([string]$Text,[int]$Level=2)
    $headings=[Collections.Generic.List[object]]::new();$fenceChar='';$fenceLength=0
    foreach($line in [regex]::Matches($Text,'(?m)^[^\r\n]*(?:\r?\n|$)')){
        if(-not $line.Length){continue}
        $value=$line.Value.TrimEnd([char]13,[char]10)
        $fence=[regex]::Match($value,'^ {0,3}(`{3,}|~{3,})(.*)$')
        if($fence.Success){
            $marker=$fence.Groups[1].Value
            if(-not $fenceChar){$fenceChar=[string]$marker[0];$fenceLength=$marker.Length}
            elseif([string]$marker[0] -eq $fenceChar -and $marker.Length -ge $fenceLength -and -not $fence.Groups[2].Value.Trim()){$fenceChar='';$fenceLength=0}
            continue
        }
        if($fenceChar){continue}
        $h=[regex]::Match($value,'^(#{1,6})[ \t]+(.+?)[ \t]*$')
        if($h.Success){$headings.Add([pscustomobject]@{level=$h.Groups[1].Value.Length;heading=$h.Groups[2].Value;index=$line.Index;body_start=$line.Index+$line.Length})}
    }
    for($i=0;$i -lt $headings.Count;$i++){
        $h=$headings[$i];if($h.level -ne $Level){continue}
        $end=$Text.Length
        for($j=$i+1;$j -lt $headings.Count;$j++){if($headings[$j].level -le $Level){$end=$headings[$j].index;break}}
        [pscustomobject]@{heading=$h.heading;index=$h.index;body_start=$h.body_start;end=$end;text=$Text.Substring($h.body_start,$end-$h.body_start)}
    }
}

function Get-WmmaInboxEntries {
    param([string]$Text)
    foreach($section in @(Get-WmmaMarkdownSections $Text 3)){
        $value=$Text.Substring($section.index,$section.end-$section.index)
        [pscustomobject]@{Value=$value;Index=$section.index;Length=$value.Length;Groups=@([pscustomobject]@{Value=$value},[pscustomobject]@{Value=$section.heading},[pscustomobject]@{Value=$section.text})}
    }
}

function Get-WmmaEntryField {
    param([string]$Text,[string]$Field)
    $fence=[regex]::Match($Text,'(?m)^ {0,3}(?:`{3,}|~{3,})')
    $header=if($fence.Success){$Text.Substring(0,$fence.Index)}else{$Text}
    $match=[regex]::Match($header,'(?m)^'+[regex]::Escape($Field)+':[ \t]*([^\r\n]*)')
    if($match.Success){return $match.Groups[1].Value.Trim()}
    return ''
}

function Get-WmmaTextFence {
    param([string]$Text)
    $max=2;foreach($run in [regex]::Matches($Text,'`+')){$max=[Math]::Max($max,$run.Length)}
    return ('`'*($max+1))
}

function Get-WmmaRawMessage {
    param([string]$Text)
    $match=[regex]::Match($Text,'(?ms)^ {0,3}(?<fence>`{3,}|~{3,})text[ \t]*\r?\n(?<body>.*?)\r?\n {0,3}\k<fence>[ \t]*(?:\r?\n|$)')
    if(-not $match.Success){return $null}
    return $match.Groups['body'].Value
}

function Test-WmmaSourcePayload {
    param([string]$Text,[string]$Hash)
    $raw=Get-WmmaRawMessage $Text
    return $null -ne $raw -and (Get-WmmaMessageHash $raw) -ceq $Hash
}

function Assert-WmmaSingleLine {
    param([string]$Value,[string]$Name,[switch]$AllowEmpty)
    if($Value -match '[\r\n]' -or (-not $AllowEmpty -and [string]::IsNullOrWhiteSpace($Value))){throw "$Name must be a single nonempty line."}
}

function Assert-WmmaReceiptPayload {
    param([string]$Root,[object]$Receipt)
    if($Receipt.source_path){
        $source=Read-WmmaText (Resolve-WmmaPath $Root $Receipt.source_path)
        if((Get-WmmaMeta $source 'request_id') -cne $Receipt.request_id -or -not (Test-WmmaSourcePayload $source $Receipt.content_sha256)){throw 'Receipt source payload is missing or changed.'}
    }else{
        $inbox=Read-WmmaText (Join-Path $Root '07_Черновики_и_идеи/Входящие_сообщения.md')
        $entries=@(Get-WmmaInboxEntries $inbox|Where-Object {(Get-WmmaEntryField $_.Value 'Request-ID') -ceq $Receipt.request_id})
        if($entries.Count -ne 1 -or -not (Test-WmmaSourcePayload $entries[0].Value $Receipt.content_sha256)){throw 'Receipt inbox payload is missing, duplicated or changed.'}
    }
    if($Receipt.scene_path){
        $scene=Read-WmmaText (Resolve-WmmaPath $Root $Receipt.scene_path)
        if((Get-WmmaMeta $scene 'request_id') -cne $Receipt.request_id){throw 'Receipt points to another scene.'}
    }
}

function Get-WmmaImageSize {
    param([string]$Path)
    $stream=$null;$image=$null
    try{
        Add-Type -AssemblyName System.Drawing
        $stream=[IO.File]::OpenRead($Path)
        $image=[Drawing.Image]::FromStream($stream)
        return [pscustomobject]@{Width=$image.Width;Height=$image.Height}
    }catch{return $null}finally{if($image){$image.Dispose()};if($stream){$stream.Dispose()}}
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

# Decision lifecycle. Pending: state pending or a DEC-PENDING-* ID; accepted: DEC-NNN in state accepted.
function Test-WmmaPendingDecision {
    param([object]$Decision)
    return $Decision.state -eq 'pending' -or $Decision.id -like 'DEC-PENDING-*'
}

function Test-WmmaAcceptedDecision {
    param([object]$Decision)
    return $Decision.state -eq 'accepted' -and $Decision.id -match '^DEC-\d{3}$'
}

# Portrait column of the character index: card portrait_status -> index word.
function Get-WmmaPortraitStatusWord {
    param([AllowNull()][string]$Status, [AllowNull()][string]$Fallback = 'нужен')
    switch ($Status) { 'available' { return 'есть' } 'planned' { return 'запланирован' } 'missing' { return 'нужен' } }
    if ([string]::IsNullOrWhiteSpace($Fallback)) { return 'нужен' }
    return $Fallback
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

# --- Entity IDs, names, merges and protected canon -------------------------------------

# ID of a document: the prefix of its type and a number of at least four digits or a GUID.
# Returns a problem message or $null.
function Test-WmmaEntityIdFormat {
    param([string]$Id,[string]$Type)
    $match=[regex]::Match([string]$Id,'^(CHAR|LOC|ASSET|SCENE|SRC|LORE)-(?:\d{4,}|[0-9a-f]{32})$')
    if(-not $match.Success){return "Invalid entity ID format: '$Id'"}
    $expected=Get-WmmaIdPrefix $Type
    if($match.Groups[1].Value -cne $expected){return "Entity ID prefix does not fit type ${Type} (expected $expected-): $Id"}
    return $null
}

# Next free number for a document type: the maximum over the entity registry and the id
# lines of the canon folders, plus one.
function New-WmmaEntityId {
    param([string]$Root,[string]$Type)
    $prefix=Get-WmmaIdPrefix $Type
    $max=0
    $graphPath=Join-Path $Root '09_Реестры/Сущности.json'
    if(Test-Path -LiteralPath $graphPath){
        foreach($entity in @((Read-WmmaJson $graphPath).entities)){
            $match=[regex]::Match([string]$entity.id,'^'+$prefix+'-(\d+)$')
            if($match.Success){$max=[Math]::Max($max,[int]$match.Groups[1].Value)}
        }
    }
    foreach($directory in @('01_Кампания/Ветки','02_Лор','03_Персонажи','04_Локации','05_Активы_персонажей','08_Источники')){
        $folder=Join-Path $Root $directory
        if(-not (Test-Path -LiteralPath $folder)){continue}
        foreach($file in Get-ChildItem -LiteralPath $folder -Recurse -File -Filter '*.md'){
            foreach($match in [regex]::Matches((Read-WmmaText $file.FullName),'(?m)^id:[ \t]*'+$prefix+'-(\d+)[ \t]*\r?$')){$max=[Math]::Max($max,[int]$match.Groups[1].Value)}
        }
    }
    return '{0}-{1:D4}' -f $prefix,($max+1)
}

# One-line JSON array of metadata values in the project style: ["A", "B"].
function Format-WmmaMetaArray {
    param([AllowEmptyCollection()][string[]]$Values)
    $items=@($Values|Where-Object {-not [string]::IsNullOrWhiteSpace($_)}|ForEach-Object {ConvertTo-Json -InputObject ([string]$_) -Compress})
    return '['+($items -join ', ')+']'
}

# Values with one ID replaced by another; duplicates removed, order kept.
function Get-WmmaReplacedIds {
    param([object[]]$Values,[string]$From,[string]$To)
    $result=[Collections.Generic.List[string]]::new()
    foreach($value in @($Values)){
        if($null -eq $value -or [string]::IsNullOrWhiteSpace([string]$value)){continue}
        $next=if([string]$value -ceq $From){$To}else{[string]$value}
        if(-not $result.Contains($next)){$result.Add($next)}
    }
    return $result
}

# Throws unless every ID names an existing entity that is not a merged redirect and, with
# -Type, has one of the given document types.
function Assert-WmmaEntityIds {
    param([object]$Graph,[object[]]$Ids,[string[]]$Type=@(),[string]$Label='entity')
    $byId=@{};foreach($entity in $Graph.entities){$byId[$entity.id]=$entity}
    foreach($id in @($Ids)){
        if([string]::IsNullOrWhiteSpace([string]$id) -or -not $byId.ContainsKey([string]$id)){throw "Unknown $Label ID: $id"}
        $entity=$byId[[string]$id]
        if($entity.merged_into){throw "$Label ID $id is merged into $($entity.merged_into); use the main ID."}
        if($Type.Count -and $Type -notcontains $entity.type){throw "$Label ID $id is a $($entity.type); expected: $($Type -join ', ')."}
    }
}

# Comparison key of a title or alias: lower case, ё as е, words separated by one space.
function ConvertTo-WmmaNameKey {
    param([AllowNull()][string]$Value)
    if([string]::IsNullOrWhiteSpace($Value)){return ''}
    return ([regex]::Replace($Value.ToLowerInvariant().Replace('ё','е'),'[^\p{L}\p{Nd}]+',' ')).Trim()
}

# Two keys may name one entity when they are equal or every word of the shorter key occurs
# in the longer one («закхерон» and «закхерон багровый»).
function Test-WmmaSimilarNameKeys {
    param([string]$Left,[string]$Right)
    if(-not $Left -or -not $Right){return $false}
    if($Left -ceq $Right){return $true}
    $short=@($Left -split ' ');$long=@($Right -split ' ')
    if($short.Count -gt $long.Count){$swap=$short;$short=$long;$long=$swap}
    foreach($word in $short){if($long -notcontains $word){return $false}}
    return $true
}

# Graph entities whose title or alias is similar to one of the names. Merged redirect
# cards and the IDs in -Except (confirmed different entities) are skipped.
function Find-WmmaSimilarEntities {
    param([object[]]$Entities,[string[]]$Names,[string[]]$Except=@())
    $keys=@($Names|ForEach-Object {ConvertTo-WmmaNameKey $_}|Where-Object {$_}|Sort-Object -Unique)
    foreach($entity in $Entities){
        if($entity.merged_into -or $Except -contains $entity.id){continue}
        $hit=$null
        foreach($candidate in @(@($entity.name)+@($entity.aliases)|ForEach-Object {ConvertTo-WmmaNameKey $_}|Where-Object {$_}|Sort-Object -Unique)){
            foreach($key in $keys){if(Test-WmmaSimilarNameKeys $key $candidate){$hit=$candidate;break}}
            if($hit){break}
        }
        if($hit){[pscustomobject]@{id=$entity.id;name=$entity.name;path=$entity.path;match=$hit}}
    }
}

# Pairs of entities with similar titles or aliases (see Test-WmmaSimilarNameKeys) among the
# given entities. Merged cards and pairs confirmed by distinct_from on either side are skipped.
function Get-WmmaSimilarNamePairs {
    param([object[]]$Entities)
    $names=[Collections.Generic.List[object]]::new();$byWord=@{}
    foreach($entity in @($Entities|Where-Object {-not $_.merged_into})){
        foreach($key in @(@($entity.name)+@($entity.aliases)|ForEach-Object {ConvertTo-WmmaNameKey $_}|Where-Object {$_}|Sort-Object -Unique)){
            $item=[pscustomobject]@{entity=$entity;key=$key;words=@($key -split ' '|Sort-Object -Unique)}
            $names.Add($item)
            foreach($word in $item.words){
                if(-not $byWord.ContainsKey($word)){$byWord[$word]=[Collections.Generic.List[object]]::new()}
                $byWord[$word].Add($item)
            }
        }
    }
    $pairs=@{}
    foreach($item in $names){
        $candidates=$null
        foreach($word in $item.words){if($null -eq $candidates -or $byWord[$word].Count -lt $candidates.Count){$candidates=$byWord[$word]}}
        foreach($other in @($candidates)){
            if($other.entity.id -ceq $item.entity.id -or $other.words.Count -lt $item.words.Count){continue}
            $contained=$true
            foreach($word in $item.words){if($other.words -notcontains $word){$contained=$false;break}}
            if(-not $contained){continue}
            $a=$item.entity;$b=$other.entity
            if(@($a.distinct_from) -contains $b.id -or @($b.distinct_from) -contains $a.id){continue}
            $ids=@($a.id,$b.id|Sort-Object)
            $key=$ids -join '|'
            if(-not $pairs.ContainsKey($key)){$pairs[$key]=[pscustomobject]@{ids=$ids;left=$item.key;right=$other.key}}
        }
    }
    return @($pairs.Keys|Sort-Object|ForEach-Object {$pairs[$_]})
}

function Get-WmmaProtectionManifestPath {
    param([string]$Root)
    return Join-Path $Root '10_Обслуживание/Миграция_v2.json'
}

# Accepts new bytes of protected canon and records the date, the previous and the new hash
# and the reason in the manifest's amendments journal. With -Add a file that is not protected
# yet joins the manifest with the date and the reason of its protection. Returns the number
# of changed entries.
function Update-WmmaProtectedFiles {
    param([string]$Root,[string[]]$Paths,[string]$Reason,[switch]$Add)
    if([string]::IsNullOrWhiteSpace($Reason)){throw 'Protected canon changes need a recorded reason: a typo, formatting, a merge of duplicate IDs, a chapter close or an accepted retcon.'}
    $manifestPath=Get-WmmaProtectionManifestPath $Root
    $manifest=Read-WmmaJson $manifestPath
    $files=[Collections.Generic.List[object]]::new();foreach($item in @($manifest.protected_files|Where-Object {$_})){$files.Add($item)}
    $journal=[Collections.Generic.List[object]]::new();foreach($item in @($manifest.amendments|Where-Object {$_})){$journal.Add($item)}
    $today=Get-Date -Format 'yyyy-MM-dd';$changed=0
    foreach($path in @($Paths|Where-Object {$_})){
        $full=Resolve-WmmaPath $Root $path
        $relative=Get-WmmaRelativePath $Root $full
        if(-not (Test-Path -LiteralPath $full -PathType Leaf)){throw "Protected file is missing: $relative"}
        $hash=(Get-FileHash -LiteralPath $full -Algorithm SHA256).Hash.ToLowerInvariant()
        $entry=@($files|Where-Object {$_.path -ceq $relative})|Select-Object -First 1
        if(-not $entry){
            if(-not $Add){throw "Not a protected file: $relative. Use -Add to protect a new file."}
            $files.Add([pscustomobject][ordered]@{path=$relative;sha256=$hash;protected_real_date=$today;reason=$Reason});$changed++
        }elseif($entry.sha256 -cne $hash){
            $journal.Add([pscustomobject][ordered]@{date=$today;path=$relative;before=$entry.sha256;after=$hash;reason=$Reason})
            $entry.sha256=$hash;$changed++
        }
    }
    if($changed){
        $manifest.protected_files=@($files)
        $manifest|Add-Member -NotePropertyName amendments -NotePropertyValue @($journal) -Force
        Write-WmmaJson $manifestPath $manifest
    }
    return $changed
}

# Keeps the full text of a replaced document as protected history of another card; the
# check compares the text hash after normalizing line endings.
function Add-WmmaHistoryRecord {
    param([string]$Root,[string]$OriginalPath,[string]$HistoryPath,[string]$Text,[string]$Note)
    $full=Resolve-WmmaPath $Root $HistoryPath
    if(Test-Path -LiteralPath $full){throw "History record already exists: $HistoryPath"}
    $body=$Text.Replace("`r`n","`n").TrimEnd()
    $title=[IO.Path]::GetFileNameWithoutExtension($OriginalPath)
    $document=(@("# История: $title",'','---','type: history_record','status: active','canon_level: support',"original_path: $OriginalPath",'---','',$Note,'','<!-- ORIGINAL DOCUMENT -->',$body) -join "`n")+"`n"
    Write-WmmaText $full $document
    $manifestPath=Get-WmmaProtectionManifestPath $Root
    $manifest=Read-WmmaJson $manifestPath
    $manifest.history=@($manifest.history)+@([pscustomobject][ordered]@{original_path=$OriginalPath;history_path=$HistoryPath;text_sha256=(Get-WmmaHash $body)})
    Write-WmmaJson $manifestPath $manifest
}

# Marks a fact as replaced by a newer one. The old record keeps its text, evidence and
# knowers; context pointers move to the newer fact.
function Set-WmmaFactSuperseded {
    param([string]$Root,[string]$FactId,[string]$ReplacedBy,[string]$Note='')
    if($FactId -ceq $ReplacedBy){throw 'A fact cannot replace itself.'}
    $knowledge=Read-WmmaRegistry $Root 'Знания'
    $byId=@{};foreach($fact in $knowledge.facts){$byId[$fact.id]=$fact}
    foreach($id in @($FactId,$ReplacedBy)){if(-not $byId.ContainsKey($id)){throw "Unknown fact: $id"}}
    $old=$byId[$FactId]
    if($old.superseded_by -and $old.superseded_by -cne $ReplacedBy){throw "$FactId is already replaced by $($old.superseded_by)."}
    $cursor=$ReplacedBy;$seen=@{}
    while($cursor -and -not $seen.ContainsKey($cursor)){
        if($cursor -ceq $FactId){throw "Replacing $FactId by $ReplacedBy would make a cycle."}
        $seen[$cursor]=$true;$cursor=$byId[$cursor].superseded_by
    }
    $changed=$false
    if($old.superseded_by -cne $ReplacedBy){
        $old|Add-Member -NotePropertyName superseded_by -NotePropertyValue $ReplacedBy -Force
        $old|Add-Member -NotePropertyName superseded_on -NotePropertyValue (Get-Date -Format 'yyyy-MM-dd') -Force
        if($Note){$old|Add-Member -NotePropertyName superseded_note -NotePropertyValue $Note -Force}
        Write-WmmaRegistry $Root 'Знания' $knowledge;$changed=$true
    }
    $state=Read-WmmaRegistry $Root 'Контекст';$moved=0
    foreach($holder in @($state)+@($state.branches)){
        if(@($holder.fact_ids) -ccontains $FactId){$holder.fact_ids=@(Get-WmmaReplacedIds $holder.fact_ids $FactId $ReplacedBy);$moved++}
    }
    if($moved){Write-WmmaRegistry $Root 'Контекст' $state;$changed=$true}
    return [pscustomobject]@{changed=$changed;moved_pointers=$moved}
}
