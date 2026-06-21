[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet("doctor", "bootstrap", "configure", "build", "test", "run", "package")]
    [string] $Command = "doctor",
    [string] $Preset = "windows-dev",
    [string] $Fixture = "canonical-verse",
    [ValidateSet("quick", "integration", "audio", "release")]
    [string] $Suite = "quick"
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
$diagnostics = Join-Path $root "build/diagnostics"

function Write-DxError {
    param([string] $Id, [string] $Message, [string] $Cause, [string] $Fix)
    [Console]::Error.WriteLine("${Id}: ${Message}")
    [Console]::Error.WriteLine("Cause: ${Cause}")
    [Console]::Error.WriteLine("Fix: ${Fix}")
    [Console]::Error.WriteLine("Details: docs/TROUBLESHOOTING.md#$($Id.ToLowerInvariant())")
}

function Resolve-Tool {
    param([string] $Name, [string] $LocalPath)
    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    $candidate = Join-Path $root $LocalPath
    if (Test-Path -LiteralPath $candidate) { return $candidate }
    return $null
}

function Get-Tools {
    $cmake = Resolve-Tool "cmake" ".tools/cmake-4.3.3-windows-x86_64/bin/cmake.exe"
    if (-not $cmake) {
        Write-DxError "DX-CMAKE-002" "CMake 3.22 or newer is required." `
            "CMake is not on PATH and the optional workspace-local tool is absent." `
            "Install CMake, reopen the terminal, then run .\scripts\dev.ps1 doctor."
        throw "tool check failed"
    }

    $ninja = Resolve-Tool "ninja" ".tools/ninja-1.13.2/ninja.exe"
    if (-not $ninja) {
        Write-DxError "DX-NINJA-003" "Ninja 1.11 or newer is required." `
            "Ninja is not on PATH and the optional workspace-local tool is absent." `
            "Install Ninja, reopen the terminal, then run .\scripts\dev.ps1 doctor."
        throw "tool check failed"
    }

    $compiler = Get-Command "cl" -ErrorAction SilentlyContinue
    $cxx = Get-Command "g++" -ErrorAction SilentlyContinue
    if (-not $compiler -and -not $cxx) {
        Write-DxError "DX-COMPILER-001" "A C++20 compiler is required." `
            "Neither MSVC cl.exe nor g++.exe is available in this terminal." `
            "Open a Visual Studio 2022 Developer PowerShell, then run .\scripts\dev.ps1 doctor."
        throw "tool check failed"
    }

    return @{
        CMake = $cmake
        CTest = Join-Path (Split-Path -Parent $cmake) "ctest.exe"
        Ninja = $ninja
        Cxx = if ($compiler) { $compiler.Source } else { $cxx.Source }
        CompilerKind = if ($compiler) { "MSVC" } else { "GNU" }
    }
}

function Invoke-External {
    param([string] $Executable, [string[]] $Arguments, [string] $Stage)
    New-Item -ItemType Directory -Force -Path $diagnostics | Out-Null
    $log = Join-Path $diagnostics "$Stage.log"
    Write-Host "> $Executable $($Arguments -join ' ')"
    $previousErrorPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    & $Executable @Arguments 2>&1 | Tee-Object -FilePath $log
    $nativeExitCode = $LASTEXITCODE
    $ErrorActionPreference = $previousErrorPreference
    if ($nativeExitCode -ne 0) {
        Write-DxError "DX-BUILD-006" "The $Stage stage failed with exit code $nativeExitCode." `
            "The underlying CMake or test command reported an error." `
            "Read $log, apply the first reported fix, then rerun .\scripts\dev.ps1 $Command."
        throw "$Stage failed"
    }
}

