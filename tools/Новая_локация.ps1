param(
    [Parameter(Mandatory = $true)]
    [string]$Name,

    [string]$Summary = 'Уточнить.',

    [string]$FrontId = '-',

    [switch]$Force,

    [switch]$SkipCheck
)

$ErrorActionPreference = 'Stop'

[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
$OutputEncoding = [System.Text.UTF8Encoding]::new()

. (Join-Path $PSScriptRoot '_lib.ps1')

$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
Invoke-WmmaToolMain -Root $root -Name $MyInvocation.MyCommand.Name -ScriptBlock {
$fileName = Convert-WmmaFileName -Value $Name
$relativePath = "04_Локации/$fileName.md"
$targetPath = Join-Path $root ($relativePath -replace '/', '\')

if ((Test-Path -LiteralPath $targetPath) -and -not $Force) {
    throw "Location file already exists: $relativePath"
}

$content = @"
# $Name

---
type: location
status: active
canon_level: active
front_id: $FrontId
---

## Кратко

$Summary

## География

Уточнить.

## Значение

Уточнить.

## Правитель / силы

Уточнить.

## Ресурсы

- Уточнить.

## История

- Уточнить.

## Текущие события

- Уточнить.

## Связанные персонажи

- Уточнить.
"@

# Bytes as written by Set-Content: the text and one platform line break.
Write-WmmaText $targetPath ($content + [Environment]::NewLine)

$mapPath = Join-Path $root '04_Локации\00_Карта_и_регионы.md'
$mapText = Read-WmmaText $mapPath
$row = "- ``$relativePath`` - $Summary"

if ($mapText -notmatch [regex]::Escape($relativePath)) {
    if ($mapText -notmatch '## Локации, добавленные инструментом') {
        $mapText = $mapText.TrimEnd() + @"

## Локации, добавленные инструментом

$row
"@
    } else {
        $mapText = $mapText.TrimEnd() + "`r`n$row"
    }

    Write-WmmaText $mapPath ($mapText + [Environment]::NewLine)
}

Complete-WmmaMutation -Root $root -Rebuild 'Собрать_индекс_локаций' -SkipCheck:$SkipCheck

"Created location: $relativePath"
}
