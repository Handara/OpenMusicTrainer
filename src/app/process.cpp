#include "app/process.h"

#include <algorithm>
#include <filesystem>

// What the program prints comes in pieces of any size: lines are cut out of them, at \n or \r (a progress line
// rewritten in place ends with \r alone)
static void takeLines(std::string& pending, const char* data, size_t size, const std::function<void(const std::string&)>& onLine){
    for (size_t i = 0; i < size; i++){
        if (data[i] != '\n' && data[i] != '\r'){
            pending += data[i];
            continue;
        }
        if (!pending.empty() && onLine) onLine(pending);
        pending.clear();
    }
}

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

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

// Windows gives a program one line of text, which the program cuts back into arguments by rules of its own: an
// argument with a space or a quote in it goes in quotes, the quotes inside it behind a backslash, and backslashes
// just before a quote doubled
static std::wstring quoted(const std::wstring& argument){
    if (!argument.empty() && argument.find_first_of(L" \t\"") == std::wstring::npos) return argument;
    std::wstring out = L"\"";
    size_t backslashes = 0;
    for (wchar_t c : argument){
        if (c == L'\\'){
            backslashes++;
        } else if (c == L'"'){
            out.append(backslashes + 1, L'\\'); // the ones before it doubled, and one for the quote itself
            backslashes = 0;
        } else {
            backslashes = 0;
        }
        out += c;
    }
    out.append(backslashes, L'\\'); // before the closing quote too
    return out + L"\"";
}

bool runProgram(const std::vector<std::string>& arguments, const std::function<void(const std::string&)>& onLine,
                const std::atomic<bool>& cancel, std::string& error){
    if (arguments.empty()) return false;
    std::wstring commandLine;
    for (const std::string& argument : arguments) commandLine += (commandLine.empty() ? L"" : L" ") + quoted(wide(argument));

    // A pipe: the program writes into one end, this reads the other. Only the writing end is handed down to it.
    SECURITY_ATTRIBUTES inherit{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
    HANDLE readEnd = nullptr, writeEnd = nullptr;
    if (!CreatePipe(&readEnd, &writeEnd, &inherit, 0)){
        error = "could not start " + arguments[0];
        return false;
    }
    SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);
    HANDLE nothing = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, 0, nullptr);

    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = nothing;
    startup.hStdOutput = writeEnd;
    startup.hStdError = writeEnd;
    PROCESS_INFORMATION process{};
    BOOL started = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
    CloseHandle(writeEnd); // the program has its own copy: with this one closed, the pipe ends when the program does
    if (nothing != INVALID_HANDLE_VALUE) CloseHandle(nothing);
    if (!started){
        CloseHandle(readEnd);
        error = "could not start " + arguments[0];
        return false;
    }

    std::string pending;
    char buffer[4096];
    bool stopped = false;
    while (true){
        DWORD waiting = 0;
        if (!PeekNamedPipe(readEnd, nullptr, 0, nullptr, &waiting, nullptr)) break; // the program ended, and all is read
        if (waiting > 0){
            DWORD got = 0;
            if (!ReadFile(readEnd, buffer, sizeof buffer, &got, nullptr) || got == 0) break;
            takeLines(pending, buffer, got, onLine);
        } else if (cancel && !stopped){
            TerminateProcess(process.hProcess, 1);
            stopped = true;
        } else {
            WaitForSingleObject(process.hProcess, 30); // nothing to read yet: a moment, or until it ends
        }
    }
    if (!pending.empty() && onLine) onLine(pending);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hProcess);
    CloseHandle(process.hThread);
    CloseHandle(readEnd);
    if (stopped) error = "stopped";
    return !stopped && code == 0;
}

struct FedProgram {
    HANDLE process = nullptr;
    HANDLE input = nullptr; // the writing end of its input
};

FedProgram* startFedProgram(const std::vector<std::string>& arguments, std::string& error){
    if (arguments.empty()) return nullptr;
    std::wstring commandLine;
    for (const std::string& argument : arguments) commandLine += (commandLine.empty() ? L"" : L" ") + quoted(wide(argument));
    SECURITY_ATTRIBUTES inherit{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
    HANDLE readEnd = nullptr, writeEnd = nullptr;
    if (!CreatePipe(&readEnd, &writeEnd, &inherit, 1 << 20)){
        error = "could not start " + arguments[0];
        return nullptr;
    }
    SetHandleInformation(writeEnd, HANDLE_FLAG_INHERIT, 0); // only its reading end goes to the program
    HANDLE nothing = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, 0, nullptr);
    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = readEnd;
    startup.hStdOutput = nothing;
    startup.hStdError = nothing;
    PROCESS_INFORMATION process{};
    BOOL started = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
    CloseHandle(readEnd);
    if (nothing != INVALID_HANDLE_VALUE) CloseHandle(nothing);
    if (!started){
        CloseHandle(writeEnd);
        error = "could not start " + arguments[0];
        return nullptr;
    }
    CloseHandle(process.hThread);
    return new FedProgram{ process.hProcess, writeEnd };
}

bool feedProgram(FedProgram* program, const void* data, size_t size){
    const char* at = (const char*)data;
    while (size > 0){
        DWORD wrote = 0;
        if (!WriteFile(program->input, at, (DWORD)std::min<size_t>(size, 1 << 20), &wrote, nullptr) || wrote == 0) return false;
        at += wrote;
        size -= wrote;
    }
    return true;
}

