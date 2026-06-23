param([Parameter(Mandatory = $true)][string] $SourceRoot)

$requiredFiles = @(
    "src/domain/Project.h",
    "src/commands/ProjectEditor.h",
    "src/recording/AudioFifo.h",
    "src/recording/RecordingSession.h",
    "src/recording/RecoveryJournal.h",
    "src/recording/WavFile.h",
    "src/recording/FakeRecordingDevice.h",
    "src/ui/WaveformEditor.h",
    "tests/integration/RecordingFixtureTests.cpp"
)

foreach ($relative in $requiredFiles) {
    $path = Join-Path $SourceRoot $relative
    if (-not (Test-Path -LiteralPath $path)) {
        Write-Error "Missing S3 recording contract file: $relative"
        exit 1
    }
}

$project = Get-Content -Raw -LiteralPath (Join-Path $SourceRoot "src/domain/Project.h")
$session = Get-Content -Raw -LiteralPath (Join-Path $SourceRoot "src/recording/RecordingSession.h")
$fixtures = Get-Content -Raw -LiteralPath (Join-Path $SourceRoot "tests/integration/RecordingFixtureTests.cpp")

$requiredTokens = @(
    "AudioTrack",
    "AudioClip",
    "MonitoringMode",
    "latencyCompensationSamples",
    "RecoveryJournal",
    "recoverPending",
    "FakeRecordingDevice",
    "FakeInputFault::disconnect",
    "FakeInputFault::sampleRateChange",
    "FakeInputFault::dropout",
    "disk-full"
)

$combined = $project + "`n" + $session + "`n" + $fixtures
$missing = @($requiredTokens | Where-Object { $combined -notmatch [regex]::Escape($_) })
if ($missing.Count -gt 0) {
    Write-Error "S3 recording contract tokens missing: $($missing -join ', ')"
    exit 1
}

Write-Host "S3 recording contract: domain, writer, journal, fake device, and fault tests present."
