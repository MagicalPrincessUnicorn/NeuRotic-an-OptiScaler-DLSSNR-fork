#include <windows.h>
#include <imgui.h>
#include <imgui_impl_win32.h>
#include "menu/SleekMotion.h"
#include <cstdio>

// Exercise the shipped Win32 backend clock without a game, GPU or visible window.
struct Sample { double wall, elapsed; float reveal; };
static Sample SampleClock(bool duplicate)
{
    LARGE_INTEGER frequency{}, begin{}, end{};
    QueryPerformanceFrequency(&frequency);
    ImGui_ImplWin32_NewFrame();
    QueryPerformanceCounter(&begin);
    Sample result{};
    for (int frame = 0; frame < 30; ++frame)
    {
        Sleep(10);
        ImGui_ImplWin32_NewFrame();
        if (duplicate) ImGui_ImplWin32_NewFrame();
        const float dt = ImGui::GetIO().DeltaTime;
        result.elapsed += dt;
        result.reveal = Neurotic::Sleek::LinearStep(result.reveal, 1.f, dt,
                                                   1.f / Neurotic::Sleek::RevealDuration);
    }
    QueryPerformanceCounter(&end);
    result.wall = double(end.QuadPart - begin.QuadPart) / frequency.QuadPart;
    return result;
}
int main()
{
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"NeuRoticFrameClockFixture";
    if (!RegisterClassW(&wc)) return 2;
    HWND window = CreateWindowW(wc.lpszClassName, L"Inert frame clock test", WS_OVERLAPPED,
                                0, 0, 320, 240, nullptr, nullptr, wc.hInstance, nullptr);
    if (!window) { UnregisterClassW(wc.lpszClassName, wc.hInstance); return 2; }
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    if (!ImGui_ImplWin32_Init(window)) return 2;
    const auto duplicated = SampleClock(true), single = SampleClock(false);
    std::printf("Duplicate: wall=%.6f UI elapsed=%.6f reveal=%.6f\n",
                duplicated.wall, duplicated.elapsed, duplicated.reveal);
    std::printf("Single: wall=%.6f UI elapsed=%.6f reveal=%.6f\n",
                single.wall, single.elapsed, single.reveal);
    const bool pass = duplicated.elapsed < duplicated.wall * .5 && duplicated.reveal < 1.f &&
                      single.elapsed > single.wall * .9 && single.elapsed < single.wall * 1.1 &&
                      single.reveal == 1.f;
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    DestroyWindow(window);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    std::puts(pass ? "PASS real Win32 clock reproduces duplicate-tick slowdown; one tick settles reveal" :
                     "FAIL real Win32 clock timing expectations");
    return pass ? 0 : 1;
}