bool finishFedProgram(FedProgram* program, std::string& error){
    CloseHandle(program->input); // the end of what it's given
    WaitForSingleObject(program->process, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(program->process, &code);
    CloseHandle(program->process);
    delete program;
    if (code != 0) error = "it ended with " + std::to_string(code);
    return code == 0;
}

std::string findProgram(const std::string& name){
    wchar_t path[MAX_PATH * 4];
    DWORD length = SearchPathW(nullptr, wide(name).c_str(), L".exe", (DWORD)(sizeof path / sizeof path[0]), path, nullptr);
    return length > 0 && length < sizeof path / sizeof path[0] ? narrow(path) : "";
}

#else
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <poll.h>
#include <sstream>
#include <sys/wait.h>
#include <unistd.h>

bool runProgram(const std::vector<std::string>& arguments, const std::function<void(const std::string&)>& onLine,
                const std::atomic<bool>& cancel, std::string& error){
    if (arguments.empty()) return false;
    int ends[2];
    if (pipe(ends) != 0){
        error = "could not start " + arguments[0];
        return false;
    }
    std::vector<char*> argv;
    for (const std::string& argument : arguments) argv.push_back(const_cast<char*>(argument.c_str()));
    argv.push_back(nullptr);

    pid_t child = fork();
    if (child < 0){
        close(ends[0]);
        close(ends[1]);
        error = "could not start " + arguments[0];
        return false;
    }
    if (child == 0){
        // The program-to-be: its outputs into the pipe, nothing to read, then it becomes the program
        dup2(ends[1], STDOUT_FILENO);
        dup2(ends[1], STDERR_FILENO);
        close(ends[0]);
        close(ends[1]);
        if (!freopen("/dev/null", "r", stdin)) _exit(127);
        execvp(argv[0], argv.data());
        _exit(127); // it couldn't be run
    }
    close(ends[1]);

    std::string pending;
    char buffer[4096];
    bool stopped = false;
    while (true){
        pollfd waiting{ ends[0], POLLIN, 0 };
        int ready = poll(&waiting, 1, 30);
        if (ready > 0){
            ssize_t got = read(ends[0], buffer, sizeof buffer);
            if (got <= 0) break; // the program ended, and all is read
            takeLines(pending, buffer, (size_t)got, onLine);
        } else if (ready < 0 && errno != EINTR){
            break;
        }
        if (cancel && !stopped){
            kill(child, SIGTERM);
            stopped = true;
        }
    }
    if (!pending.empty() && onLine) onLine(pending);
    close(ends[0]);
    int status = 0;
    waitpid(child, &status, 0);
    if (stopped) error = "stopped";
    else if (WIFEXITED(status) && WEXITSTATUS(status) == 127) error = "could not start " + arguments[0];
    return !stopped && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

struct FedProgram {
    pid_t child = -1;
    int input = -1; // the writing end of its input
};

FedProgram* startFedProgram(const std::vector<std::string>& arguments, std::string& error){
    if (arguments.empty()) return nullptr;
    int ends[2];
    if (pipe(ends) != 0){
        error = "could not start " + arguments[0];
        return nullptr;
    }
    std::vector<char*> argv;
    for (const std::string& argument : arguments) argv.push_back(const_cast<char*>(argument.c_str()));
    argv.push_back(nullptr);
    pid_t child = fork();
    if (child < 0){
        close(ends[0]);
        close(ends[1]);
        error = "could not start " + arguments[0];
        return nullptr;
    }
    if (child == 0){
        dup2(ends[0], STDIN_FILENO);
        close(ends[0]);
        close(ends[1]);
        if (!freopen("/dev/null", "w", stdout) || !freopen("/dev/null", "w", stderr)) _exit(127);
        execvp(argv[0], argv.data());
        _exit(127);
    }
    close(ends[0]);
    signal(SIGPIPE, SIG_IGN); // a program that ended makes writing fail, not this one stop
    return new FedProgram{ child, ends[1] };
}

bool feedProgram(FedProgram* program, const void* data, size_t size){
    const char* at = (const char*)data;
    while (size > 0){
        ssize_t wrote = write(program->input, at, size);
        if (wrote < 0 && errno == EINTR) continue;
        if (wrote <= 0) return false;
        at += wrote;
        size -= (size_t)wrote;
    }
    return true;
}

bool finishFedProgram(FedProgram* program, std::string& error){
    close(program->input);
    int status = 0;
    waitpid(program->child, &status, 0);
    delete program;
    const bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
    if (!ok) error = WIFEXITED(status) && WEXITSTATUS(status) == 127 ? "could not start it" : "it didn't end well";
    return ok;
}

std::string findProgram(const std::string& name){
    const char* path = std::getenv("PATH");
    if (!path) return "";
    std::istringstream folders(path);
    std::string folder;
    while (std::getline(folders, folder, ':')){
        std::filesystem::path candidate = std::filesystem::path(folder.empty() ? "." : folder) / name;
        if (access(candidate.c_str(), X_OK) == 0) return candidate.string();
    }
    return "";
}
#endif
