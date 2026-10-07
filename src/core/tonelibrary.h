#pragma once

#include "core/tonechain.h"

#include <string>
#include <vector>

// The player's tones: one file each (core/tonechain) in their tones folder, beside the ones that come with lahn.
// Tones are known by name: a file's name follows its tone's.

// Every tone file in the folder, by name; files that aren't tones are left out, each with why in `problems`
std::vector<Tone> loadUserTones(const std::string& folder, std::vector<std::string>& problems);
// Where a tone of that name is kept (its name made safe for a file)
std::string tonePath(const std::string& folder, const std::string& name);
bool saveUserTone(const std::string& folder, const Tone& tone, std::string& error);
bool deleteUserTone(const std::string& folder, const std::string& name, std::string& error);
// A name no other tone has: `wanted` itself, or with " 2", " 3"... after it
std::string freeToneName(const std::string& wanted, const std::vector<Tone>& userTones);
// A tone file from anywhere (dropped on the window) into the folder, under a name of its own; `imported` is it
bool importTone(const std::string& path, const std::string& folder, const std::vector<Tone>& userTones, Tone& imported,
                std::string& error);
// The tone of that name: the player's, else a built-in one, else the first built-in (Clean)
Tone findTone(const std::string& name, const std::vector<Tone>& userTones);
