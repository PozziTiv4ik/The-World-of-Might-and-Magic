function Get-WmmaFileInventory {
    param([string]$Root)
    $result=@{}
    foreach($file in Get-ChildItem -LiteralPath $Root -Recurse -File -Force){
        $relative=Get-WmmaRelativePath $Root $file.FullName
        if($relative -match '^(\.git|\.wmma)(/|$)' -or $relative -match '(^|/)desktop\.ini$|\.(tmp|bak|orig)$'){continue}
        $result[$relative]=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    return $result
}

function Get-WmmaTurnPath {
    param([string]$Root,[string]$Id)
    if($Id -notmatch '^TURN-[a-zA-Z0-9-]+$'){throw 'Invalid turn ID.'}
    return Resolve-WmmaPath $Root ('.wmma/turns/'+$Id)
}

function Start-WmmaTurn {
    param([string]$Root,[string]$Id,[string]$Title)
    $turn=Get-WmmaTurnPath $Root $Id
    $manifestPath=Join-Path $turn 'transaction.json'
    if(Test-Path -LiteralPath $manifestPath){return Read-WmmaJson $manifestPath}
    $stage=Join-Path $turn 'stage'
    [IO.Directory]::CreateDirectory($stage)|Out-Null
    $inventory=Get-WmmaFileInventory $Root
    foreach($path in $inventory.Keys){
        $target=Resolve-WmmaPath $stage $path
        [IO.Directory]::CreateDirectory((Split-Path -Parent $target))|Out-Null
        [IO.File]::Copy((Resolve-WmmaPath $Root $path),$target)
    }
    $manifest=[ordered]@{schema_version=1;id=$Id;title=$Title;status='editing';base=$inventory;changes=@();applied=@();prepared_digest=$null}
    Write-WmmaJson $manifestPath $manifest
    return $manifest
}

function Invoke-WmmaStageValidation {
    param([string]$Stage,[switch]$Build)
    $program=if($Build){'Завершить_ход.ps1'}else{'Проверить_проект.ps1'}
    $shell=(Get-Process -Id $PID).Path
    & $shell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $Stage ('tools/'+$program))
    if($LASTEXITCODE -ne 0){throw "Stage validation failed: $program"}
}

function Prepare-WmmaTurn {
    param([string]$Root,[string]$Id)
    $turn=Get-WmmaTurnPath $Root $Id
    $path=Join-Path $turn 'transaction.json';$m=Read-WmmaJson $path
    if($m.status -in @('applying','committed')){throw "Cannot prepare a $($m.status) turn."}
    $stage=Join-Path $turn 'stage'
    Invoke-WmmaStageValidation $stage -Build
    $after=Get-WmmaFileInventory $stage
    $base=@{};foreach($p in $m.base.PSObject.Properties){$base[$p.Name]=$p.Value}
    $changes=@()
    foreach($p in @(@($base.Keys)+@($after.Keys)|Sort-Object -Unique)){
        if($base[$p] -ceq $after[$p]){continue}
        if($p -match '^(\.git|\.wmma)(/|$)'){throw 'Local runtime paths cannot be in a turn.'}
        $changes += [pscustomobject]@{path=$p;before=$base[$p];after=$after[$p]}
    }
    $m.changes=$changes;$m.applied=@();$m.status='prepared'
    $m.prepared_digest=Get-WmmaHash (ConvertTo-Json -InputObject @($changes) -Depth 10 -Compress)
    Write-WmmaJson $path $m
    return $m
}

