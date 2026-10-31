param(
    [Parameter(Mandatory = $true)]
    [string]$Title,

    [string]$Text = '',

    [string]$TextPath = '',

    [ValidateSet('inbox', 'source')]
    [string]$Mode = 'source',

    [string]$RequestId = '',

    [switch]$SkipCheck
)

$ErrorActionPreference = 'Stop'

[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
$OutputEncoding = [System.Text.UTF8Encoding]::new()

. (Join-Path $PSScriptRoot '_lib.ps1')

$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
Invoke-VoidlightToolMain -Root $root -Name $MyInvocation.MyCommand.Name -ScriptBlock {
$today = Get-Date -Format 'yyyy-MM-dd'
Assert-VoidlightSingleLine $Title 'Title'

if (-not [string]::IsNullOrWhiteSpace($TextPath)) {
    $resolvedTextPath = (Resolve-Path -LiteralPath $TextPath).Path
    $Text = Read-VoidlightText $resolvedTextPath
}

if ([string]::IsNullOrWhiteSpace($Text)) {
    throw 'Provide message text through -Text or -TextPath.'
}
$codeFence=Get-VoidlightTextFence $Text

$sourceReference = 'вручную через `tools/Принять_сообщение.ps1`'
$messageHash=Get-VoidlightMessageHash $Text
if(-not $RequestId){$RequestId='MSG-'+$messageHash.Substring(0,24)}
if($RequestId -notmatch '^[A-Za-z0-9-]+$'){throw 'Invalid request ID.'}
$receipt=Get-VoidlightReceipt $root $RequestId
if($receipt){
    if($receipt.content_sha256 -cne $messageHash){throw 'Request ID already belongs to different content.'}
    Assert-VoidlightReceiptPayload $root $receipt
    "Accepted message into inbox: $($receipt.title) (already recorded: $RequestId)"
    return
}
$receipt=[pscustomobject]@{request_id=$RequestId;content_sha256=$messageHash;title=$Title;source_path=$null;scene_path=$null;state='accepted'}

if ($Mode -eq 'source') {
    $sourceRoot = Join-Path $root '08_Источники'
    $sourceFileName = "$today`_$(Convert-VoidlightFileName -Lowercase -Value $Title).md"
    $sourcePath = Join-Path $sourceRoot $sourceFileName
    if((Test-Path -LiteralPath $sourcePath) -and (Get-VoidlightMeta (Read-VoidlightText $sourcePath) 'request_id') -ne $RequestId){
        $sourceFileName="$today`_$(Convert-VoidlightFileName -Lowercase -Value $Title)_$RequestId.md"
        $sourcePath=Join-Path $sourceRoot $sourceFileName
    }

    $reuseSource=$false
    if (Test-Path -LiteralPath $sourcePath) {
        $existingSource=Read-VoidlightText $sourcePath
        if((Get-VoidlightMeta $existingSource 'request_id') -ne $RequestId -or (Get-VoidlightMeta $existingSource 'content_sha256') -cne $messageHash){throw "Source file already exists for another message: $(Get-VoidlightRelativePath -Root $root $sourcePath)"}
        if(-not (Test-VoidlightSourcePayload $existingSource $messageHash)){throw 'Existing source payload differs from the original message.'}
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

    if(-not $reuseSource){Write-VoidlightText $sourcePath $sourceContent}
    $sourceReference = "``$(Get-VoidlightRelativePath -Root $root $sourcePath)``"
    $receipt.source_path=Get-VoidlightRelativePath -Root $root $sourcePath
}

$inboxPath = Join-Path $root '07_Черновики_и_идеи\Входящие_сообщения.md'
$inbox = Read-VoidlightText $inboxPath
if(@(Get-VoidlightInboxEntries $inbox|Where-Object {(Get-VoidlightEntryField $_.Value 'Request-ID') -ceq $RequestId}).Count -gt 0){
    Save-VoidlightReceipt $root $receipt
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

$newSection=@(Get-VoidlightMarkdownSections $inbox 2|Where-Object heading -eq 'Новые сообщения')|Select-Object -First 1
if(-not $newSection){throw 'Inbox new-message section is missing.'}
$existing=$newSection.text.Trim()
if($existing -eq 'Пока нет новых необработанных сообщений.'){$existing=''}
$replacement="`n$entry`n$existing`n`n"
$inbox=$inbox.Substring(0,$newSection.body_start)+$replacement+$inbox.Substring($newSection.end)

Write-VoidlightText $inboxPath $inbox
Save-VoidlightReceipt $root $receipt

Complete-VoidlightMutation -Root $root -Documents @($receipt.source_path | Where-Object { $_ }) -SkipCheck:$SkipCheck

"Accepted message into inbox: $Title"
}
