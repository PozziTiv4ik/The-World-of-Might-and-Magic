# Scenario suite helpers (tools/Проверить_*.ps1 fixtures): assertions, expected failures,
# named steps and a disposable project copy. Loaded only by the suites, after _lib.ps1.
$script:VoidlightPassedTests=[Collections.Generic.List[string]]::new()

function Assert-True([bool]$Condition,[string]$Message){if(-not $Condition){throw $Message}}

# The action must fail. -PassThru returns the failure message for further assertions.
function Expect-Failure {
    param([scriptblock]$Action,[string]$Message,[switch]$PassThru)
    $failure=$null
    try{& $Action|Out-Null}catch{$failure=$_}
    if(-not $failure){throw $Message}
    $global:LASTEXITCODE=0
    if($PassThru){return $failure.Exception.Message}
}

function Step([string]$Name,[scriptblock]$Action){
    $global:LASTEXITCODE=0
    & $Action
    if($LASTEXITCODE -ne 0){throw "Native/script failure in $Name"}
    $script:VoidlightPassedTests.Add($Name);Write-Host "PASS: $Name"
}

# Full copy of the project files (the snapshot inventory, without .git and .voidlight) under .voidlight/<Folder>/.
function New-VoidlightProjectFixture {
    param([string]$Root,[string]$Folder)
    $fixture=Resolve-VoidlightPath $Root ('.voidlight/'+$Folder+'/'+[guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($fixture)|Out-Null
    foreach($path in (Get-VoidlightFileInventory $Root).Keys){
        $target=Resolve-VoidlightPath $fixture $path
        [IO.Directory]::CreateDirectory((Split-Path -Parent $target))|Out-Null
        [IO.File]::Copy((Resolve-VoidlightPath $Root $path),$target)
    }
    return $fixture
}

# Removes the fixture after a successful run (unless kept) and prints the JSON summary.
function Complete-VoidlightTestRun {
    param([string]$Fixture,[switch]$KeepFixture)
    if(-not $KeepFixture){Remove-Item -LiteralPath $Fixture -Recurse -Force -ErrorAction SilentlyContinue}
    [pscustomobject]@{Passed=$script:VoidlightPassedTests.Count;Tests=@($script:VoidlightPassedTests);Fixture=$(if(Test-Path -LiteralPath $Fixture){$Fixture})}|ConvertTo-Json -Depth 6
}
