param(
    [Parameter(Mandatory = $true)]
    [string] $SourceRoot
)

$ErrorActionPreference = "Stop"
$checks = @(
    @{ File = "src/app/MainComponent.cpp"; Text = "onTrackDeleteRequested"; Name = "track delete wiring" },
    @{ File = "src/app/MainComponent.cpp"; Text = "onChordShortcut"; Name = "Ctrl+K chord wiring" },
    @{ File = "src/app/MainComponent.cpp"; Text = "ProjectFile::load"; Name = "project open wiring" },
    @{ File = "src/app/MainComponent.cpp"; Text = "ProjectFile::save"; Name = "project save wiring" },
    @{ File = "src/app/MainComponent.cpp"; Text = "onGridChanged"; Name = "grid subdivision wiring" },
    @{ File = "src/app/MainComponent.cpp"; Text = "onTempoChanged"; Name = "tempo wiring" },
    @{ File = "src/ui/TrackList.cpp"; Text = "onTrackReordered"; Name = "track drag reorder wiring" },
    @{ File = "src/ui/PianoRoll.cpp"; Text = "onChordShortcut"; Name = "piano-roll chord shortcut" },
    @{ File = "src/ui/PianoRoll.cpp"; Text = "DragMode::marquee"; Name = "piano-roll marquee selection" },
    @{ File = "src/ui/PianoRoll.cpp"; Text = "editor_.moveNotes"; Name = "piano-roll group move" },
    @{ File = "src/ui/PianoRoll.cpp"; Text = "juce::KeyPress('c'"; Name = "piano-roll copy shortcut" },
    @{ File = "src/ui/PianoRoll.cpp"; Text = "juce::KeyPress('v'"; Name = "piano-roll paste shortcut" },
    @{ File = "src/commands/ProjectEditor.cpp"; Text = "ProjectEditor::pasteNotes"; Name = "atomic note paste command" },
    @{ File = "src/ui/ArrangementBar.cpp"; Text = "seed_.setRange"; Name = "rhythm seed control" },
    @{ File = "src/ui/ChordPopover.h"; Text = "Insert notes"; Name = "chord insert control" }
)

foreach ($check in $checks) {
    $path = Join-Path $SourceRoot $check.File
    if (-not (Test-Path -LiteralPath $path)) {
        throw "S2 interaction contract file missing: $($check.File)"
    }
    $content = Get-Content -LiteralPath $path -Raw
    if (-not $content.Contains($check.Text)) {
        throw "S2 interaction contract missing $($check.Name) in $($check.File)"
    }
}

Write-Host "S2 interaction source contract passed ($($checks.Count) checks)."