function Invoke-Doctor {
    $tools = Get-Tools
    $cmakeLine = (& $tools.CMake --version | Select-Object -First 1)
    $ninjaLine = (& $tools.Ninja --version | Select-Object -First 1)
    $cmakeVersion = [version]([regex]::Match($cmakeLine, '[0-9]+\.[0-9]+\.[0-9]+').Value)
    if ($cmakeVersion -lt [version]"3.22.0") {
        Write-DxError "DX-CMAKE-002" "CMake 3.22 or newer is required; found $cmakeVersion." `
            "JUCE 8.0.13 requires CMake 3.22." `
            "Upgrade CMake, reopen the terminal, then run .\scripts\dev.ps1 doctor."
        throw "tool check failed"
    }
    Write-Host "[ok] $cmakeLine"
    Write-Host "[ok] Ninja $ninjaLine"
    Write-Host "[ok] $($tools.CompilerKind) compiler: $($tools.Cxx)"
    Write-Host "[ok] Physical audio hardware is optional; fake device tests are available."
    return $tools
}

function Invoke-Configure {
    $tools = Invoke-Doctor
    $ninjaPath = $tools.Ninja.Replace('\', '/')
    $arguments = @("--preset", $Preset, "-DCMAKE_MAKE_PROGRAM=$ninjaPath")
    if ($tools.CompilerKind -eq "GNU") {
        Write-Warning "JUCE 8 does not support MinGW; configuring the foundation harness without the JUCE app target."
        $arguments += "-DCOMPOSER_BUILD_APP=OFF"
        $compilerDirectory = Split-Path -Parent $tools.Cxx
        $cCompiler = (Join-Path $compilerDirectory 'gcc.exe').Replace('\', '/')
        $cxxCompiler = $tools.Cxx.Replace('\', '/')
        $rcCompiler = (Join-Path $compilerDirectory 'windres.exe').Replace('\', '/')
        $arguments += "-DCMAKE_C_COMPILER=$cCompiler"
        $arguments += "-DCMAKE_CXX_COMPILER=$cxxCompiler"
        $arguments += "-DCMAKE_RC_COMPILER=$rcCompiler"
    }
    $archive = Join-Path $root ".cache/JUCE-8.0.13.zip"
    if (Test-Path -LiteralPath $archive) {
        $arguments += "-DCOMPOSER_JUCE_ARCHIVE=$($archive.Replace('\', '/'))"
    }
    Invoke-External $tools.CMake $arguments "configure-$Preset"
}

function Invoke-Build {
    $tools = Get-Tools
    Invoke-External $tools.CMake @("--build", "--preset", $Preset) "build-$Preset"
}

function Invoke-Test {
    $tools = Get-Tools
    Invoke-External $tools.CTest @("--preset", $Suite) "test-$Suite"
}

try {
    Push-Location $root
    switch ($Command) {
        "doctor" { Invoke-Doctor | Out-Null }
        "configure" { Invoke-Configure }
        "build" { Invoke-Build }
        "test" { Invoke-Test }
        "bootstrap" {
            Invoke-Configure
            Invoke-Build
            Invoke-Test
            Write-Host "Bootstrap complete. Raw commands are cmake --preset $Preset, cmake --build --preset $Preset, and ctest --preset quick."
        }
        "run" {
            $binary = Join-Path $root "build/$Preset/composer_fixture.exe"
            if (-not (Test-Path -LiteralPath $binary)) {
                Write-DxError "DX-FIXTURE-005" "The canonical fixture runner has not been built." `
                    "The selected preset has no composer_fixture.exe." `
                    "Run .\scripts\dev.ps1 bootstrap, then rerun this command."
                throw "fixture unavailable"
            }
            Invoke-External $binary @("--fixture", $Fixture) "run-$Fixture"
        }
        "package" {
            Write-DxError "DX-SCOPE-007" "Packaging is unavailable in S0 Foundation." `
                "Installer signing and rollback belong to the S4 release slice." `
                "Run .\scripts\dev.ps1 test -Suite release to verify the current S0 gate."
            throw "package deferred"
        }
    }
} catch {
    exit 1
} finally {
    Pop-Location -ErrorAction SilentlyContinue
}
