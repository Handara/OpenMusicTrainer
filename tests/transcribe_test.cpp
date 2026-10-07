#include "doctest/doctest.h"

#include "core/music.h"
#include "core/synth.h"
#include "core/transcribe.h"

#include <cmath>
#include <functional>
#include <vector>

// A bass line, bar after bar: roots on the one, a fifth and its octave, a pickup sixteenth; E, A, D and G in turn
struct LineNote { double beat, length; int pitch; };
static std::vector<LineNote> bassLine(int bars){
    const int roots[] = { 28, 33, 38, 31 };
    std::vector<LineNote> line;
    for (int bar = 0; bar < bars; bar++){
        int root = roots[bar % 4];
        double b = bar * 4.0;
        for (const LineNote& note : std::vector<LineNote>{ { 0, 1.0, root }, { 1, 0.5, root + 7 }, { 1.5, 0.5, root + 12 },
                                                           { 2, 0.75, root }, { 2.75, 0.25, root }, { 3, 1.0, root + 7 } }){
            line.push_back({ b + note.beat, note.length, note.pitch });
        }
    }
    return line;
}

// The line played on hardthz's synth bass, each note a little shorter than written, as a bass player leaves room;
// `timeOf` turns a beat into seconds
static std::vector<float> play(const std::vector<LineNote>& line, int rate, const std::function<double(double)>& timeOf){
    double end = timeOf(line.back().beat + line.back().length) + 1.0;
    std::vector<float> out((size_t)(end * rate), 0.0f), note;
    for (const LineNote& played : line){
        double start = timeOf(played.beat), stop = timeOf(played.beat + played.length * 0.85);
        int count = (int)((stop - start + 0.04) * rate);
        note.assign(count, 0.0f);
        renderBass(note.data(), count, midiToFrequency((float)played.pitch), rate);
        int muteFrom = (int)((stop - start) * rate);
        for (int i = muteFrom; i < count; i++) note[i] *= 1.0f - (float)(i - muteFrom) / (count - muteFrom);
        size_t at = (size_t)(start * rate);
        for (int i = 0; i < count && at + i < out.size(); i++) out[at + i] += 0.4f * note[i];
    }
    return out;
}

// How many of the line's notes the chart has, on their tick and pitch
static int matched(const Transcription& heard, const std::vector<LineNote>& line){
    const FrettedTrack& bass = heard.chart.frettedTracks.at(0);
    int found = 0;
    for (const LineNote& note : line){
        int tick = (int)std::lround(note.beat * heard.chart.resolution);
        for (const FrettedNote& played : bass.notes){
            if (played.tick == tick && bass.tuning[played.stringIndex] + played.fret == note.pitch){ found++; break; }
        }
    }
    return found;
}

TEST_CASE("a bass line in a recording, written down: its tempo, where the bars start, every note"){
    const int rate = 44100;
    const double bpm = 96.0, offset = 0.37;
    std::vector<LineNote> line = bassLine(8);
    std::vector<float> recording = play(line, rate, [&](double beat){ return offset + beat * 60.0 / bpm; });

    Transcription heard;
    std::string error;
    REQUIRE_MESSAGE(transcribeBass(recording, rate, "Line", heard, error), error);
    CHECK(heard.bpm == doctest::Approx(bpm).epsilon(0.01));
    CHECK(heard.chart.offset == doctest::Approx(offset).epsilon(0.05)); // bar one, where it starts
    CHECK(heard.chart.title == "Line");
    REQUIRE(heard.chart.frettedTracks.size() == 1);
    CHECK(heard.chart.frettedTracks[0].tuning == std::vector<int>{ 28, 33, 38, 43 });
    int found = matched(heard, line);
    CAPTURE(found);
    CAPTURE(heard.notes);
    CHECK(found >= (int)line.size() * 95 / 100);
    CHECK(heard.notes <= (int)line.size() * 105 / 100); // hardly anything that wasn't played
}

TEST_CASE("a band speeding up: the beats follow it, and the notes stay on them"){
    const int rate = 44100;
    std::vector<LineNote> line = bassLine(8);
    // From 90 to 102 beats a minute over the 32 beats
    auto timeOf = [](double beat){
        double t = 0.5;
        for (double b = 0.0; b < beat; b += 0.01) t += 0.01 * 60.0 / (90.0 + 12.0 * std::min(b, 32.0) / 32.0);
        return t;
    };
    std::vector<float> recording = play(line, rate, timeOf);
    Transcription heard;
    std::string error;
    REQUIRE_MESSAGE(transcribeBass(recording, rate, "Faster", heard, error), error);
    int found = matched(heard, line);
    CAPTURE(found);
    CHECK(found >= (int)line.size() * 90 / 100);
    CHECK(heard.chart.tempoMap.size() > 4); // the tempo moves with it
    // Played back at the chart's tempo, the last note falls where it was played
    const FrettedTrack& bass = heard.chart.frettedTracks[0];
    double lastHeard = tickToSeconds(heard.chart, bass.notes.back().tick); // the offset included
    CHECK(lastHeard == doctest::Approx(timeOf(line.back().beat)).epsilon(0.01));
}

TEST_CASE("silence, or a few clicks, isn't a bass line"){
    Transcription heard;
    std::string error;
    std::vector<float> silence(44100 * 2, 0.0f);
    CHECK_FALSE(transcribeBass(silence, 44100, "", heard, error));
    CHECK(!error.empty());
}
