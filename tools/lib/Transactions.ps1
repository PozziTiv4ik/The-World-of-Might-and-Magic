function Get-VoidlightFileInventory {
    param([string]$Root)
    $result=@{}
    $folders=[Collections.Generic.Stack[string]]::new();$folders.Push($Root)
    while($folders.Count){foreach($file in Get-ChildItem -LiteralPath $folders.Pop() -Force){
        $relative=Get-VoidlightRelativePath $Root $file.FullName
        if($relative -match '^(\.git|\.voidlight|Все_MD_файлы)(/|$)' -or $relative -match '(^|/)desktop\.ini$|\.(tmp|bak|orig)$'){continue}
        # Локальные сборки вне Git (кэш компиляции Regnum, тестовые программы) и резервные
        # копии мира Regnum (.regnum-backup) в снимки не входят.
        if($relative -match '(^|/)(\.build|\.voidlight|__pycache__|\.regnum-backup)(/|$)|^tools/Regnum/bin/[^/]+/(regnum-test|platform-demo)'){continue}
        if($file.Attributes -band [IO.FileAttributes]::ReparsePoint){throw "Reparse point cannot be copied into a turn: $relative"}
        if($file.PSIsContainer){$folders.Push($file.FullName);continue}
        $result[$relative]=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }}
    return $result
}

function Get-VoidlightTurnPath {
    param([string]$Root,[string]$Id)
    if($Id -notmatch '^TURN-[a-zA-Z0-9-]+$'){throw 'Invalid turn ID.'}
    return Resolve-VoidlightPath $Root ('.voidlight/turns/'+$Id)
}

function Start-VoidlightTurn {
    param([string]$Root,[string]$Id,[string]$Title)
    $turn=Get-VoidlightTurnPath $Root $Id
    $manifestPath=Join-Path $turn 'transaction.json'
    $manifest=if(Test-Path -LiteralPath $manifestPath){Read-VoidlightJson $manifestPath}else{$null}
    if($manifest -and $manifest.title -cne $Title){throw 'Turn ID already belongs to another title.'}
    if($manifest -and $manifest.status -ne 'copying'){return $manifest}
    $stage=Join-Path $turn 'stage'
    [IO.Directory]::CreateDirectory($stage)|Out-Null
    $inventory=Get-VoidlightFileInventory $Root
    if(-not $manifest){
        $manifest=[pscustomobject][ordered]@{schema_version=2;id=$Id;title=$Title;status='copying';base=$inventory;changes=@();applied=@();prepared_digest=$null}
        Write-VoidlightJson $manifestPath $manifest
    }else{
        foreach($p in $manifest.base.PSObject.Properties){if($inventory[$p.Name] -cne $p.Value){throw "Source changed during interrupted snapshot: $($p.Name)"}}
        if($inventory.Count -ne @($manifest.base.PSObject.Properties).Count){throw 'File list changed during interrupted snapshot.'}
    }
    foreach($path in $inventory.Keys){
        $target=Resolve-VoidlightPath $stage $path
        [IO.Directory]::CreateDirectory((Split-Path -Parent $target))|Out-Null
        if(Test-Path -LiteralPath $target){if((Get-FileHash -LiteralPath $target).Hash.ToLowerInvariant() -cne $inventory[$path]){throw "Incomplete snapshot contains a changed file: $path"};continue}
        [IO.File]::Copy((Resolve-VoidlightPath $Root $path),$target)
    }
    $afterCopy=Get-VoidlightFileInventory $Root
    foreach($key in $inventory.Keys){if($afterCopy[$key] -cne $inventory[$key]){throw "Source changed while copying: $key"}}
    if($afterCopy.Count -ne $inventory.Count){throw 'Source file list changed while copying.'}
    $manifest.status='editing'
    Write-VoidlightJson $manifestPath $manifest
    return $manifest
}

function Invoke-VoidlightStageValidation {
    param([string]$Stage,[switch]$Build)
    $program=if($Build){'Завершить_ход.ps1'}else{'Проверить_проект.ps1'}
    $shell=(Get-Process -Id $PID).Path
    $log=Join-Path $Stage '.voidlight/validation.log';[IO.Directory]::CreateDirectory((Split-Path -Parent $log))|Out-Null
    & $shell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $Stage ('tools/'+$program)) *> $log
    if($LASTEXITCODE -ne 0){$details=(Get-Content -LiteralPath $log -Tail 45)-join [Environment]::NewLine;throw "Stage validation failed: $program. Log: $log$([Environment]::NewLine)$details"}
}

