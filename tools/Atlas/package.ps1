$ErrorActionPreference = 'Stop'
$atlasRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$atlasMap = Join-Path $atlasRoot '12_Карты/Карта_мира'
if(Test-Path -LiteralPath (Join-Path $atlasRoot '12_Карты/Карта_мира_Объекты/map.json')) {
    $atlasMap=Join-Path $atlasRoot '12_Карты/Карта_мира_Объекты'
}
$atlasCli = Join-Path $PSScriptRoot 'bin/Atlas.Cli.exe'
& $atlasCli validate $atlasMap --project $atlasRoot
if ($LASTEXITCODE -ne 0) { throw 'Map validation failed' }
$atlasManifestFile = Join-Path $atlasMap 'map.json'
$atlasBeforeHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $atlasManifestFile).Hash
$atlasStage = Join-Path $atlasRoot ('.wmma/atlas-packages/' + [guid]::NewGuid().ToString() + '/Атлас')
$atlasStageMap = Join-Path $atlasStage 'Карта'
New-Item -ItemType Directory -Path $atlasStageMap -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'bin/Atlas.exe'),$atlasCli -Destination $atlasStage
Copy-Item -LiteralPath $atlasManifestFile -Destination (Join-Path $atlasStageMap 'map.json')
$atlasDocuments = @((Get-Content -Raw -Encoding UTF8 -LiteralPath $atlasManifestFile | ConvertFrom-Json))
$atlasVersions = Join-Path $atlasMap 'versions'
if (Test-Path -LiteralPath $atlasVersions) {
    New-Item -ItemType Directory -Path (Join-Path $atlasStageMap 'versions') -Force | Out-Null
    foreach ($atlasVersion in Get-ChildItem -LiteralPath $atlasVersions -File -Filter '*.json') {
        $atlasSnapshot = Get-Content -Raw -Encoding UTF8 -LiteralPath $atlasVersion.FullName | ConvertFrom-Json
        $atlasDocuments += $atlasSnapshot.document
        Copy-Item -LiteralPath $atlasVersion.FullName -Destination (Join-Path $atlasStageMap 'versions')
    }
}
$atlasAssetPaths = @($atlasDocuments | ForEach-Object { $_.layers } | Where-Object image | ForEach-Object image | Sort-Object -Unique)
foreach ($atlasRelative in $atlasAssetPaths) {
    $atlasSource = [IO.Path]::GetFullPath((Join-Path $atlasMap $atlasRelative))
    $atlasDestination = [IO.Path]::GetFullPath((Join-Path $atlasStageMap $atlasRelative))
    if (-not $atlasSource.StartsWith($atlasMap + [IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { throw 'Asset escapes source map directory' }
    if (-not $atlasDestination.StartsWith($atlasStageMap + [IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { throw 'Asset escapes package directory' }
    New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($atlasDestination)) -Force | Out-Null
    Copy-Item -LiteralPath $atlasSource -Destination $atlasDestination
    if ((Get-FileHash -Algorithm SHA256 -LiteralPath $atlasSource).Hash -ne (Get-FileHash -Algorithm SHA256 -LiteralPath $atlasDestination).Hash) { throw 'Asset copy mismatch' }
}
New-Item -ItemType Directory -Path (Join-Path $atlasStage 'licenses') -Force | Out-Null
Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'licenses') -File | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $atlasStage 'licenses') }
$atlasHelp = @'
АТЛАС 4 — редактор карт на C++ для Windows

Распакуй всю папку и запусти Atlas.exe. Установка не требуется.
Папка «Карта» содержит мир с отдельной сушей и государствами, а также сохранённые версии.
В рабочем изображении нет растровых подложек: страны, воды, горы и поселения — геометрические объекты.

Для связи с карточками и сценами выбери:
Файл → Выбрать папку кампании → папка «The World of Might and Magic».

Основные клавиши:
V — выбор; N — узлы; P — территория; R — маршрут; U — река.
S — символ; T — подпись; B — кисть; E — ластик; F — заливка.
W — обводка по цвету; G — прямоугольник; L — лассо.
Колесо — масштаб; пробел / средняя кнопка — перемещение полотна.
Enter — закончить контур; Esc — отменить; Home — весь лист.
Ctrl+S — сохранить; Ctrl+Z/Y — отменить / повторить.

Ctrl+F открывает список стран справа. Клик в списке приближает территорию; K включает контрольные точки границы.
J — суша; P — государство; K — общая граница.
Сверху слева: «Границы», «Суша», «Объекты». В границах выбери линию и перемещай круглые точки.
Двойной клик / + — добавить точку; Delete — удалить; Shift — движение по оси.
Кнопка «Море» включает морскую заливку. Берега остаются на месте.
Клик по названию слоя включает выбор только этого слоя; плашка «Только слой» снимает изоляцию.
После движения камеры детализация уточняется в фоне.
Режим «Объекты» позволяет отдельно выбрать подпись. Ctrl+F открывает поиск, F2 — свойства.
Торговые пути и заметки старого PDN находятся в скрытых слоях архива.
Названия из OCR отмечены как требующие проверки. Изображение не подтверждает литературный канон.
Новые объекты, узлы и связи хранятся в читаемом map.json.
«Сохранить версию» связывает снимок с выбранной сценой; литературный канон не меняется автоматически.

Atlas.Cli.exe help показывает команды для ИИ и автоматизации.

Исходники и документация находятся в папке tools/Atlas основного проекта.
'@
[IO.File]::WriteAllText((Join-Path $atlasStage 'Прочитай.txt'),$atlasHelp,[Text.UTF8Encoding]::new($false))
if ($atlasBeforeHash -ne (Get-FileHash -Algorithm SHA256 -LiteralPath $atlasManifestFile).Hash) { throw 'Map changed during packaging; rerun packaging' }
$atlasArchive = Join-Path $PSScriptRoot 'Atlas-portable.zip'
Compress-Archive -LiteralPath $atlasStage -DestinationPath $atlasArchive -Force
Write-Output ('Portable package: ' + $atlasArchive)
Write-Output ('Package source: ' + $atlasStage)
