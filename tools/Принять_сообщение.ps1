param(
    [Parameter(Mandatory = $true)]
    [string]$Title,

    [string]$Text = '',

    [string]$TextPath = '',

    [ValidateSet('inbox', 'source')]
    [string]$Mode = 'inbox',

    [string]$RequestId = '',

    [switch]$SkipCheck
)

$ErrorActionPreference = 'Stop'

[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
$OutputEncoding = [System.Text.UTF8Encoding]::new()


. (Join-Path $PSScriptRoot '_lib.ps1')
function Convert-ToProjectFileName {
    param([string]$Value)

    $safe = $Value.Trim().ToLowerInvariant()
    $safe = [regex]::Replace($safe, '\s+', '_')
    $safe = $safe -replace '[\\/:*?"<>|]', ''
    $safe = $safe.Trim('_', '.', ' ')

    if ([string]::IsNullOrWhiteSpace($safe)) {
        throw 'Cannot build a safe file name from an empty message title.'
    }

    return $safe
}

function Get-RelativeProjectPath {
    param([string]$Path)

    return (($Path.Substring($root.Length).TrimStart('\', '/')) -replace '\\', '/')
}

$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
Invoke-WmmaToolMain -Root $root -Name $MyInvocation.MyCommand.Name -ScriptBlock {
$today = Get-Date -Format 'yyyy-MM-dd'
$codeFence = '```'

if (-not [string]::IsNullOrWhiteSpace($TextPath)) {
    $resolvedTextPath = (Resolve-Path -LiteralPath $TextPath).Path
    $Text = Get-Content -Raw -Encoding UTF8 -LiteralPath $resolvedTextPath
}

if ([string]::IsNullOrWhiteSpace($Text)) {
    throw 'Provide message text through -Text or -TextPath.'
}

$sourceReference = 'вручную через `tools/Принять_сообщение.ps1`'
$messageHash=Get-WmmaHash ($Text.Replace("`r`n","`n").Trim())
if(-not $RequestId){$RequestId='MSG-'+$messageHash.Substring(0,24)}
if($RequestId -notmatch '^[A-Za-z0-9-]+$'){throw 'Invalid request ID.'}
$receipt=Get-WmmaReceipt $root $RequestId
if($receipt){
    if($receipt.content_sha256 -cne $messageHash){throw 'Request ID already belongs to different content.'}
    "Accepted message into inbox: $($receipt.title) (already recorded: $RequestId)"
    return
}
$receipt=[pscustomobject]@{request_id=$RequestId;content_sha256=$messageHash;title=$Title;source_path=$null;scene_path=$null;state='accepted'}

if ($Mode -eq 'source') {
    $sourceRoot = Join-Path $root '08_Источники'
    $sourceFileName = "$today`_$(Convert-ToProjectFileName -Value $Title).md"
    $sourcePath = Join-Path $sourceRoot $sourceFileName

    $reuseSource=$false
    if (Test-Path -LiteralPath $sourcePath) {
        $existingSource=Read-WmmaText $sourcePath
        if((Get-WmmaMeta $existingSource 'request_id') -ne $RequestId -or (Get-WmmaMeta $existingSource 'content_sha256') -cne $messageHash){throw "Source file already exists for another message: $(Get-RelativeProjectPath $sourcePath)"}
        $reuseSource=$true
    }

    $sourceContent = @"
# $Title

---
type: source_note
status: new
canon_level: draft
received_real_date: $today
id: SRC-$([guid]::NewGuid().ToString('N'))
request_id: $RequestId
content_sha256: $messageHash
---

${codeFence}text
$Text
${codeFence}
"@

    if(-not $reuseSource){Write-WmmaText $sourcePath $sourceContent}
    $sourceReference = "``$(Get-RelativeProjectPath $sourcePath)``"
    $receipt.source_path=Get-RelativeProjectPath $sourcePath
}

$inboxPath = Join-Path $root '07_Черновики_и_идеи\Входящие_сообщения.md'
$inbox = Get-Content -Raw -Encoding UTF8 -LiteralPath $inboxPath
if($inbox -match ('(?m)^Request-ID:\s*'+[regex]::Escape($RequestId)+'\s*$')){
    Save-WmmaReceipt $root $receipt
    "Accepted message into inbox: $Title (recovered: $RequestId)";return
}
$entry = @"
### $today. $Title

Статус: новое.
Request-ID: $RequestId
Источник: $sourceReference

${codeFence}text
$Text
${codeFence}

"@

if ($inbox -notmatch '(?m)^## Новые сообщения\s*$') {
    throw 'Inbox section not found: ## Новые сообщения'
}

$inbox = $inbox -replace '(?m)^Пока нет новых необработанных сообщений\.\s*', ''
$inbox = [regex]::Replace(
    $inbox,
    '(?ms)(^## Новые сообщения\s*\r?\n)(.*?)(\r?\n## Обработанные входящие)',
    {
        param($match)

        $existing = $match.Groups[2].Value.Trim()
        $body = if ([string]::IsNullOrWhiteSpace($existing)) {
            "`r`n$entry"
        } else {
            "`r`n$entry`r`n$existing`r`n"
        }

        return $match.Groups[1].Value + $body.TrimEnd() + $match.Groups[3].Value
    },
    1
)

Write-WmmaText $inboxPath $inbox
Save-WmmaReceipt $root $receipt

if (-not $SkipCheck) {
    & (Join-Path $root 'tools/Завершить_ход.ps1')
    if($LASTEXITCODE -ne 0){throw 'Final turn validation failed.'}
}

"Accepted message into inbox: $Title"
}