function Get-VoidlightPlanDigest {
    param([object]$Manifest)
    if($Manifest.schema_version -lt 2){return Get-VoidlightHash (ConvertTo-Json -InputObject @($Manifest.changes) -Depth 10 -Compress)}
    $base=@($Manifest.base.PSObject.Properties|Sort-Object Name|ForEach-Object {[ordered]@{path=$_.Name;hash=$_.Value}})
    return Get-VoidlightHash (ConvertTo-Json -InputObject ([ordered]@{id=$Manifest.id;base=$base;changes=@($Manifest.changes)}) -Depth 20 -Compress)
}

function Assert-VoidlightTurnPlan {
    param([string]$Root,[object]$Manifest,[string]$Id)
    if($Manifest.id -cne $Id -or $Manifest.schema_version -notin @(1,2)){throw 'Invalid turn identity or schema.'}
    if($Manifest.prepared_digest -cne (Get-VoidlightPlanDigest $Manifest)){throw 'Prepared plan was modified.'}
    $paths=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach($change in $Manifest.changes){
        $full=Resolve-VoidlightPath $Root $change.path
        $relative=Get-VoidlightRelativePath $Root $full
        if($relative -match '^(\.git|\.voidlight)(/|$)' -or $relative -cne $change.path.Replace('\','/')){throw 'Noncanonical or protected turn path.'}
        if(-not $paths.Add($relative)){throw 'Duplicate path in prepared turn.'}
        foreach($hash in @($change.before,$change.after)|Where-Object {$_}){if($hash -notmatch '^[a-f0-9]{64}$'){throw 'Invalid hash in prepared turn.'}}
        if($change.before -ceq $change.after){throw 'Turn contains an unchanged operation.'}
    }
}

function Prepare-VoidlightTurn {
    param([string]$Root,[string]$Id)
    $turn=Get-VoidlightTurnPath $Root $Id
    $path=Join-Path $turn 'transaction.json';$m=Read-VoidlightJson $path
    if($m.status -notin @('editing','prepared','restored')){throw "Cannot prepare a $($m.status) turn."}
    $stage=Join-Path $turn 'stage'
    Invoke-VoidlightStageValidation $stage -Build
    $after=Get-VoidlightFileInventory $stage
    $base=@{};foreach($p in $m.base.PSObject.Properties){$base[$p.Name]=$p.Value}
    $changes=@()
    foreach($p in @(@($base.Keys)+@($after.Keys)|Sort-Object -Unique)){
        if($base[$p] -ceq $after[$p]){continue}
        if($p -match '^(\.git|\.voidlight)(/|$)'){throw 'Local runtime paths cannot be in a turn.'}
        $changes += [pscustomobject]@{path=$p;before=$base[$p];after=$after[$p]}
    }
    $m.changes=$changes;$m.applied=@();$m.status='prepared'
    $m.schema_version=2
    $m.prepared_digest=Get-VoidlightPlanDigest $m
    Write-VoidlightJson $path $m
    return $m
}

function Apply-VoidlightTurn {
    param([string]$Root,[string]$Id,[int]$FailAfter=0)
    $turn=Get-VoidlightTurnPath $Root $Id
    $manifestPath=Join-Path $turn 'transaction.json';$m=Read-VoidlightJson $manifestPath
    if($m.status -eq 'committed'){Assert-VoidlightTurnPlan $Root $m $Id;return $m}
    if($m.status -notin @('prepared','applying')){throw "Turn is not prepared: $($m.status)"}
    Assert-VoidlightTurnPlan $Root $m $Id
    $stage=Join-Path $turn 'stage'
    $current=Get-VoidlightFileInventory $Root
    $staged=Get-VoidlightFileInventory $stage
    # Verify every operation before the first write. On recovery each target may be
    # at its before or after hash; unrelated concurrent edits are never overwritten.
    foreach($change in $m.changes){
        [void](Resolve-VoidlightPath $Root $change.path)
        if($change.path -match '^(\.git|\.voidlight)(/|$)'){throw 'Protected runtime path.'}
        if($staged[$change.path] -cne $change.after){throw "Stage changed after preparation: $($change.path)"}
        if($m.status -eq 'prepared' -and $current[$change.path] -cne $change.before){throw "Concurrent edit before application: $($change.path)"}
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
    Invoke-VoidlightStageValidation $stage
    $m.status='applying';Write-VoidlightJson $manifestPath $m
    $count=0
    foreach($change in $m.changes){
        if($current[$change.path] -ceq $change.after){
            if($change.before){$backup=Resolve-VoidlightPath (Join-Path $turn 'backup') $change.path;if(-not (Test-Path -LiteralPath $backup) -or (Get-FileHash -LiteralPath $backup).Hash.ToLowerInvariant() -cne $change.before){throw 'Recovery backup is missing or invalid.'}}
            continue
        }
        $target=Resolve-VoidlightPath $Root $change.path
        $targetHash=if(Test-Path -LiteralPath $target){(Get-FileHash -LiteralPath $target).Hash.ToLowerInvariant()}else{$null}
        if($targetHash -cne $change.before){throw "Concurrent edit during application: $($change.path)"}
        $backup=Resolve-VoidlightPath (Join-Path $turn 'backup') $change.path
        if($change.before -and -not (Test-Path -LiteralPath $backup)){
            [IO.Directory]::CreateDirectory((Split-Path -Parent $backup))|Out-Null
            [IO.File]::Copy($target,$backup)
        }
        if($change.before -and (Get-FileHash -LiteralPath $backup).Hash.ToLowerInvariant() -cne $change.before){throw "Invalid recovery backup: $($change.path)"}
        if($change.after){
            [IO.Directory]::CreateDirectory((Split-Path -Parent $target))|Out-Null
            $temp=$target+'.'+[guid]::NewGuid().ToString('N')+'.tmp'
            [IO.File]::Copy((Resolve-VoidlightPath $stage $change.path),$temp)
            if((Get-FileHash -LiteralPath $temp).Hash.ToLowerInvariant() -cne $change.after){throw "Staged payload changed during application: $($change.path)"}
            if(Test-Path -LiteralPath $target){[IO.File]::Replace($temp,$target,[NullString]::Value)}else{[IO.File]::Move($temp,$target)}
        }else{[IO.File]::Delete($target)}
        $m.applied=@($m.applied)+@($change.path);Write-VoidlightJson $manifestPath $m
        $count++
        if($FailAfter -gt 0 -and $count -ge $FailAfter){throw 'Simulated interruption; journal retained for recovery.'}
    }
    $final=Get-VoidlightFileInventory $Root
    if($final.Count -ne $staged.Count){throw 'File list changed during application; journal retained.'}
    foreach($path in $staged.Keys){if($final[$path] -cne $staged[$path]){throw "Project changed during application: $path; journal retained."}}
    $m.status='committed';Write-VoidlightJson $manifestPath $m
    return $m
}

function Undo-VoidlightTurn {
    param([string]$Root,[string]$Id)
    $turn=Get-VoidlightTurnPath $Root $Id;$mp=Join-Path $turn 'transaction.json';$m=Read-VoidlightJson $mp
    Assert-VoidlightTurnPlan $Root $m $Id
    if($m.status -eq 'restored'){return $m}
    if($m.status -notin @('applying','committed','restoring')){throw 'No applied turn to restore.'}
    $current=Get-VoidlightFileInventory $Root
    foreach($change in $m.changes){
        if($current[$change.path] -cne $change.before -and $current[$change.path] -cne $change.after){throw "Concurrent edit prevents restoration: $($change.path)"}
        if($change.before -and $current[$change.path] -cne $change.before){
            $backup=Resolve-VoidlightPath (Join-Path $turn 'backup') $change.path
            if(-not (Test-Path -LiteralPath $backup) -or (Get-FileHash -LiteralPath $backup).Hash.ToLowerInvariant() -cne $change.before){throw "Missing or invalid backup: $($change.path)"}
        }
    }
    $m.status='restoring';Write-VoidlightJson $mp $m
    foreach($change in $m.changes){
        if($current[$change.path] -ceq $change.before){continue}
        $target=Resolve-VoidlightPath $Root $change.path
        $actual=if(Test-Path -LiteralPath $target){(Get-FileHash -LiteralPath $target).Hash.ToLowerInvariant()}else{$null}
        if($actual -cne $change.after){throw "Concurrent edit during restoration: $($change.path)"}
        if($change.before){
            $backup=Resolve-VoidlightPath (Join-Path $turn 'backup') $change.path
            [IO.Directory]::CreateDirectory((Split-Path -Parent $target))|Out-Null
            $temp=$target+'.'+[guid]::NewGuid().ToString('N')+'.tmp';[IO.File]::Copy($backup,$temp)
            if((Get-FileHash -LiteralPath $temp).Hash.ToLowerInvariant() -cne $change.before){throw "Backup changed during restoration: $($change.path)"}
            if(Test-Path -LiteralPath $target){[IO.File]::Replace($temp,$target,[NullString]::Value)}else{[IO.File]::Move($temp,$target)}
        }else{[IO.File]::Delete($target)}
    }
    $m.status='restored';Write-VoidlightJson $mp $m;return $m
}

# Moves an unapplied turn onto the current project files. Outside edits (for example a
# Regnum autosave) are copied into the stage when they touch other files than the turn;
# the turn then needs Подготовить_ход again. Overlapping edits are refused with the list.
function Rebase-VoidlightTurn {
    param([string]$Root,[string]$Id)
    $turn=Get-VoidlightTurnPath $Root $Id
    $manifestPath=Join-Path $turn 'transaction.json';$m=Read-VoidlightJson $manifestPath
    if($m.status -notin @('editing','prepared')){throw "Only an unapplied turn (editing or prepared) moves to a new base; turn $Id is $($m.status). Use -Mode resume or -Mode restore."}
    $stage=Join-Path $turn 'stage'
    $current=Get-VoidlightFileInventory $Root
    $staged=Get-VoidlightFileInventory $stage
    $base=@{};foreach($p in $m.base.PSObject.Properties){$base[$p.Name]=$p.Value}
    $paths=@(@($base.Keys)+@($current.Keys)+@($staged.Keys)|Sort-Object -Unique)
    $outside=@($paths|Where-Object {$base[$_] -cne $current[$_]})
    $own=@($paths|Where-Object {$base[$_] -cne $staged[$_]})
    $conflicts=@($outside|Where-Object {$own -contains $_ -and $current[$_] -cne $staged[$_]})
    if($conflicts.Count){
        throw ("The turn and outside edits changed the same files. Keep one version of each (or restore the outside edit), then retry:`n"+(($conflicts|ForEach-Object {"- $_"}) -join "`n"))
    }
    foreach($path in $outside){
        if($own -contains $path){continue}
        $target=Resolve-VoidlightPath $stage $path
        if($null -ne $current[$path]){
            [IO.Directory]::CreateDirectory((Split-Path -Parent $target))|Out-Null
            $temp=$target+'.'+[guid]::NewGuid().ToString('N')+'.tmp'
            [IO.File]::Copy((Resolve-VoidlightPath $Root $path),$temp)
            if((Get-FileHash -LiteralPath $temp).Hash.ToLowerInvariant() -cne $current[$path]){[IO.File]::Delete($temp);throw "Outside file changed during the rebase: $path"}
            if(Test-Path -LiteralPath $target){[IO.File]::Replace($temp,$target,[NullString]::Value)}else{[IO.File]::Move($temp,$target)}
        }elseif(Test-Path -LiteralPath $target){[IO.File]::Delete($target)}
    }
    $after=Get-VoidlightFileInventory $Root
    if($after.Count -ne $current.Count){throw 'Project file list changed during the rebase; retry.'}
    foreach($key in $current.Keys){if($after[$key] -cne $current[$key]){throw "Project changed during the rebase: $key; retry."}}
    $newBase=[ordered]@{};foreach($key in @($after.Keys|Sort-Object)){$newBase[$key]=$after[$key]}
    $m.base=[pscustomobject]$newBase
    $m.changes=@();$m.applied=@();$m.prepared_digest=$null;$m.status='editing'
    $m|Add-Member -NotePropertyName rebased_real_date -NotePropertyValue (Get-Date -Format 'yyyy-MM-dd') -Force
    $m|Add-Member -NotePropertyName rebased_files -NotePropertyValue @($outside|Where-Object {$own -notcontains $_}) -Force
    Write-VoidlightJson $manifestPath $m
    return $m
}

# Journals of turns in .voidlight/turns: status, start and last journal write; -Size adds the
# size of the copy (stage and backups) in megabytes.
function Get-VoidlightTurns {
    param([string]$Root,[switch]$Size)
    $folder=Join-Path $Root '.voidlight/turns'
    if(-not (Test-Path -LiteralPath $folder)){return}
    foreach($directory in @(Get-ChildItem -LiteralPath $folder -Directory|Sort-Object Name)){
        $manifestPath=Join-Path $directory.FullName 'transaction.json'
        $manifest=$null
        if(Test-Path -LiteralPath $manifestPath){try{$manifest=Read-VoidlightJson $manifestPath}catch{$manifest=$null}}
        $bytes=$null
        if($Size){$bytes=[double](Get-ChildItem -LiteralPath $directory.FullName -Recurse -File -Force -ErrorAction SilentlyContinue|Measure-Object -Property Length -Sum).Sum}
        [pscustomobject][ordered]@{
            id=$directory.Name
            title=$(if($manifest){$manifest.title}else{$null})
            status=$(if($manifest -and $manifest.status){[string]$manifest.status}else{'unreadable'})
            started=$directory.CreationTime.ToString('yyyy-MM-dd HH:mm')
            updated=$(if(Test-Path -LiteralPath $manifestPath){(Get-Item -LiteralPath $manifestPath).LastWriteTime.ToString('yyyy-MM-dd HH:mm')}else{$null})
            size_mb=$(if($null -ne $bytes){[Math]::Round($bytes/1MB,1)}else{$null})
            path=(Get-VoidlightRelativePath $Root $directory.FullName)
        }
    }
}

# Unfinished turns: a half-applied or half-restored turn is an error (finish it with
# Восстановить_ход before committing or rebuilding); an unapplied copy is a warning.
function Get-VoidlightTurnProblems {
    param([string]$Root)
    foreach($turn in @(Get-VoidlightTurns -Root $Root)){
        $id=$turn.id
        switch($turn.status){
            'applying' {[pscustomobject]@{kind='Error';message="Turn $id is half applied: run tools/Восстановить_ход.ps1 -Id $id with -Mode resume (finish) or -Mode restore (undo) before committing or rebuilding."}}
            'restoring' {[pscustomobject]@{kind='Error';message="Turn $id is half restored: run tools/Восстановить_ход.ps1 -Id $id -Mode restore before committing or rebuilding."}}
            'unreadable' {[pscustomobject]@{kind='Error';message="Turn journal is missing or unreadable: $($turn.path)/transaction.json."}}
            {$_ -in @('copying','editing','prepared')} {[pscustomobject]@{kind='Warning';message="Unfinished turn $id ($($turn.status)): continue with Подготовить_ход and Применить_ход, move it onto outside edits with Восстановить_ход -Mode rebase, or discard the copy with Список_ходов -Discard -Id $id."}}
        }
    }
}

# Deletes a turn copy: a finished turn (committed or restored) or, with -Unapplied, a copy
# that never touched the project (copying, editing, prepared). Returns the removed status.
function Remove-VoidlightTurn {
    param([string]$Root,[string]$Id,[switch]$Unapplied)
    $turn=Get-VoidlightTurnPath $Root $Id
    if(-not (Test-Path -LiteralPath $turn)){throw "Unknown turn: $Id"}
    $manifestPath=Join-Path $turn 'transaction.json'
    $status=if(Test-Path -LiteralPath $manifestPath){[string](Read-VoidlightJson $manifestPath).status}else{'unreadable'}
    $allowed=if($Unapplied){@('copying','editing','prepared')}else{@('committed','restored')}
    if($allowed -notcontains $status){throw "Turn $Id is $status; this removal accepts only: $($allowed -join ', ')."}
    Remove-Item -LiteralPath $turn -Recurse -Force
    return $status
}

function Remove-VoidlightFinishedTurns {
    param([string]$Root,[string]$Id='')
    foreach($turn in @(Get-VoidlightTurns -Root $Root -Size|Where-Object {(-not $Id -or $_.id -eq $Id) -and $_.status -in @('committed','restored')})){
        [void](Remove-VoidlightTurn -Root $Root -Id $turn.id)
        $turn
    }
}
