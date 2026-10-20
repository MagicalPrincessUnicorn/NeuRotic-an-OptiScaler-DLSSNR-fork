#include <windows.h>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    const auto destination = std::filesystem::absolute(argv[2]);
    assert(!std::filesystem::exists(destination));
    std::filesystem::create_directories(destination);
    SetEnvironmentVariableA("NEUROTIC_CANDIDATE_OBSERVER", "1");
    SetEnvironmentVariableA("NEUROTIC_FRAME_TRACE_SESSION", "dddddddddddddddddddddddddddddddd");
    SetEnvironmentVariableW(L"NEUROTIC_DIAGNOSTIC_DIRECTORY", destination.c_str());
    const auto dll = LoadLibraryW(std::filesystem::absolute(argv[1]).c_str());
    assert(dll);
    const auto begin = reinterpret_cast<void(*)()>(GetProcAddress(dll, "BeginCapture"));
    const auto end = reinterpret_cast<void(*)()>(GetProcAddress(dll, "EndCapture"));
    assert(begin && end);
    begin();
    Sleep(6000); // First publication at 250 ms; exercise repeated idle copies.
    end();
    const auto trace = destination / L"candidate-dddddddddddddddddddddddddddddddd" / L"trace.jsonl";
    bool footer = false;
    for (unsigned i = 0; i < 200 && !footer; ++i) {
        std::ifstream input(trace);
        std::string line;
        while (std::getline(input, line))
            if (line.find("\"type\":\"footer\"") != std::string::npos &&
                line.find("\"complete_transport\":true") != std::string::npos) footer = true;
        Sleep(10);
    }
    assert(footer);
    // The worker owns a module reference until its RAII teardown is finished.
    FreeLibrary(dll);
    std::cout << "PASS: LTCG DLL without snapshot reader, repeated publication and complete footer\n";
}
