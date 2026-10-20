#define NOMINMAX
#include <windows.h>
#include "nr/semantic/character/CharacterRuntime.h"
#include "character_test_errors.h"
#include <chrono>
#include <thread>
#include <iostream>
using namespace Neurotic::Semantic::Character;
static void Await(WorkerState expected,unsigned milliseconds){
    const auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(milliseconds);
    while(std::chrono::steady_clock::now()<end){if(TryCharacterRuntimeView().worker==expected)return;std::this_thread::sleep_for(std::chrono::milliseconds(5));}
    throw std::runtime_error("worker lifecycle deadline: "+TryCharacterRuntimeView().reason);
}
int main(int argc,char** argv){try{
    CharacterTestQuietFailures();if(argc!=4)throw std::runtime_error("valid, stall and crashed roots required");
    StartCharacterWorker(std::filesystem::absolute(argv[2]));Await(WorkerState::Fault,10000);
    if(CharacterWorkerRequested())throw std::runtime_error("stall restarted implicitly");
    StartCharacterWorker(std::filesystem::absolute(argv[2]));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const auto start=std::chrono::steady_clock::now();StopCharacterWorker();
    if(std::chrono::steady_clock::now()-start>std::chrono::milliseconds(10))throw std::runtime_error("stall cancellation blocked caller");
    Await(WorkerState::Stopped,2000);
    StartCharacterWorker(std::filesystem::absolute(argv[3]));Await(WorkerState::Fault,3000);
    if(TryCharacterRuntimeView().captures)throw std::runtime_error("handshake launched GPU work");
    StartCharacterWorker(std::filesystem::absolute(argv[1]));Await(WorkerState::Ready,10000);
    StopCharacterWorker();Await(WorkerState::Stopped,2000);
    std::cout<<"PASS actual child permanent stall timeout, nonblocking cancellation, crash fault, explicit clean restart, zero captures\n";return 0;
}catch(const std::exception& e){StopCharacterWorker();std::cerr<<e.what()<<'\n';std::this_thread::sleep_for(std::chrono::milliseconds(150));return 1;}}
