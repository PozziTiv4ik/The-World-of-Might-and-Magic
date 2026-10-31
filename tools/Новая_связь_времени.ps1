param(
    # -From A -To B -Relation after: A happened after B; learned_after: A was learned after B;
    # overlaps: the events overlap and do not order each other.
    [Parameter(Mandatory = $true)]
    [string]$From,

    [Parameter(Mandatory = $true)]
    [string]$To,

    [Parameter(Mandatory = $true)]
    [ValidateSet('after', 'overlaps', 'learned_after')]
    [string]$Relation,

    [Parameter(Mandatory = $true)]
    [string[]]$EvidenceIds,

    [ValidateSet('relative', 'exact')]
    [string]$Precision = 'relative',

    [switch]$SkipCheck
)

$ErrorActionPreference = 'Stop'

[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
$OutputEncoding = [System.Text.UTF8Encoding]::new()

. (Join-Path $PSScriptRoot '_lib.ps1')
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path

# Adds a relative-time relation to 09_Реестры/Хронология_связей.json. A repeated relation
# only gains new evidence; a relation that would order events in a cycle is refused.
Invoke-VoidlightToolMain -Root $root -Name $MyInvocation.MyCommand.Name -ScriptBlock {
    if ($From -ceq $To) { throw 'An event cannot be ordered against itself.' }
    $graph = Read-VoidlightRegistry $root 'Сущности'
    Assert-VoidlightEntityIds $graph @($From, $To) -Type @('scene', 'source', 'source_note', 'source_compilation') -Label 'event'
    $evidence = @($EvidenceIds | Where-Object { $_ } | Select-Object -Unique)
    if (-not $evidence.Count) { throw 'A temporal relation needs at least one evidence ID.' }
    Assert-VoidlightEntityIds $graph $evidence -Label 'evidence'

    $registry = Read-VoidlightRegistry $root 'Хронология_связей'
    $relations = [System.Collections.Generic.List[object]]::new()
    foreach ($item in @($registry.relations | Where-Object { $_ })) { $relations.Add($item) }
    $existing = @($relations | Where-Object { $_.from -ceq $From -and $_.to -ceq $To -and $_.relation -ceq $Relation }) | Select-Object -First 1
    if ($existing) {
        $merged = @(@($existing.evidence_ids) + $evidence | Select-Object -Unique)
        if ($merged.Count -eq @($existing.evidence_ids).Count) { "Temporal relation already recorded: $From $Relation $To"; return }
        $existing.evidence_ids = $merged
    } else {
        $relations.Add([pscustomobject][ordered]@{ from = $From; to = $To; relation = $Relation; evidence_ids = $evidence; precision = $Precision })
    }
    $cycles = @(Get-VoidlightOrderCycles @($relations))
    if ($cycles.Count) { throw "The relation would order events in a cycle: $($cycles[0])" }

    $registry.relations = @($relations)
    Write-VoidlightRegistry $root 'Хронология_связей' $registry
    Complete-VoidlightMutation -Root $root -SkipCheck:$SkipCheck
    "Recorded temporal relation: $From $Relation $To"
}