function Apply-WmmaTurn {
    param([string]$Root,[string]$Id,[int]$FailAfter=0)
    $turn=Get-WmmaTurnPath $Root $Id
    $manifestPath=Join-Path $turn 'transaction.json';$m=Read-WmmaJson $manifestPath
    if($m.status -eq 'committed'){return $m}
    if($m.status -notin @('prepared','applying')){throw "Turn is not prepared: $($m.status)"}
    if($m.prepared_digest -cne (Get-WmmaHash (ConvertTo-Json -InputObject @($m.changes) -Depth 10 -Compress))){throw 'Prepared plan was modified.'}
    $stage=Join-Path $turn 'stage'
    $current=Get-WmmaFileInventory $Root
    $staged=Get-WmmaFileInventory $stage
    # Verify every operation before the first write. On recovery each target may be
    # at its before or after hash; unrelated concurrent edits are never overwritten.
    foreach($change in $m.changes){
        [void](Resolve-WmmaPath $Root $change.path)
        if($change.path -match '^(\.git|\.wmma)(/|$)'){throw 'Protected runtime path.'}
        if($staged[$change.path] -cne $change.after){throw "Stage changed after preparation: $($change.path)"}
        if($current[$change.path] -cne $change.before -and $current[$change.path] -cne $change.after){throw "Concurrent edit: $($change.path)"}
    }
    # Dependencies not changed by the plan must also match the base. Otherwise
    # staged validation would have validated a different campaign state.
    $changed=@($m.changes|ForEach-Object {$_.path})
    foreach($p in $m.base.PSObject.Properties){
        if($changed -notcontains $p.Name -and $current[$p.Name] -cne $p.Value){throw "Changed dependency: $($p.Name)"}
        if($changed -notcontains $p.Name -and $staged[$p.Name] -cne $p.Value){throw "Stage dependency changed after preparation: $($p.Name)"}
    }
    foreach($p in $current.Keys){if(-not $m.base.PSObject.Properties[$p] -and $changed -notcontains $p){throw "New concurrent file: $p"}}
    foreach($p in $staged.Keys){if(-not $m.base.PSObject.Properties[$p] -and $changed -notcontains $p){throw "New stage file after preparation: $p"}}
    Invoke-WmmaStageValidation $stage
    $m.status='applying';Write-WmmaJson $manifestPath $m
    $count=0
    foreach($change in $m.changes){
        if($current[$change.path] -ceq $change.after){continue}
        $target=Resolve-WmmaPath $Root $change.path
        $targetHash=if(Test-Path -LiteralPath $target){(Get-FileHash -LiteralPath $target).Hash.ToLowerInvariant()}else{$null}
        if($targetHash -cne $change.before -and $targetHash -cne $change.after){throw "Concurrent edit during application: $($change.path)"}
        $backup=Resolve-WmmaPath (Join-Path $turn 'backup') $change.path
        if($change.before -and -not (Test-Path -LiteralPath $backup)){
            [IO.Directory]::CreateDirectory((Split-Path -Parent $backup))|Out-Null
            [IO.File]::Copy($target,$backup)
        }
        if($change.after){
            [IO.Directory]::CreateDirectory((Split-Path -Parent $target))|Out-Null
            $temp=$target+'.'+[guid]::NewGuid().ToString('N')+'.tmp'
            [IO.File]::Copy((Resolve-WmmaPath $stage $change.path),$temp)
            if(Test-Path -LiteralPath $target){[IO.File]::Replace($temp,$target,[NullString]::Value)}else{[IO.File]::Move($temp,$target)}
        }else{[IO.File]::Delete($target)}
        $m.applied=@($m.applied)+@($change.path);Write-WmmaJson $manifestPath $m
        $count++
        if($FailAfter -gt 0 -and $count -ge $FailAfter){throw 'Simulated interruption; journal retained for recovery.'}
    }
    $m.status='committed';Write-WmmaJson $manifestPath $m
    return $m
}

function Undo-WmmaTurn {
    param([string]$Root,[string]$Id)
    $turn=Get-WmmaTurnPath $Root $Id;$mp=Join-Path $turn 'transaction.json';$m=Read-WmmaJson $mp
    if($m.status -notin @('applying','committed')){throw 'No applied turn to restore.'}
    $current=Get-WmmaFileInventory $Root
    foreach($change in $m.changes){
        if($current[$change.path] -cne $change.before -and $current[$change.path] -cne $change.after){throw "Concurrent edit prevents restoration: $($change.path)"}
        if($change.before -and $current[$change.path] -cne $change.before){
            $backup=Resolve-WmmaPath (Join-Path $turn 'backup') $change.path
            if(-not (Test-Path -LiteralPath $backup) -or (Get-FileHash -LiteralPath $backup).Hash.ToLowerInvariant() -cne $change.before){throw "Missing or invalid backup: $($change.path)"}
        }
    }
    foreach($change in $m.changes){
        if($current[$change.path] -ceq $change.before){continue}
        $target=Resolve-WmmaPath $Root $change.path
        if($change.before){
            $backup=Resolve-WmmaPath (Join-Path $turn 'backup') $change.path
            [IO.Directory]::CreateDirectory((Split-Path -Parent $target))|Out-Null
            $temp=$target+'.'+[guid]::NewGuid().ToString('N')+'.tmp';[IO.File]::Copy($backup,$temp)
            if(Test-Path -LiteralPath $target){[IO.File]::Replace($temp,$target,[NullString]::Value)}else{[IO.File]::Move($temp,$target)}
        }else{[IO.File]::Delete($target)}
    }
    $m.status='restored';Write-WmmaJson $mp $m;return $m
}
