# Shared validation primitives: the problem set and report, ID set comparison,
# legacy paths of the old layout and portrait file checks.

# The problem set of the running check lives in its script scope, so Add-Problem needs
# no explicit argument. A nested check (& tools/Проверить_*.ps1) keeps its own set.
function New-WmmaProblemSet {
    $script:WmmaProblems=[pscustomobject]@{Errors=[Collections.Generic.List[string]]::new();Warnings=[Collections.Generic.List[string]]::new()}
    return $script:WmmaProblems
}

function Add-Problem {
    param([string]$Kind,[string]$Message)
    if($Kind -eq 'Error'){$script:WmmaProblems.Errors.Add($Message)}else{$script:WmmaProblems.Warnings.Add($Message)}
}

# Summary, then warnings and errors in alphabetical order. Does not stop the script.
function Write-WmmaProblemReport {
    param([object]$Summary)
    $problems=$script:WmmaProblems
    if($null -ne $Summary){$Summary|Format-List}
    if($problems.Warnings.Count -gt 0){"`nWarnings:";$problems.Warnings|Sort-Object|ForEach-Object {"- $_"}}
    if($problems.Errors.Count -gt 0){"`nErrors:";$problems.Errors|Sort-Object|ForEach-Object {"- $_"}}
}

# Final report of a standalone check: exit code 1 on errors, otherwise the success message.
function Complete-WmmaCheck {
    param([object]$Summary,[string]$SuccessMessage)
    Write-WmmaProblemReport $Summary
    if($script:WmmaProblems.Errors.Count -gt 0){exit 1}
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
function Get-WmmaLegacyPaths {
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

function Get-WmmaLegacyReferences {
    foreach($path in Get-WmmaLegacyPaths){
        # Only the root-level dictionary is legacy; 03_Персонажи/00_Словарь_имен_и_алиасов.md is current.
        if($path -eq '00_Словарь_имен_и_алиасов.md'){continue}
        $path
        if($path.Contains('/')){$path.Replace('/','\')}
    }
}

# Portrait proportions: 3:4 within 0.04; -AllowLegacy also accepts the old 2:3 portraits.
function Test-WmmaPortraitRatio {
    param([int]$Width,[int]$Height,[switch]$AllowLegacy)
    if($Width -le 0 -or $Height -le 0 -or $Width -ge $Height){return $false}
    $ratio=$Width/$Height
    return ([Math]::Abs($ratio-0.75) -le 0.04 -or ($AllowLegacy -and [Math]::Abs($ratio-(2/3)) -le 0.04))
}

# Portrait file of a card with portrait_status=available. Problem is missing (no file),
# unreadable (no image size), ratio (neither 3:4 nor 2:3) or empty. -CardPath also resolves
# the path from the card's folder. Each caller keeps its own severity per problem.
function Test-WmmaPortraitFile {
    param([string]$Root,[string]$Portrait,[string]$CardPath)
    $path=if($Portrait -ne 'null'){Resolve-WmmaReference $Root $Portrait -FromFile $CardPath}
    if(-not $path){return [pscustomobject]@{Problem='missing';Path=$null;Size=$null}}
    $size=Get-WmmaImageSize $path
    $problem=if(-not $size){'unreadable'}elseif(-not (Test-WmmaPortraitRatio $size.Width $size.Height -AllowLegacy)){'ratio'}else{''}
    return [pscustomobject]@{Problem=$problem;Path=$path;Size=$size}
}

# Portrait folders (direct children of the portrait root) that contain an image.
function Get-WmmaPortraitImageFolders {
    param([string]$PortraitRoot)
    Get-ChildItem -LiteralPath $PortraitRoot -Directory|Where-Object {
        @(Get-ChildItem -LiteralPath $_.FullName -File|Where-Object {$_.Extension -match '^\.(jpg|jpeg|png|webp)$'}).Count -gt 0
    }
}
