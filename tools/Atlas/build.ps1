param([switch]$Test,[string]$TestMap,[string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$atlasRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$atlasTools = Join-Path $atlasRoot '.wmma/toolchains'
$atlasCompilerRoot = Join-Path $atlasTools 'llvm-mingw-20260908-ucrt-x86_64'
$atlasCompiler = Join-Path $atlasCompilerRoot 'bin/clang++.exe'
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
[IO.File]::WriteAllText((Join-Path $atlasBuild 'symbols_data.hpp'),$atlasSymbolsTemplate.Replace('@ATLAS_SYMBOLS_JSON@',$atlasSymbols),[Text.UTF8Encoding]::new($false))
$atlasObjects = @()
$atlasFlags = @('-std=c++20','-O2','-Wall','-Wextra','-Wno-unused-parameter','-Wno-missing-field-initializers','-DUNICODE','-D_UNICODE','-I',$atlasBuild)
foreach ($atlasSource in Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'src') -File -Filter '*.cpp' | Sort-Object Name) {
    $atlasObject = Join-Path $atlasBuild ($atlasSource.BaseName + '.o')
    Write-Output ('Compiling ' + $atlasSource.Name)
    & $atlasCompiler @atlasFlags '-c' $atlasSource.FullName '-o' $atlasObject
    if ($LASTEXITCODE -ne 0) { throw ('Compilation failed: ' + $atlasSource.Name) }
    if ($atlasSource.BaseName -notin @('main','cli_main')) {
        $atlasObjects += $atlasObject
    }
}
$atlasLibraries = @('-ld2d1','-ldwrite','-lwindowscodecs','-lole32','-luuid','-lbcrypt','-ldwmapi','-lcomdlg32','-lshell32','-luser32','-lgdi32')
& $atlasCompiler '-static' '-municode' '-mwindows' (Join-Path $atlasBuild 'main.o') @atlasObjects @atlasLibraries '-o' (Join-Path $atlasBin 'Atlas.exe')
if ($LASTEXITCODE -ne 0) { throw 'GUI link failed' }
& $atlasCompiler '-static' '-municode' (Join-Path $atlasBuild 'cli_main.o') @atlasObjects @atlasLibraries '-o' (Join-Path $atlasBin 'Atlas.Cli.exe')
if ($LASTEXITCODE -ne 0) { throw 'CLI link failed' }
Write-Output ('Built: ' + $atlasBin)
if ($Test) {
    $atlasTestArgs = @('self-test','--out',(Join-Path $atlasRoot '.wmma/atlas-tests'),'--pdn',(Join-Path $atlasRoot '10_Обслуживание/Концепты/Редактор_карт_2026-09-13/Референсы/map.pdn'))
    if(-not $TestMap) { $TestMap=Join-Path $atlasRoot '12_Карты/Карта_мира_Объекты' }
    if(Test-Path -LiteralPath (Join-Path $TestMap 'map.json')) { $atlasTestArgs += @('--map',$TestMap) }
    & (Join-Path $atlasBin 'Atlas.Cli.exe') @atlasTestArgs
    if ($LASTEXITCODE -ne 0) { throw 'Atlas verification failed' }
}
