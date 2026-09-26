#include "doctest/doctest.h"

#include "core/notation.h"

TEST_CASE("treble staff positions"){
    CHECK(trebleStaffNote(64).position == 0);  // E4: bottom line
    CHECK(trebleStaffNote(65).position == 1);  // F4: first space
    CHECK(trebleStaffNote(71).position == 4);  // B4: middle line
    CHECK(trebleStaffNote(77).position == 8);  // F5: top line
    CHECK(trebleStaffNote(60).position == -2); // middle C: one ledger line below
    CHECK(trebleStaffNote(81).position == 10); // A5: one ledger line above
}

TEST_CASE("black keys are spelled as sharps on the natural's position"){
    StaffNote cSharp = trebleStaffNote(61);
    CHECK(cSharp.position == -2); // same position as C
    CHECK(cSharp.accidental == 1);
    CHECK(trebleStaffNote(60).accidental == 0);
    StaffNote fSharp = trebleStaffNote(66);
    CHECK(fSharp.position == 1);  // on F's space
    CHECK(fSharp.accidental == 1);
}

TEST_CASE("guitar open strings, written an octave up"){
    // Sounding pitches of standard tuning, low to high, and where guitar notation puts them
    const int openStrings[6] = { 40, 45, 50, 55, 59, 64 };
    const int expectedPositions[6] = { -7, -4, -1, 2, 4, 7 }; // E3 A3 D4 G4 B4 E5
    for (int i = 0; i < 6; i++){
        CHECK(trebleStaffNote(openStrings[i] + GUITAR_WRITTEN_OCTAVE_SHIFT).position == expectedPositions[i]);
    }
    CHECK(ledgerLineCount(-7) == 3); // the low E sits under three ledger lines
}

TEST_CASE("ledger lines and stems"){
    CHECK(ledgerLineCount(-1) == 0); // just under the staff: no line needed
    CHECK(ledgerLineCount(-2) == 1);
    CHECK(ledgerLineCount(-3) == 1); // hangs under the first ledger line
    CHECK(ledgerLineCount(0) == 0);
    CHECK(ledgerLineCount(8) == 0);
    CHECK(ledgerLineCount(10) == 1);
    CHECK(ledgerLineCount(13) == 2);
    CHECK(stemUp(3));
    CHECK_FALSE(stemUp(4)); // middle line: stem down
}
