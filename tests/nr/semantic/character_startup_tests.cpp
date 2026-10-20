#include "nr/semantic/character/CharacterRuntime.h"
#include "character_test_errors.h"
#include <chrono>
#include <thread>
#include <iostream>
using namespace Neurotic::Semantic::Character;
int main(int argc,char** argv){CharacterTestQuietFailures();if(argc!=2)return 2;
    StartCharacterWorker(std::filesystem::absolute(argv[1]));
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(9);
    while(TryCharacterRuntimeView().worker==WorkerState::Starting&&std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(std::chrono::milliseconds(5));
    const bool ready=TryCharacterRuntimeView().worker==WorkerState::Ready;
    StopCharacterWorker();while(TryCharacterRuntimeView().worker==WorkerState::Stopping&&std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(std::chrono::milliseconds(5));
    if(!ready){std::cerr<<"FAIL delayed model startup treated as frame timeout\n";return 1;}
    std::cout<<"PASS slow initialization has separate budget; stop remains asynchronous\n";return 0;
}
