#include "app/filedialog.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>

static std::wstring wide(const std::string& text){
    int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    std::wstring out(size > 0 ? size - 1 : 0, L'\0');
    if (size > 1) MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, out.data(), size);
    return out;
}

static std::string narrow(const wchar_t* text){
    int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    std::string out(size > 0 ? size - 1 : 0, '\0');
    if (size > 1) WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), size, nullptr, nullptr);
    return out;
}

bool chooseFile(const std::string& title, const std::string& kind, const std::vector<std::string>& patterns,
                std::string& path, std::string& error){
    // The filter: "Guitar Pro files\0*.gp;*.gpx\0All files\0*.*\0\0"
    std::string list;
    for (const std::string& pattern : patterns) list += (list.empty() ? "" : ";") + pattern;
    std::wstring filter = wide(kind) + L'\0' + wide(list) + L'\0' + L"All files" + L'\0' + L"*.*" + L'\0' + L'\0';
    std::wstring caption = wide(title);
    wchar_t file[4096] = L"";
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof dialog;
    dialog.hwndOwner = GetActiveWindow();
    dialog.lpstrFilter = filter.c_str();
    dialog.lpstrFile = file;
    dialog.nMaxFile = sizeof file / sizeof file[0];
    dialog.lpstrTitle = caption.c_str();
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER; // lahn's own folder stays the working one
    if (!GetOpenFileNameW(&dialog)){
        DWORD code = CommDlgExtendedError();
        if (code != 0) error = "The file dialog didn't open (error " + std::to_string(code) + "): drop the file on the window instead";
        return false;
    }
    path = narrow(file);
    return true;
}

#else
#include <cstdio>
#include <cstdlib>

// Runs a command and gives its first line of output
static bool firstLine(const std::string& command, std::string& line){
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) return false;
    char buffer[4096];
    bool got = std::fgets(buffer, sizeof buffer, pipe) != nullptr;
    int status = pclose(pipe);
    if (!got || status != 0) return false;
    line = buffer;
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
    return !line.empty();
}

static std::string quoted(const std::string& text){
    std::string out = "'";
    for (char c : text) out += c == '\'' ? std::string("'\\''") : std::string(1, c);
    return out + "'";
}

bool chooseFile(const std::string& title, const std::string& kind, const std::vector<std::string>& patterns,
                std::string& path, std::string& error){
    std::string list;
    for (const std::string& pattern : patterns) list += (list.empty() ? "" : " ") + pattern;
    bool zenity = std::system("command -v zenity >/dev/null 2>&1") == 0;
    bool kdialog = !zenity && std::system("command -v kdialog >/dev/null 2>&1") == 0;
    if (zenity){
        return firstLine("zenity --file-selection --title=" + quoted(title) + " --file-filter=" + quoted(kind + " | " + list) + " 2>/dev/null", path);
    }
    if (kdialog){
        return firstLine("kdialog --title " + quoted(title) + " --getopenfilename . " + quoted(list + "|" + kind) + " 2>/dev/null", path);
    }
    error = "No file dialog here (install zenity): drop the file on the window instead";
    return false;
}
#endif
