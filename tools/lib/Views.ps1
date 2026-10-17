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
