param(
    [Parameter(Mandatory = $true)][string] $AppPath
)

# Regression guard for the S1 startup crash (0xC0000005): MainComponent::setSize ran before the
# child components were constructed, so the synchronous resized() callback dereferenced null
# unique_ptrs and the process crashed before the window ever appeared. The app must survive
# startup. Liveness alone is unreliable (the faulting thread can take WER's path while the
# process object lingers for a beat), so we also scan the Application event log for a fresh
# APPCRASH naming this binary.

if (-not (Test-Path -LiteralPath $AppPath)) {
    Write-Error "App binary not found: $AppPath"
    exit 1
}

$exeName = Split-Path -Leaf $AppPath
$launchedAt = Get-Date
$proc = Start-Process -FilePath $AppPath -PassThru -WorkingDirectory (Split-Path -Parent $AppPath)
Start-Sleep -Seconds 4

$exited = $proc.HasExited
$exitCode = if ($exited) { $proc.ExitCode } else { $null }
if (-not $exited) {
    Stop-Process -Id $proc.Id -Force
}

# Ground truth: did Windows Error Reporting log a crash for this exe since launch?
Start-Sleep -Milliseconds 500
$crash = $null
try {
    $crash = Get-WinEvent -FilterHashtable @{
        LogName      = 'Application'
        ProviderName = 'Application Error'
    } -MaxEvents 10 -ErrorAction Stop | Where-Object {
        $_.TimeCreated -ge $launchedAt -and $_.Message -match [regex]::Escape($exeName)
    } | Select-Object -First 1
}
catch {
    # No matching events at all -> Get-WinEvent throws; treat as no crash.
    $crash = $null
}

if ($crash) {
    Write-Error "App crashed during startup (Application Error logged for $exeName at $($crash.TimeCreated)). This is the S1 startup access-violation regression."
    exit 1
}

if ($exited -and $exitCode -ne 0) {
    Write-Error ("App exited during startup with non-zero code 0x{0:X8}." -f ([uint32]$exitCode))
    exit 1
}

Write-Host "App startup contract: process survived startup with no crash event."
