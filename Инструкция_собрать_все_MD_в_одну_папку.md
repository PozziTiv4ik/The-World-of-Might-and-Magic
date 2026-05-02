# Инструкция: собрать все Markdown-файлы в одну папку

Сделай в текущем проекте то же самое:

1. Найди все существующие файлы с расширением `.md` во всех папках проекта.
2. Создай в корне проекта папку `Все_MD_файлы`.
3. Скопируй туда все найденные `.md` файлы в один плоский список, без вложенных папок.
4. Не перемещай и не удаляй исходные файлы, только копируй.
5. Не включай в поиск файлы, которые уже лежат внутри папки `Все_MD_файлы`, чтобы не копировать копии повторно.
6. Если у разных файлов одинаковые имена, не затирай их. Для таких копий используй имя на основе исходного пути, заменяя символы папок на `__`.
7. После копирования проверь, что в папке `Все_MD_файлы` нет вложенных папок и что количество скопированных `.md` файлов совпадает с количеством найденных исходных `.md` файлов.

Готовая команда PowerShell для Windows:

```powershell
$ErrorActionPreference = 'Stop'

$root = (Get-Location).Path.TrimEnd('\')
$dest = Join-Path $root 'Все_MD_файлы'

New-Item -ItemType Directory -Force -Path $dest | Out-Null

$destResolved = (Resolve-Path -LiteralPath $dest).Path.TrimEnd('\')

$files = Get-ChildItem -LiteralPath $root -Recurse -Force -File -Filter '*.md' | Where-Object {
    $full = $_.FullName
    -not (
        $full.StartsWith($destResolved + '\', [System.StringComparison]::OrdinalIgnoreCase) -or
        $full.Equals($destResolved, [System.StringComparison]::OrdinalIgnoreCase)
    )
} | Sort-Object FullName

$used = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)
$copied = 0
$renamed = 0

foreach ($file in $files) {
    $name = $file.Name

    if (-not $used.Add($name)) {
        $relative = $file.FullName

        if ($relative.StartsWith($root + '\', [System.StringComparison]::OrdinalIgnoreCase)) {
            $relative = $relative.Substring($root.Length + 1)
        }

        $name = ($relative -replace '[\\/:*?"<>|]', '__')

        if (-not $name.EndsWith('.md', [System.StringComparison]::OrdinalIgnoreCase)) {
            $name = [System.IO.Path]::GetFileNameWithoutExtension($name) + '.md'
        }

        $renamed++
        $candidate = $name
        $i = 2

        while (-not $used.Add($candidate)) {
            $stem = [System.IO.Path]::GetFileNameWithoutExtension($name)
            $candidate = "$stem`_$i.md"
            $i++
        }

        $name = $candidate
    }

    Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $dest $name) -Force
    $copied++
}

$mdCount = (Get-ChildItem -LiteralPath $dest -File -Filter '*.md' | Measure-Object).Count
$dirCount = (Get-ChildItem -LiteralPath $dest -Directory | Measure-Object).Count

[pscustomobject]@{
    Destination = $dest
    MarkdownFilesFound = $files.Count
    MarkdownFilesCopied = $copied
    MarkdownFilesInDestination = $mdCount
    SubfoldersInDestination = $dirCount
    RenamedForFilenameCollisions = $renamed
} | Format-List
```

Ожидаемый результат: в корне проекта появляется папка `Все_MD_файлы`, внутри нее лежат копии всех Markdown-файлов проекта одним списком.
