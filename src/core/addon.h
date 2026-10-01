#pragma once

#include <string>

// Add-ons: parts of lahn that are optional, and too big to give everyone (the stems add-on's model and its runtime are
// 40 MB). One is a zip named .lahnaddon holding plain files, among them addon.txt, which says what it is:
//     lahn_addon 1
//     name stems
//     version 1
// Installed, it's a folder of that name in the player's add-ons folder.

const char* const ADDON_EXTENSION = ".lahnaddon";

struct AddonInfo {
    std::string name;
    int version = 0;
};

// What's installed under that name; false if it isn't
bool readAddon(const std::string& addonsDir, const std::string& name, AddonInfo& info);
// Unpacks an add-on into the add-ons folder, in place of the same one already there. Refused, with why, if it isn't
// one: every file must be a plain one, and addon.txt must be among them.
bool installAddon(const std::string& zipPath, const std::string& addonsDir, AddonInfo& installed, std::string& error);
