param([Parameter(Mandatory = $true)][string] $SourceRoot)

$registry = Get-Content -Raw -LiteralPath (Join-Path $SourceRoot "docs/TROUBLESHOOTING.md")
$files = @(
    (Join-Path $SourceRoot "scripts/dev.ps1"),
    (Join-Path $SourceRoot "cmake/Dependencies.cmake"),
    (Join-Path $SourceRoot "src/app/FixtureMain.cpp")
)
$ids = foreach ($file in $files) {
    [regex]::Matches((Get-Content -Raw -LiteralPath $file), 'DX-[A-Z]+-[0-9]{3}') |
        ForEach-Object { $_.Value }
}

$missing = @($ids | Sort-Object -Unique | Where-Object { $registry -notmatch "(?m)^### $($_)$" })
if ($missing.Count -gt 0) {
    Write-Error "Missing troubleshooting anchors: $($missing -join ', ')"
    exit 1
}

Write-Host "Error registry contract: $(@($ids | Sort-Object -Unique).Count) stable IDs documented."
