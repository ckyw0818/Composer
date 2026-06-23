param([Parameter(Mandatory = $true)][string] $SourceRoot)

$inventory = Get-Content -Raw -LiteralPath (Join-Path $SourceRoot "docs/THIRD_PARTY_LICENSES.md")
$required = @(
    'JUCE 8.0.13',
    '7c9d3783b127263d72bb65fe0a7e2dc8a02a7ac2',
    '97c3c5cf039d8ba45378397c3d6c1033c3fc85102c928054a77e8857031ecae3',
    'EXTERNAL DISTRIBUTION: BLOCKED',
    'No sample assets are bundled in S0'
)
foreach ($entry in $required) {
    if (-not $inventory.Contains($entry)) {
        Write-Error "License inventory is missing required entry: $entry"
        exit 1
    }
}

Write-Host "License inventory contract: pins recorded and external distribution remains blocked."
