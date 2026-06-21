#include "tests/TestRunner.h"

int runContractTests();
int runMidiSongTests();

int main() {
    const int contract = runContractTests();
    const int midiSong = runMidiSongTests();
    return (contract == 0 && midiSong == 0) ? 0 : 1;
}
