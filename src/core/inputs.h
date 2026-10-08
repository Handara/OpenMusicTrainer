#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Instruments on audio inputs. An audio interface (a Focusrite Scarlett, say) is one device with several inputs,
// its channels: a guitar on one, a bass or a microphone on another. Each instrument listens to its own channel, so
// they're never mixed together. Pure logic; opening devices is the audio layer's job.

// Picks one channel out of interleaved samples (frame by frame: channel 0, 1..., 0, 1...). A channel of -1 mixes
// them all down to one instead (their average), for a device used as a single input. The inputs switched off (bit c
// of `off` for channel c) are left out: of the mix, and a channel asked for that's off gives silence.
void takeChannel(const float* interleaved, int frames, int channels, int channel, float* out, uint64_t off = 0);
// The inputs switched off as bits, for takeChannel (channels past 63 can't be)
uint64_t inputsOffMask(const std::vector<int>& inputsOff);

// The instruments a player plugs in: each is given one channel of the input device
enum class InputRole { Guitar, Bass, Voice };
const char* inputRoleName(InputRole role); // "Guitar"

// The instrument a tuning is for: a lowest string below C2 is a bass's, anything higher a guitar's
InputRole roleForTuning(int lowestPitch);

// Telling an instrument by its lowest open string: a bass's E1 is 41 Hz, a guitar's E2 82 Hz. Below 65 Hz it's a
// bass (or a baritone or seven-string, tuned that low); up to about 100 Hz a guitar; higher, a voice or another
// instrument that the note alone can't tell. What it sounds like, as the player would say it.
std::string guessInstrument(float lowestFrequency);
// Whether that fits the role a channel was given (a guitar-range note on the bass's channel doesn't)
bool fitsRole(InputRole role, float lowestFrequency);

// An input's noise floor: how loud it is with nothing played. Inputs differ a lot: a microphone hears the room, an
// instrument input is near silent, and a bass through it with modest gain is far quieter than a voice on the mic. So
// whether an input is being played is judged against its own floor, not a fixed level. The floor follows the
// quietest level heard (never below -100 dB: some devices send exact zeros between notes), and rises slowly so a room
// getting noisier is followed too: a few dB a second, and far slower while the input is played, so a long note
// isn't taken for the new floor.
struct NoiseFloor {
    float db = 0.0f;
    bool started = false;
};
void trackNoiseFloor(NoiseFloor& floor, float levelDb, float seconds);
// How far above its floor an input is now, in dB; 0 for an input quieter than any playing could be
float riseAboveFloor(const NoiseFloor& floor, float levelDb);
// Clearly played: well above its floor
bool isSounding(const NoiseFloor& floor, float levelDb);

// Finding the input an instrument is on, while the player plays it: from how far each input has risen above its
// floor (0 for one not sounding). Sound bleeds between inputs (singing makes a bass's open strings ring, and its
// pickups hear them), so the input that rose most is the one played. Inputs already given to another instrument
// (`taken`) count 12 dB less: enough to beat bleed, not so much that roles set wrong can't be found again.
// -1 while none is sounding.
int playedInput(const std::vector<float>& rises, const std::vector<bool>& taken);
