$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'lib/Core.ps1')
. (Join-Path $PSScriptRoot 'lib/Graph.ps1')
. (Join-Path $PSScriptRoot 'lib/ReadModel.ps1')
. (Join-Path $PSScriptRoot 'lib/Context.ps1')
. (Join-Path $PSScriptRoot 'lib/Connections.ps1')
. (Join-Path $PSScriptRoot 'lib/Transactions.ps1')
. (Join-Path $PSScriptRoot 'lib/Views.ps1')
. (Join-Path $PSScriptRoot 'lib/Checks.ps1')

function Get-VoidlightToolLockPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Root
    )

    $normalizedRoot = (Resolve-Path -LiteralPath $Root).Path.ToLowerInvariant()
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [System.Text.Encoding]::UTF8.GetBytes($normalizedRoot)
        $hashBytes = $sha.ComputeHash($bytes)
        $hash = -join ($hashBytes | ForEach-Object { $_.ToString('x2') })
    } finally {
        $sha.Dispose()
    }

    return Join-Path ([System.IO.Path]::GetTempPath()) "voidlight_tool_$hash.lock"
}

function Enter-VoidlightToolLock {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Root,

        [string]$Name = 'tool',

        [int]$TimeoutSeconds = 90
    )

    if (-not $global:VoidlightToolLocks) {
        $global:VoidlightToolLocks = @{}
    }

    $resolvedRoot = (Resolve-Path -LiteralPath $Root).Path
    $rootKey = $resolvedRoot.ToLowerInvariant()

    if ($global:VoidlightToolLocks.ContainsKey($rootKey)) {
        $entry = $global:VoidlightToolLocks[$rootKey]
        $entry.Depth++
        return [pscustomobject]@{
            RootKey = $rootKey
            Reentrant = $true
        }
    }

    $lockPath = Get-VoidlightToolLockPath -Root $resolvedRoot
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    $stream = $null

    while (-not $stream) {
        try {
            $stream = [System.IO.File]::Open(
                $lockPath,
                [System.IO.FileMode]::OpenOrCreate,
                [System.IO.FileAccess]::ReadWrite,
                [System.IO.FileShare]::None
            )
        } catch [System.IO.IOException] {
            if ((Get-Date) -ge $deadline) {
                throw "Timed out waiting for tool lock: $lockPath"
            }

            Start-Sleep -Milliseconds 200
        }
    }

    $entry = [pscustomobject]@{
        Stream = $stream
        Depth = 1
        Path = $lockPath
        Root = $resolvedRoot
    }
    $global:VoidlightToolLocks[$rootKey] = $entry

    $lockInfo = @(
        "pid=$PID",
        "tool=$Name",
        "root=$resolvedRoot",
        "started_utc=$([DateTime]::UtcNow.ToString('o'))"
    ) -join "`n"

    $lockBytes = [System.Text.Encoding]::UTF8.GetBytes($lockInfo)
    $stream.SetLength(0)
    $stream.Write($lockBytes, 0, $lockBytes.Length)
    $stream.Flush()

    return [pscustomobject]@{
        RootKey = $rootKey
        Reentrant = $false
    }
}

function Exit-VoidlightToolLock {
    param(
        [object]$Lock
    )

    if (-not $Lock -or -not $global:VoidlightToolLocks) {
        return
    }

    $rootKey = $Lock.RootKey
    if (-not $rootKey -or -not $global:VoidlightToolLocks.ContainsKey($rootKey)) {
        return
    }

    $entry = $global:VoidlightToolLocks[$rootKey]
    $entry.Depth--

    if ($entry.Depth -gt 0) {
        return
    }

    try {
        $entry.Stream.Dispose()
    } finally {
        $global:VoidlightToolLocks.Remove($rootKey)
    }
}

function Invoke-VoidlightToolMain {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Root,

        [Parameter(Mandatory = $true)]
        [scriptblock]$ScriptBlock,

        [Alias('Name')]
        [string]$ToolName = 'tool'
    )

    $lock = Enter-VoidlightToolLock -Root $Root -Name $ToolName
    try {
        & $ScriptBlock
    } finally {
        Exit-VoidlightToolLock -Lock $lock
    }
}

# Runs a sibling command tools/<Name>.ps1 of the same project. A thrown error, a failed
# status or a nonzero exit code of the child becomes an exception naming the step.
function Invoke-VoidlightTool {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Root,

        [Parameter(Mandatory = $true)]
        [string]$Name,

        [hashtable]$Arguments = @{}
    )

    $global:LASTEXITCODE = 0
    & (Join-Path $Root "tools/$Name.ps1") @Arguments
    if (-not $? -or $LASTEXITCODE -ne 0) {
        throw "Tool step failed: $Name"
    }
}

# Common tail of a write command: rebuild the affected views without checks, then run
# the full Завершить_ход unless the caller deferred validation with -SkipCheck. With
# -SkipCheck the documents the command created or changed are registered in Сущности.json
# at once (-FullGraph rebuilds the whole graph instead), so the next command of a batch
# sees their IDs; the full graph is rebuilt by Завершить_ход or Подготовить_ход.
function Complete-VoidlightMutation {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Root,

        [string[]]$Rebuild = @(),

        [string[]]$Documents = @(),

        [switch]$FullGraph,

        [switch]$SkipCheck
    )

    if ($SkipCheck -and $FullGraph) {
        Invoke-VoidlightTool -Root $Root -Name 'Собрать_связи' -Arguments @{ AssignMissingIds = $true; SkipCheck = $true }
    } elseif ($SkipCheck -and @($Documents | Where-Object { $_ }).Count) {
        Register-VoidlightDocuments -Root $Root -Paths @($Documents | Where-Object { $_ })
    }

    foreach ($name in $Rebuild) {
        Invoke-VoidlightTool -Root $Root -Name $name -Arguments @{ SkipCheck = $true }
    }

    if (-not $SkipCheck) {
        Invoke-VoidlightTool -Root $Root -Name 'Завершить_ход'
    }
}
