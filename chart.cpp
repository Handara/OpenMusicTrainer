#include "chart.h"

// Walks the tempo map section by section, adding the duration of each section the tick passes through
double tickToSeconds(const Chart& chart, int tick){
    double seconds = chart.offset;
    for (size_t i = 0; i < chart.tempoMap.size(); i++){
        const TempoChange& tempo = chart.tempoMap[i];
        if (tick <= tempo.tick) break;

        int sectionEnd = tick;
        bool hasNext = i + 1 < chart.tempoMap.size();
        if (hasNext && chart.tempoMap[i+1].tick < tick) sectionEnd = chart.tempoMap[i+1].tick;

        double beats = (double)(sectionEnd - tempo.tick) / chart.resolution;
        seconds += beats * 60.0 / tempo.bpm;
    }
    return seconds;
}
