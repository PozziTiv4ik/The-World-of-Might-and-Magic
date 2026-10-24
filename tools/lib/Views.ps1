# Generated Markdown views: index header, tables and the row order of the previous build.

# Index header: H1, front matter with the build date, and the introductory paragraph.
function New-WmmaGeneratedHeader {
    param([string]$Title,[string]$Type,[string]$CanonLevel,[string]$Script,[string]$Intro)
    $lines=[Collections.Generic.List[string]]::new()
    foreach($line in @("# $Title",'','---',"type: $Type",'status: active',"canon_level: $CanonLevel","generated_real_date: $(Get-Date -Format 'yyyy-MM-dd')","generated_by: tools/$Script.ps1",'---','',$Intro)){$lines.Add($line)}
    return ,$lines
}

# Markdown table. Every row is an array of cells. -Escape formats each cell (for example
# Format-WmmaTableCell or Convert-WmmaCell); without it the cells are written as given.
function Add-WmmaMarkdownTable {
    param([Collections.Generic.List[string]]$Lines,[string[]]$Header,[object[]]$Rows,[scriptblock]$Escape)
    $Lines.Add('| '+($Header -join ' | ')+' |')
    $Lines.Add('| '+(@($Header|ForEach-Object {'---'}) -join ' | ')+' |')
    foreach($row in $Rows){
        $cells=if($Escape){@($row|ForEach-Object {& $Escape $_})}else{@($row)}
        $Lines.Add('| '+($cells -join ' | ')+' |')
    }
}

# Rows of an existing generated table keyed by the backticked path in -FileColumn.
# Keeps the manual row order, the cells and the enclosing "## " section of each row.
function Get-WmmaIndexRows {
    param([AllowEmptyString()][string]$Text,[int]$FileColumn,[string]$HeaderCell)
    $rows=@{};$sections=[Collections.Generic.List[string]]::new();$section=$null;$order=0
    foreach($line in ($Text -split "\r?\n")){
        if($line -match '^##\s+(.+?)\s*$'){$section=$Matches[1].Trim();$sections.Add($section);continue}
        $cells=Convert-WmmaTableRow -Line $line
        if($null -eq $cells -or $cells.Count -le $FileColumn -or $cells[0] -eq $HeaderCell){continue}
        if($cells[$FileColumn] -match '`([^`]+)`'){
            $order++
            $rows[$Matches[1]]=[pscustomobject]@{Order=$order;Cells=$cells;Section=$section}
        }
    }
    return [pscustomobject]@{Rows=$rows;Sections=@($sections)}
}

# Writes a generated index (an unchanged view keeps its build date) and checks the
# project unless the caller deferred validation.
function Save-WmmaIndex {
    param([string]$Root,[string]$Path,[Collections.Generic.List[string]]$Lines,[string]$Label,[switch]$SkipCheck)
    Write-WmmaGeneratedText -Path $Path -Text (($Lines -join "`n").TrimEnd()+"`n")
    if(-not $SkipCheck){Invoke-WmmaTool -Root $Root -Name 'Проверить_проект'}
    "Built ${Label}: $(Get-WmmaRelativePath -Root $Root $Path)"
}

# Character asset cards of 05_Активы_персонажей with the fields of the generated indexes.
function Get-WmmaAssetCards {
    param([string]$Root)
    $assetRoot=Join-Path $Root '05_Активы_персонажей'
    foreach($file in @(Get-ChildItem -LiteralPath $assetRoot -Recurse -File -Filter '*.md'|Sort-Object FullName)){
        if($file.Name -in @('00_Индекс_активов.md','README.md')){continue}
        $text=Read-WmmaText $file.FullName
        if((Get-WmmaMeta $text 'type') -ne 'character_asset'){continue}
        $parent=Get-WmmaMeta $text 'parent_asset'
        [pscustomobject]@{
            Owner=(Get-WmmaMeta $text 'owner');OwnerId=(Get-WmmaMeta $text 'owner_id')
            Asset=(Get-WmmaTitle $text);Kind=(Get-WmmaMeta $text 'asset_kind');Status=(Get-WmmaMeta $text 'status')
            Parent=$(if($parent -and $parent -ne 'null'){$parent}else{'-'});Visual=(Get-WmmaMeta $text 'visual_status')
            File=(Get-WmmaRelativePath $Root $file.FullName)
        }
    }
}

