#pragma once

#include "core/chart.h"

#include <string>
#include <vector>

// Guitar Pro files, the biggest library of tabs there is, as lahn charts: their guitar and bass tracks become parts,
// with their tunings, every note on its string and fret, timed by the file's tempos, time and key signatures.
// Repeats are played out (alternate endings too), as a recording of the song plays them. Drum and keys tracks are
// left out, and so are grace notes and dead notes, for now; bends and slides come in as plain notes. Guitar Pro files
// hold no audio: the chart has none. Pure logic, but for reading the file.
//
// Guitar Pro 7 and 8 (.gp) keep the score as XML (score.gpif) in a zip.

struct GuitarProImport {
    Chart chart;
    std::vector<std::string> leftOut; // what couldn't come in, for the player: "Drums (a drum track)"
};

// A .gp file, read and turned into a chart
bool importGuitarPro(const std::string& path, GuitarProImport& out, std::string& error);
// The score itself (score.gpif's text)
bool readGpif(const std::string& xml, GuitarProImport& out, std::string& error);
