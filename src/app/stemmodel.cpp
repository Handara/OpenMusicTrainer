#include "app/stemmodel.h"

#include "core/addon.h"
#include "core/stemsplit.h"
#include "onnxruntime_c_api.h"

#include <algorithm>
#include <filesystem>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
const char* const RUNTIME_FILE = "onnxruntime.dll";
#elif defined(__APPLE__)
#include <dlfcn.h>
const char* const RUNTIME_FILE = "libonnxruntime.dylib";
#else
#include <dlfcn.h>
const char* const RUNTIME_FILE = "libonnxruntime.so";
#endif

namespace fs = std::filesystem;

const char* const MODEL_FILE = "bass.onnx";

static struct {
    void* library = nullptr;
    const OrtApi* api = nullptr;
    OrtEnv* env = nullptr;
    OrtSession* session = nullptr;
    OrtMemoryInfo* memory = nullptr;
    std::string error;
} stem;

bool stemsAddonInstalled(const std::string& addonsDir){
    AddonInfo info;
    fs::path folder = fs::path(addonsDir) / STEMS_ADDON;
    std::error_code ec;
    return readAddon(addonsDir, STEMS_ADDON, info) && fs::is_regular_file(folder / RUNTIME_FILE, ec) && fs::is_regular_file(folder / MODEL_FILE, ec);
}

// The runtime's errors come as objects to read and free: true if there was none
static bool ok(OrtStatus* status, std::string& error){
    if (!status) return true;
    error = stem.api->GetErrorMessage(status);
    stem.api->ReleaseStatus(status);
    return false;
}

bool openStemModel(const std::string& addonsDir, std::string& error){
    if (stem.session) return true;
    closeStemModel();
    if (!stemsAddonInstalled(addonsDir)){
        error = "the stems add-on isn't installed";
        return false;
    }
    const fs::path folder = fs::path(addonsDir) / STEMS_ADDON, runtime = folder / RUNTIME_FILE, model = folder / MODEL_FILE;
    using GetApiBase = const OrtApiBase* (ORT_API_CALL*)(void);
    GetApiBase getBase = nullptr;
#ifdef _WIN32
    // Its own folder searched for what it needs beside it
    stem.library = LoadLibraryExW(runtime.wstring().c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (stem.library) getBase = (GetApiBase)GetProcAddress((HMODULE)stem.library, "OrtGetApiBase");
#else
    stem.library = dlopen(runtime.string().c_str(), RTLD_NOW | RTLD_LOCAL);
    if (stem.library) getBase = (GetApiBase)dlsym(stem.library, "OrtGetApiBase");
#endif
    if (!stem.library || !getBase){
        error = std::string("the add-on's runtime (") + RUNTIME_FILE + ") couldn't be loaded: is the add-on for this system?";
        closeStemModel();
        return false;
    }
    // The newest interface it has that lahn knows
    const OrtApiBase* base = getBase();
    for (int version = ORT_API_VERSION; version >= 17 && !stem.api; version--) stem.api = base->GetApi((uint32_t)version);
    if (!stem.api){
        error = "the add-on's runtime is too old for this lahn";
        closeStemModel();
        return false;
    }
    OrtSessionOptions* options = nullptr;
    bool good = ok(stem.api->CreateEnv(ORT_LOGGING_LEVEL_ERROR, "lahn", &stem.env), error)
             && ok(stem.api->CreateSessionOptions(&options), error);
    if (good){
        // Half the processor's threads: the game keeps drawing while it works
        int threads = std::max(1, (int)std::thread::hardware_concurrency() / 2);
        good = ok(stem.api->SetIntraOpNumThreads(options, threads), error)
            && ok(stem.api->SetSessionGraphOptimizationLevel(options, ORT_ENABLE_ALL), error);
    }
#ifdef _WIN32
    if (good) good = ok(stem.api->CreateSession(stem.env, model.wstring().c_str(), options, &stem.session), error);
#else
    if (good) good = ok(stem.api->CreateSession(stem.env, model.string().c_str(), options, &stem.session), error);
#endif
    if (options) stem.api->ReleaseSessionOptions(options);
    if (good) good = ok(stem.api->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &stem.memory), error);
    if (!good){
        error = "the add-on's model couldn't be loaded: " + error;
        closeStemModel();
        return false;
    }
    return true;
}

bool runStemModel(const std::vector<float>& input, std::vector<float>& output){
    stem.error.clear();
    if (!stem.session || (int)input.size() != STEM_TENSOR){
        stem.error = "the model isn't loaded";
        return false;
    }
    const int64_t shape[4] = { 1, 4, STEM_BINS, STEM_FRAMES };
    OrtValue* in = nullptr;
    OrtValue* out = nullptr;
    const char* inputNames[1] = { "input" };
    const char* outputNames[1] = { "output" };
    bool good = ok(stem.api->CreateTensorWithDataAsOrtValue(stem.memory, (void*)input.data(), input.size() * sizeof(float), shape, 4,
                                                            ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &in), stem.error);
    const OrtValue* inputs[1] = { in };
    if (good) good = ok(stem.api->Run(stem.session, nullptr, inputNames, inputs, 1, outputNames, 1, &out), stem.error);
    float* data = nullptr;
    if (good) good = ok(stem.api->GetTensorMutableData(out, (void**)&data), stem.error);
    if (good) output.assign(data, data + STEM_TENSOR);
    if (out) stem.api->ReleaseValue(out);
    if (in) stem.api->ReleaseValue(in);
    return good;
}

std::string stemModelError(){
    return stem.error;
}

void closeStemModel(){
    if (stem.api){
        if (stem.memory) stem.api->ReleaseMemoryInfo(stem.memory);
        if (stem.session) stem.api->ReleaseSession(stem.session);
        if (stem.env) stem.api->ReleaseEnv(stem.env);
    }
    stem.memory = nullptr;
    stem.session = nullptr;
    stem.env = nullptr;
    stem.api = nullptr;
    if (stem.library){
#ifdef _WIN32
        FreeLibrary((HMODULE)stem.library);
#else
        dlclose(stem.library);
#endif
    }
    stem.library = nullptr;
}
