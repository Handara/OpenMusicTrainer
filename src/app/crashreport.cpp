#include "app/crashreport.h"

#include <cstdarg>
#include <cstdio>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
    #include <dbghelp.h>
#elif defined(__linux__)
    #include <csignal>
    #include <execinfo.h>
    #include <fcntl.h>
    #include <unistd.h>
#endif

static std::string reportPath;

#ifdef _WIN32

static FILE* report = nullptr;

// Both to the console and to the report
static void say(const char* format, ...){
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    if (report){
        va_start(args, format);
        vfprintf(report, format, args);
        va_end(args);
    }
}

static LONG WINAPI onCrash(EXCEPTION_POINTERS* crash){
    report = std::fopen(reportPath.c_str(), "w");
    say("\nlahn crashed: exception 0x%08lX at %p\n", crash->ExceptionRecord->ExceptionCode, crash->ExceptionRecord->ExceptionAddress);
#if defined(_M_X64)
    HANDLE process = GetCurrentProcess(), thread = GetCurrentThread();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(process, nullptr, TRUE);
    CONTEXT context = *crash->ContextRecord;
    STACKFRAME64 frame = {};
    frame.AddrPC.Offset = context.Rip;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrStack.Offset = context.Rsp;
    frame.AddrPC.Mode = frame.AddrFrame.Mode = frame.AddrStack.Mode = AddrModeFlat;
    for (int depth = 0; depth < 48; depth++){
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &context, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) break;
        DWORD64 address = frame.AddrPC.Offset;
        if (address == 0) break;
        alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 256] = {};
        SYMBOL_INFO* symbol = (SYMBOL_INFO*)buffer;
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 255;
        DWORD64 offset = 0;
        const char* name = SymFromAddr(process, address, &offset, symbol) ? symbol->Name : "?";
        IMAGEHLP_LINE64 line = {};
        line.SizeOfStruct = sizeof(line);
        DWORD column = 0;
        if (SymGetLineFromAddr64(process, address, &column, &line)) say("  %s  %s:%lu\n", name, line.FileName, line.LineNumber);
        else say("  %s  (0x%llx)\n", name, (unsigned long long)address);
    }
    SymCleanup(process);
#endif
    if (report){
        say("This report is in %s\n", reportPath.c_str());
        std::fclose(report);
    }
    return EXCEPTION_EXECUTE_HANDLER; // the game ends; Windows' own crash dialog isn't needed after this
}

void installCrashReport(const std::string& path){
    reportPath = path;
    SetUnhandledExceptionFilter(onCrash);
}

#elif defined(__linux__)

static void put(int file, const char* text){
    ssize_t written = write(file, text, __builtin_strlen(text));
    (void)written; // crashing: nothing to do if it can't be written
}

// Only calls that are safe inside a signal handler: the stack goes straight to the file descriptors
static void onCrash(int signal){
    const char* heading = "\nlahn crashed. Where it was:\n";
    void* frames[64];
    int count = backtrace(frames, 64);
    put(STDERR_FILENO, heading);
    backtrace_symbols_fd(frames, count, STDERR_FILENO);
    int file = open(reportPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (file >= 0){
        put(file, heading);
        backtrace_symbols_fd(frames, count, file);
        close(file);
    }
    std::signal(signal, SIG_DFL); // and on to the system's own handling (a core dump, the exit code)
    std::raise(signal);
}

void installCrashReport(const std::string& path){
    reportPath = path;
    void* warmUp[1];
    backtrace(warmUp, 1); // loads what backtrace needs now, not in the handler
    for (int signal : { SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS }) std::signal(signal, onCrash);
}

#else

void installCrashReport(const std::string& path){ reportPath = path; }

#endif
