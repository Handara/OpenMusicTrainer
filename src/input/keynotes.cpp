#include "input/keynotes.h"

#include "imgui.h"

int keyboardNoteClass(){
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return -1;
    static const ImGuiKey KEYS[7] = { ImGuiKey_C, ImGuiKey_D, ImGuiKey_E, ImGuiKey_F, ImGuiKey_G, ImGuiKey_A, ImGuiKey_B };
    static const int CLASSES[7] = { 0, 2, 4, 5, 7, 9, 11 };
    for (int i = 0; i < 7; i++){
        if (!ImGui::IsKeyPressed(KEYS[i], false)) continue;
        const int shift = io.KeyShift ? 1 : io.KeyCtrl ? -1 : 0;
        return (CLASSES[i] + shift + 12) % 12;
    }
    return -1;
}
