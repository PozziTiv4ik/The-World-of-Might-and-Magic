param(
    [switch]$Build,
    [switch]$LongPaths,
    [string]$CliPath,
    [string]$MapDirectory,
    [string]$OutputDirectory,
    [string[]]$Scenario,
    [int]$TimeoutSeconds = 300
)
$ErrorActionPreference = 'Stop'
$atlasTestProject = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
if ($Build) {
    if($CliPath){& (Join-Path $PSScriptRoot 'build.ps1') -OutputDirectory ([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($CliPath)))}else{& (Join-Path $PSScriptRoot 'build.ps1')}
    if($LASTEXITCODE -ne 0){throw 'Atlas build failed'}
}
if (-not $CliPath) { $CliPath = Join-Path $PSScriptRoot 'bin/Atlas.Cli.exe' }
$CliPath = [IO.Path]::GetFullPath($CliPath)
if (-not (Test-Path -LiteralPath $CliPath)) { throw 'Build Atlas before running editor scenarios' }
$atlasProvenanceFile=Join-Path ([IO.Path]::GetDirectoryName($CliPath)) 'build-info.json'
if(-not(Test-Path -LiteralPath $atlasProvenanceFile)){throw 'Missing build-info.json; build Atlas with build.ps1 before testing'}
$atlasProvenance=Get-Content -LiteralPath $atlasProvenanceFile -Raw | ConvertFrom-Json
foreach($atlasSource in $atlasProvenance.source_sha256.PSObject.Properties) {
    $atlasSourceFile=Join-Path $PSScriptRoot $atlasSource.Name
    if(-not(Test-Path -LiteralPath $atlasSourceFile) -or (Get-FileHash -LiteralPath $atlasSourceFile).Hash.ToLowerInvariant() -ne $atlasSource.Value){throw ('Source changed after build: '+$atlasSource.Name+'. Run test-editor.ps1 -Build')}
}
foreach($atlasSourceFile in Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'src') -File) {
    $atlasRelative='src\'+$atlasSourceFile.Name
    if(-not($atlasProvenance.source_sha256.PSObject.Properties.Name -contains $atlasRelative) -and -not($atlasProvenance.source_sha256.PSObject.Properties.Name -contains $atlasRelative.Replace('\','/'))){throw ('Unbuilt source: '+$atlasSourceFile.Name)}
}
if((Get-FileHash -LiteralPath $CliPath).Hash.ToLowerInvariant() -ne $atlasProvenance.binary_sha256.'Atlas.Cli.exe'){throw 'CLI checksum does not match its build provenance'}
if (-not $MapDirectory) { $MapDirectory=Join-Path $atlasTestProject '12_Карты/Карта_мира_Объекты' }
if (-not $OutputDirectory) { $OutputDirectory=Join-Path $atlasTestProject '.wmma/atlas-editor-tests' }
if($LongPaths) {
    $OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
    while($OutputDirectory.Length -lt 180){$OutputDirectory=Join-Path $OutputDirectory 'nested-directory-for-windows-long-path-regression'}
}
$atlasSuitePath=Join-Path ([IO.Path]::GetFullPath($OutputDirectory)) ('suite-'+[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')+'-'+[guid]::NewGuid().ToString('N').Substring(0,6))
New-Item -ItemType Directory -Path $atlasSuitePath -Force | Out-Null
$atlasScenarioFiles=if($Scenario){@($Scenario | ForEach-Object { Get-Item -LiteralPath $_ })}else{@(Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'tests/scenarios') -Filter '*.json' -File | Sort-Object Name)}
$atlasResults=[Collections.Generic.List[object]]::new()
foreach($atlasScenarioFile in $atlasScenarioFiles) {
    $atlasSpec=Get-Content -LiteralPath $atlasScenarioFile.FullName -Raw | ConvertFrom-Json
    if($atlasSpec.requires_map -and -not(Test-Path -LiteralPath (Join-Path $MapDirectory 'map.json'))) {
        $atlasResults.Add([ordered]@{name=$atlasSpec.name;status='skipped';error='Source map is unavailable'})
        continue
    }
    $atlasStart=[Diagnostics.ProcessStartInfo]::new()
    $atlasStart.FileName=$CliPath
    $atlasStart.UseShellExecute=$false
    $atlasStart.CreateNoWindow=$true
    $atlasStart.RedirectStandardOutput=$true
    $atlasStart.RedirectStandardError=$true
    $atlasStart.StandardOutputEncoding=[Text.UTF8Encoding]::new($false)
    $atlasStart.StandardErrorEncoding=[Text.UTF8Encoding]::new($false)
    foreach($atlasArgument in @('editor-test','--scenario',$atlasScenarioFile.FullName,'--out',$atlasSuitePath,'--project',$atlasTestProject)){$atlasStart.ArgumentList.Add($atlasArgument)}
    if($atlasSpec.requires_map){$atlasStart.ArgumentList.Add('--map');$atlasStart.ArgumentList.Add([IO.Path]::GetFullPath($MapDirectory))}
    $atlasProcess=[Diagnostics.Process]::Start($atlasStart)
    $atlasStdout=$atlasProcess.StandardOutput.ReadToEndAsync()
    $atlasStderr=$atlasProcess.StandardError.ReadToEndAsync()
    $atlasTimedOut=-not $atlasProcess.WaitForExit([Math]::Clamp($TimeoutSeconds,5,1800)*1000)
    if($atlasTimedOut){$atlasProcess.Kill($true);$atlasProcess.WaitForExit()}
    $atlasText=$atlasStdout.GetAwaiter().GetResult()
    $atlasError=$atlasStderr.GetAwaiter().GetResult()
    [IO.File]::WriteAllText((Join-Path $atlasSuitePath ($atlasScenarioFile.BaseName+'.log')),$atlasText+"`n"+$atlasError,[Text.UTF8Encoding]::new($false))
    try{
        $atlasResult=$atlasText | ConvertFrom-Json -ErrorAction Stop
        if($null -eq $atlasResult -or $atlasResult.status -notin @('passed','failed')){throw 'CLI did not return a scenario result'}
    }catch{
        $atlasFailure=if($atlasError.Trim()){$atlasError.Trim()}else{'CLI did not return a valid scenario result (exit '+$atlasProcess.ExitCode+')'}
        $atlasResult=[pscustomobject]@{name=$atlasSpec.name;status='failed';error=$atlasFailure;exit_code=$atlasProcess.ExitCode}
    }
    if($atlasTimedOut){$atlasResult=[pscustomobject]@{name=$atlasSpec.name;status='failed';error='Scenario timed out';exit_code=$atlasProcess.ExitCode}}
    if($atlasProcess.ExitCode -ne 0 -and $atlasResult.status -eq 'passed'){$atlasResult.status='failed'}
    $atlasResults.Add($atlasResult)
    Write-Output ($atlasResult.status.ToUpperInvariant()+' '+$atlasSpec.name+$(if($atlasResult.error){' — '+$atlasResult.error}else{''}))
    $atlasProcess.Dispose()
}
$atlasSummary=[ordered]@{
    passed=@($atlasResults | Where-Object status -eq 'passed').Count
    failed=@($atlasResults | Where-Object status -eq 'failed').Count
    skipped=@($atlasResults | Where-Object status -eq 'skipped').Count
    computer_use=$false; native_windows=$false
    output=$atlasSuitePath; scenarios=$atlasResults
}
$atlasSummary | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $atlasSuitePath 'summary.json') -Encoding utf8
$atlasHtml=[Text.StringBuilder]::new()
[void]$atlasHtml.Append('<!doctype html><html lang="ru"><meta charset="utf-8"><title>Проверки Atlas</title><style>body{font:16px system-ui;max-width:1000px;margin:40px auto;background:#fafbf8;color:#233432;padding:0 24px}article{padding:18px 0;border-top:1px solid #d8e1dc}a{color:#22685f}.failed{color:#a33429}</style><h1>Проверки Atlas без Computer Use</h1>')
[void]$atlasHtml.Append('<p>Пройдено: '+$atlasSummary.passed+' · Ошибок: '+$atlasSummary.failed+' · Пропущено: '+$atlasSummary.skipped+'</p>')
foreach($atlasResult in $atlasResults){
    $atlasName=[Net.WebUtility]::HtmlEncode($atlasResult.name)
    $atlasMessage=[Net.WebUtility]::HtmlEncode($atlasResult.error)
    $atlasLink=if($atlasResult.output){[IO.Path]::GetRelativePath($atlasSuitePath,$atlasResult.output).Replace('\','/')+'/report.html'}else{''}
    [void]$atlasHtml.Append('<article class="'+$atlasResult.status+'"><b>'+$atlasName+'</b> · '+$atlasResult.status+'<p>'+$atlasMessage+'</p>')
    if($atlasLink){[void]$atlasHtml.Append('<a href="'+[Net.WebUtility]::HtmlEncode($atlasLink)+'">Действия, снимки и состояние</a>')}
    [void]$atlasHtml.Append('</article>')
}
[void]$atlasHtml.Append('</html>')
[IO.File]::WriteAllText((Join-Path $atlasSuitePath 'index.html'),$atlasHtml.ToString(),[Text.UTF8Encoding]::new($false))
Write-Output ('Report: '+(Join-Path $atlasSuitePath 'index.html'))
if($atlasSummary.failed){exit 1}
if($atlasSummary.skipped){Write-Warning 'Some scenarios were not run because their source map was unavailable'}