# Replaces the asset table of an owner index (the first table headed «Актив») or, without
# one, appends it under «## Активы». The rest of the document is kept as written.
function Set-WmmaOwnerIndexTable {
    param([string]$Text,[string[]]$Table)
    $lines=[Collections.Generic.List[string]]::new()
    foreach($line in ($Text.TrimEnd() -split "\r?\n")){$lines.Add($line)}
    $start=-1
    for($i=0;$i -lt $lines.Count;$i++){if($lines[$i] -match '^\|\s*Актив\s*\|'){$start=$i;break}}
    if($start -lt 0){
        foreach($line in @('','## Активы','')+@($Table)){$lines.Add($line)}
    }else{
        $end=$start
        while($end -lt $lines.Count -and $lines[$end] -match '^\|'){$end++}
        $lines.RemoveRange($start,$end-$start)
        $lines.InsertRange($start,[string[]]$Table)
    }
    return (($lines -join "`n").TrimEnd()+"`n")
}

# Owner indexes of character assets: one per owner_id (or owner name without it), where an
# index of that owner already is or in the folder of most of the owner's cards. Only the
# asset table is generated; summaries and restrictions are the author's notes.
function Get-WmmaOwnerAssetIndexViews {
    param([string]$Root,[object[]]$Cards)
    $assetRoot=Join-Path $Root '05_Активы_персонажей'
    $indexes=@{}
    foreach($file in @(Get-ChildItem -LiteralPath $assetRoot -Recurse -File -Filter '00_Индекс_активов.md'|Where-Object {$_.DirectoryName -ne $assetRoot})){
        $text=Read-WmmaText $file.FullName
        if((Get-WmmaMeta $text 'type') -eq 'character_asset_owner_index'){$indexes[(Get-WmmaRelativePath $Root $file.FullName)]=$text}
    }
    $header=@('Актив','Тип','Родитель','Визуал','Файл')
    foreach($group in @($Cards|Group-Object {if($_.OwnerId){$_.OwnerId}else{'name:'+$_.Owner}}|Sort-Object Name)){
        $cards=@($group.Group);$ownerId=$cards[0].OwnerId;$owner=$cards[0].Owner
        $path=@($indexes.Keys|Sort-Object|Where-Object {($ownerId -and (Get-WmmaMeta $indexes[$_] 'owner_id') -ceq $ownerId) -or (Get-WmmaMeta $indexes[$_] 'owner') -ceq $owner})|Select-Object -First 1
        if(-not $path){
            $folder=@($cards|Group-Object {$_.File -replace '/[^/]+$',''}|Sort-Object @{Expression='Count';Descending=$true},Name)[0].Name
            $path="$folder/00_Индекс_активов.md"
        }
        $previous=if($indexes.ContainsKey($path)){$indexes[$path]}else{''}
        $rows=(Get-WmmaIndexRows -Text $previous -FileColumn 4 -HeaderCell 'Актив').Rows
        $ordered=@($cards|Sort-Object @{Expression={if($rows.ContainsKey($_.File)){$rows[$_.File].Order}else{100000}}},Asset,File)
        $table=[Collections.Generic.List[string]]::new()
        Add-WmmaMarkdownTable $table $header @($ordered|ForEach-Object {,@($_.Asset,$_.Kind,$_.Parent,$_.Visual,"``$($_.File)``")}) -Escape {param($Value) Format-WmmaTableCell -Empty '-' $Value}
        if($previous){
            $text=Set-WmmaMeta $previous 'generated_by' 'tools/Собрать_индекс_активов.ps1'
            if($ownerId){$text=Set-WmmaMeta $text 'owner_id' $ownerId}
            $text=Set-WmmaOwnerIndexTable $text @($table)
        }else{
            $lines=@("# Активы: $owner",'','---','type: character_asset_owner_index','status: active','canon_level: active',"owner: $owner")
            if($ownerId){$lines+="owner_id: $ownerId"}
            $lines+=@('generated_by: tools/Собрать_индекс_активов.ps1','---','','Таблица собирается из карточек активов этого владельца командой `tools/Собрать_индекс_активов.ps1`.','','## Активы','')
            $text=((@($lines)+@($table)) -join "`n")+"`n"
        }
        [pscustomobject]@{Path=$path;Text=$text;OwnerId=$ownerId;Owner=$owner;Count=$cards.Count}
    }
}
