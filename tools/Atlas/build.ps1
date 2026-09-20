param([switch]$Test,[string]$TestMap,[string]$OutputDirectory,[string]$CompilerPath)
$ErrorActionPreference = 'Stop'
$atlasRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$atlasTools = Join-Path $atlasRoot '.wmma/toolchains'
$atlasCompilerRoot = Join-Path $atlasTools 'llvm-mingw-20260908-ucrt-x86_64'
$atlasCompiler = Join-Path $atlasCompilerRoot 'bin/clang++.exe'
if ($CompilerPath) { $atlasCompiler = [IO.Path]::GetFullPath($CompilerPath) }
if (-not (Test-Path -LiteralPath $atlasCompiler)) {
    New-Item -ItemType Directory -Path $atlasTools -Force | Out-Null
    $atlasArchive = Join-Path $atlasTools 'llvm-mingw-20260908-ucrt-x86_64.zip'
    Invoke-WebRequest -Uri 'https://github.com/mstorsjo/llvm-mingw/releases/download/20260908/llvm-mingw-20260908-ucrt-x86_64.zip' -OutFile $atlasArchive
    $atlasExpected = '1bcf74d06b724aeecaa6412ca85f5b26fb1da770e7cdcefa9263c9c5c3ad34b6'
    if ((Get-FileHash -Algorithm SHA256 -LiteralPath $atlasArchive).Hash.ToLowerInvariant() -ne $atlasExpected) { throw 'Compiler download checksum mismatch' }
    Expand-Archive -LiteralPath $atlasArchive -DestinationPath $atlasTools -Force
}
$atlasBuild = Join-Path $atlasRoot '.wmma/atlas-build'
$atlasBin = Join-Path $PSScriptRoot 'bin'
if($OutputDirectory){$atlasBin=[IO.Path]::GetFullPath($OutputDirectory)}
New-Item -ItemType Directory -Path $atlasBuild,$atlasBin -Force | Out-Null
$atlasSymbols = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'symbols.json'))
$atlasSymbolsTemplate = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'src/symbols_data.hpp.in'))
$atlasGeneratedSymbols = $atlasSymbolsTemplate.Replace('@ATLAS_SYMBOLS_JSON@',$atlasSymbols)
$atlasGeneratedPath = Join-Path $atlasBuild 'symbols_data.hpp'
if (-not (Test-Path -LiteralPath $atlasGeneratedPath) -or [IO.File]::ReadAllText($atlasGeneratedPath) -cne $atlasGeneratedSymbols) {
    [IO.File]::WriteAllText($atlasGeneratedPath,$atlasGeneratedSymbols,[Text.UTF8Encoding]::new($false))
}
$atlasObjects = @()
$atlasFlags = @('-std=c++20','-O2','-Wall','-Wextra','-Wno-unused-parameter','-Wno-missing-field-initializers','-DUNICODE','-D_UNICODE','-I',$atlasBuild)
$atlasHeaderInputs = @(Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'src') -Filter '*.hpp' -File | Sort-Object Name | ForEach-Object { (Get-FileHash -LiteralPath $_.FullName).Hash })
$atlasSignature = ($atlasHeaderInputs + (Get-FileHash -LiteralPath $atlasGeneratedPath).Hash + $atlasCompiler + ($atlasFlags -join ' ')) -join ':'
$atlasSignaturePath = Join-Path $atlasBuild 'inputs.txt'
$atlasRebuildAll = -not (Test-Path -LiteralPath $atlasSignaturePath) -or [IO.File]::ReadAllText($atlasSignaturePath) -cne $atlasSignature
foreach ($atlasSource in Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'src') -File -Filter '*.cpp' | Sort-Object Name) {
    $atlasObject = Join-Path $atlasBuild ($atlasSource.BaseName + '.o')
    $atlasObjectStamp = $atlasObject + '.inputs'
    $atlasObjectSignature = $atlasSignature + ':' + (Get-FileHash -LiteralPath $atlasSource.FullName).Hash
    if (-not (Test-Path -LiteralPath $atlasObject) -or -not (Test-Path -LiteralPath $atlasObjectStamp) -or [IO.File]::ReadAllText($atlasObjectStamp) -cne $atlasObjectSignature) {
        Write-Output ('Compiling ' + $atlasSource.Name)
        & $atlasCompiler @atlasFlags '-c' $atlasSource.FullName '-o' $atlasObject
        if ($LASTEXITCODE -ne 0) { throw ('Compilation failed: ' + $atlasSource.Name) }
        [IO.File]::WriteAllText($atlasObjectStamp,$atlasObjectSignature,[Text.UTF8Encoding]::new($false))
    }
    if ($atlasSource.BaseName -notin @('main','cli_main')) {
        $atlasObjects += $atlasObject
    }
}
$atlasLibraries = @('-ld2d1','-ldwrite','-lwindowscodecs','-lole32','-luuid','-lbcrypt','-ldwmapi','-lcomdlg32','-lshell32','-luser32','-lgdi32')
& $atlasCompiler '-static' '-municode' '-mwindows' (Join-Path $atlasBuild 'main.o') @atlasObjects @atlasLibraries '-o' (Join-Path $atlasBin 'Atlas.exe')
if ($LASTEXITCODE -ne 0) { throw 'GUI link failed' }
& $atlasCompiler '-static' '-municode' (Join-Path $atlasBuild 'cli_main.o') @atlasObjects @atlasLibraries '-o' (Join-Path $atlasBin 'Atlas.Cli.exe')
if ($LASTEXITCODE -ne 0) { throw 'CLI link failed' }
[IO.File]::WriteAllText($atlasSignaturePath,$atlasSignature,[Text.UTF8Encoding]::new($false))
Write-Output ('Built: ' + $atlasBin)
$atlasBuildInfo = [ordered]@{
    application='Atlas'; version='5.2.0'; history_format=2; editor_scenario_format=1
    compiler_sha256=(Get-FileHash -LiteralPath $atlasCompiler).Hash.ToLowerInvariant()
    compiler_version=(& $atlasCompiler '--version' | Select-Object -First 1)
    compile_flags=$atlasFlags; link_libraries=$atlasLibraries
    source_sha256=[ordered]@{}; binary_sha256=[ordered]@{}
}
foreach ($atlasInput in @(Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'src') -File | Sort-Object Name) + @(Get-Item -LiteralPath (Join-Path $PSScriptRoot 'symbols.json'),(Join-Path $PSScriptRoot 'build.ps1'))) {
    $atlasBuildInfo.source_sha256[[IO.Path]::GetRelativePath($PSScriptRoot,$atlasInput.FullName).Replace('\','/')] = (Get-FileHash -LiteralPath $atlasInput.FullName).Hash.ToLowerInvariant()
}
foreach ($atlasBinary in @('Atlas.exe','Atlas.Cli.exe')) { $atlasBuildInfo.binary_sha256[$atlasBinary] = (Get-FileHash -LiteralPath (Join-Path $atlasBin $atlasBinary)).Hash.ToLowerInvariant() }
$atlasBuildInfo | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $atlasBin 'build-info.json') -Encoding utf8
if ($Test) {
    $atlasTestArgs = @('self-test','--out',(Join-Path $atlasRoot '.wmma/atlas-tests'),'--pdn',(Join-Path $atlasRoot '10_Обслуживание/Концепты/Редактор_карт_2026-09-13/Референсы/map.pdn'))
    if(-not $TestMap) { $TestMap=Join-Path $atlasRoot '12_Карты/Карта_мира_Объекты' }
    if(Test-Path -LiteralPath (Join-Path $TestMap 'map.json')) { $atlasTestArgs += @('--map',$TestMap) }
    & (Join-Path $atlasBin 'Atlas.Cli.exe') @atlasTestArgs
    if ($LASTEXITCODE -ne 0) { throw 'Atlas verification failed' }
}
