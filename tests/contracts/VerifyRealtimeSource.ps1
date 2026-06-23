param([Parameter(Mandatory = $true)][string] $SourceRoot)

# Every source that implements an audio-callback render path must stay free of locks, file I/O,
# and heap allocation. Add new realtime renderers here as the engine grows.
$callbackFiles = @(
    "src/audio/runtime/ClickRenderer.cpp",
    "src/audio/runtime/ProjectRenderer.cpp"
)
$forbidden = @(
    @{ Pattern = 'std::mutex|std::recursive_mutex|lock_guard|unique_lock'; Name = 'lock' },
    @{ Pattern = 'fstream|FILE\s*\*|fopen|CreateFile'; Name = 'file I/O' },
    @{ Pattern = '\bnew\b|make_unique|make_shared|std::vector'; Name = 'allocation' }
)

foreach ($relative in $callbackFiles) {
    $content = Get-Content -Raw -LiteralPath (Join-Path $SourceRoot $relative)
    foreach ($rule in $forbidden) {
        if ($content -match $rule.Pattern) {
            Write-Error "Realtime callback source $relative contains forbidden $($rule.Name): $($Matches[0])"
            exit 1
        }
    }
}

Write-Host "Realtime source contract: $($callbackFiles.Count) render paths free of lock, file I/O, and allocation APIs."
