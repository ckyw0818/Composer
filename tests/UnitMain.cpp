#include "tests/TestRunner.h"

int runContractTests();
int runMidiSongTests();
int runArrangementTests();
int runRecordingTests();

int main() {
    const int contract = runContractTests();
    const int midiSong = runMidiSongTests();
    const int arrangement = runArrangementTests();
    const int recording = runRecordingTests();
    return (contract == 0 && midiSong == 0 && arrangement == 0 && recording == 0) ? 0 : 1;
}
