param([string]$MapDirectory,[string]$OutputFile)
$ErrorActionPreference = 'Stop'
$atlasRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$atlasMap = if ($MapDirectory) { [IO.Path]::GetFullPath($MapDirectory) } else { Join-Path $atlasRoot '12_Карты/Карта_мира_Объекты' }
$atlasCli = Join-Path $PSScriptRoot 'bin/Atlas.Cli.exe'
& $atlasCli validate $atlasMap --project $atlasRoot --history
if ($LASTEXITCODE -ne 0) { throw 'Map or history validation failed' }
$atlasFilesBefore = @{}
foreach ($atlasFile in Get-ChildItem -LiteralPath $atlasMap -File -Recurse) {
    $atlasRelative = [IO.Path]::GetRelativePath($atlasMap,$atlasFile.FullName)
    if ($atlasRelative.StartsWith('.atlas' + [IO.Path]::DirectorySeparatorChar)) { continue }
    $atlasFilesBefore[$atlasRelative] = (Get-FileHash -Algorithm SHA256 -LiteralPath $atlasFile.FullName).Hash
}
$atlasStage = Join-Path $atlasRoot ('.wmma/pkg-' + [guid]::NewGuid().ToString('N').Substring(0,8) + '/Атлас')
$atlasStageMap = Join-Path $atlasStage 'Карта'
New-Item -ItemType Directory -Path $atlasStageMap -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'bin/Atlas.exe'),$atlasCli -Destination $atlasStage
if (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'bin/build-info.json')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'bin/build-info.json') -Destination $atlasStage
}
# Transfer the whole project, including compact documents and named drafts.
# Cached views and per-session recovery stay with the local editor.
foreach ($atlasRelative in $atlasFilesBefore.Keys) {
    $atlasSource = [IO.Path]::GetFullPath((Join-Path $atlasMap $atlasRelative))
    $atlasDestination = [IO.Path]::GetFullPath((Join-Path $atlasStageMap $atlasRelative))
    if (-not $atlasDestination.StartsWith($atlasStageMap + [IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { throw 'Package path escapes map directory' }
    New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($atlasDestination)) -Force | Out-Null
    Copy-Item -LiteralPath $atlasSource -Destination $atlasDestination
    if ((Get-FileHash -Algorithm SHA256 -LiteralPath $atlasDestination).Hash -ne $atlasFilesBefore[$atlasRelative]) { throw 'Map changed during packaging; run again' }
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'licenses') -Destination $atlasStage -Recurse
$atlasHelp = @'
Открой «Atlas.exe».

Карта занимает всё окно. F11 — полный экран / обычное окно.
Кнопка главы сверху или Ctrl+H — отдельный экран истории.
На линии выбирается событие, внизу — редакция его карты. «Открыть карту» начинает работу.
«К карте» и Esc возвращают прежнюю карту. Колесо листает хронологию, Ctrl+колесо меняет плотность.
Ctrl+F — поиск, «Все главы» — фильтр, настройки — порядок и подборки.

Ctrl+S сохраняет новую редакцию текущего события; Ctrl+Shift+S добавляет комментарий.
«+ Событие» добавляет новый момент, в том числе между прошлыми событиями.
Старые снимки сохраняются. Предыдущая работа доступна в именованных черновиках.
«Сравнить» показывает редакции рядом или наложением.

V — выбор, H/пробел/средняя кнопка — перемещение, колесо — масштаб, Home — вся карта.
J — суша, P — государство, K — точки границы, R — маршрут, S — символ, T — подпись.
Enter — закончить контур, Esc — отменить жест. Ctrl+Z/Y — отмена/повтор до 250 действий.
F2 — свойства, F6 — панель, Tab — переход по кнопкам. Дополнительные инструменты — через «…».

Для исходных сцен: меню → Выбрать папку кампании → The World of Might and Magic.
Вся карта и история находятся в папке «Карта». Переноси её целиком.
Atlas.Cli.exe help показывает команды для работы из кода.
build-info.json связывает сборку с хешами исходников и компилятора.
'@
[IO.File]::WriteAllText((Join-Path $atlasStage 'Прочитай.txt'),$atlasHelp,[Text.UTF8Encoding]::new($false))
& (Join-Path $atlasStage 'Atlas.Cli.exe') validate $atlasStageMap --history
if ($LASTEXITCODE -ne 0) { throw 'Portable map does not validate independently' }
$atlasCache = Join-Path $atlasStageMap '.atlas/versions-cache.json'
if (Test-Path -LiteralPath $atlasCache) { Remove-Item -LiteralPath $atlasCache }
$atlasManifest = [ordered]@{ application='Atlas'; version='5.2.0'; files=[ordered]@{} }
foreach ($atlasFile in Get-ChildItem -LiteralPath $atlasStage -Recurse -File | Sort-Object FullName) {
    $atlasManifest.files[[IO.Path]::GetRelativePath($atlasStage,$atlasFile.FullName).Replace('\','/')] = (Get-FileHash -Algorithm SHA256 -LiteralPath $atlasFile.FullName).Hash.ToLowerInvariant()
}
$atlasManifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $atlasStage 'package-manifest.json') -Encoding utf8
foreach ($atlasRelative in $atlasFilesBefore.Keys) {
    if ((Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $atlasMap $atlasRelative)).Hash -ne $atlasFilesBefore[$atlasRelative]) { throw 'Source map changed during packaging; run again' }
}
$atlasArchive = if ($OutputFile) { [IO.Path]::GetFullPath($OutputFile) } else { Join-Path $PSScriptRoot 'Atlas-portable.zip' }
$atlasTemporaryArchive = $atlasArchive + '.new'
if (Test-Path -LiteralPath $atlasTemporaryArchive) { Remove-Item -LiteralPath $atlasTemporaryArchive }
[IO.Compression.ZipFile]::CreateFromDirectory($atlasStage,$atlasTemporaryArchive,[IO.Compression.CompressionLevel]::Optimal,$true)
Move-Item -LiteralPath $atlasTemporaryArchive -Destination $atlasArchive -Force
Write-Output ('Portable package: ' + $atlasArchive)
Write-Output ('Package SHA256: ' + (Get-FileHash -Algorithm SHA256 -LiteralPath $atlasArchive).Hash)
