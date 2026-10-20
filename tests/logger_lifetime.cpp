#include "Logger.h"
#include "Config.h"
#include <spdlog/async.h>
#include <spdlog/sinks/base_sink.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <thread>

// PrepareLogger is linked from the production translation unit but is outside
// this shutdown regression. Any accidental attempt to initialize product config
// fails immediately instead of executing provider discovery through Config.
Config* Config::Instance() { std::abort(); }

// This object registers its destructor before main first constructs spdlog's
// registry. It therefore logs AFTER the registry destructor during process exit.
struct LateStaticCallback
{
    ~LateStaticCallback()
    {
        int evaluated = 0;
        LOG_ERROR("static teardown callback {}", ++evaluated);
        if (evaluated != 0)
            std::abort();
        std::puts("PASS: static callback after registry destruction");
    }
} lateStaticCallback;

// Runs the production CloseLogger and the real spdlog implementation, with an
// in-memory destination only. No provider, graphics device or Config is started.
class CountingSink : public spdlog::sinks::base_sink<std::mutex>
{
  public:
    std::atomic<unsigned> messages { 0 };
    std::atomic<unsigned> flushes { 0 };
    std::condition_variable flushed;
    std::mutex waitMutex;

  protected:
    void sink_it_(const spdlog::details::log_msg&) override { ++messages; }
    void flush_() override
    {
        std::lock_guard lock(waitMutex);
        ++flushes;
        flushed.notify_all();
    }
};

static int Fail(const char* message)
{
    std::cerr << "FAIL: " << message << '\n';
    return 1;
}

int main(int argc, char** argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    const std::string mode = argc > 1 ? argv[1] : "sync";
    auto sink = std::make_shared<CountingSink>();
    std::shared_ptr<spdlog::logger> logger;
    if (mode == "async")
    {
        spdlog::init_thread_pool(8192, 1);
        logger = std::make_shared<spdlog::async_logger>("lifetime", sink, spdlog::thread_pool());
    }
    else
        logger = std::make_shared<spdlog::logger>("lifetime", sink);
    logger->set_level(spdlog::level::trace);
    spdlog::set_default_logger(logger);
    LOG_ERROR("accepted failure before close: {}", 7);

    std::atomic<bool> start { false };
    std::vector<std::thread> callers;
    if (mode == "concurrent")
    {
        for (int i = 0; i < 8; ++i)
            callers.emplace_back([&] {
                while (!start.load())
                    std::this_thread::yield();
                for (int j = 0; j < 10000; ++j)
                {
                    LOG_ERROR("concurrent teardown failure {}", j);
                    spdlog::warn("direct concurrent caller {}", j);
                    if (j == 100)
                        CloseLogger();
                }
            });
    }
    start = true;
    CloseLogger();
    for (auto& caller : callers)
        caller.join();

    if (!spdlog::default_logger())
        return Fail("CloseLogger invalidated the default logger needed by late callbacks");
    // Exercise the same LOG_ERROR route as IdentifyGpu::checkGpuInfo, plus direct
    // callers that would remain unsafe if only LOG_* acquired a guarded pointer.
    LOG_TRACE("late trace");
    LOG_DEBUG("late debug");
    LOG_INFO("late info");
    LOG_WARN("late warn");
    LOG_ERROR("Failed to create DXGI Factory, GPU info will be inaccurate!");
    LOG_FUNC();
    LOG_FUNC_RESULT(E_FAIL);
    spdlog::error("late direct error");
    spdlog::critical("late direct critical");
    CloseLogger();
    CloseLogger();

    {
        std::unique_lock lock(sink->waitMutex);
        if (!sink->flushed.wait_for(lock, std::chrono::seconds(5), [&] { return sink->flushes.load() > 0; }))
            return Fail("accepted messages were not flushed");
    }
    if (sink->flushes != 1)
        return Fail("repeated/concurrent close flushed more than once");
    if (mode != "concurrent" && sink->messages != 1)
        return Fail("late logging reached the destination after close");
    const auto count = sink->messages.load();
    LOG_ERROR("after all concurrent callers retired");
    if (sink->messages != count)
        return Fail("closed logger resumed output");
    int lateArgumentEvaluations = 0;
    LOG_ERROR("late callback argument {}", ++lateArgumentEvaluations);
    if (lateArgumentEvaluations != 0)
        return Fail("closed logging evaluated a potentially torn-down callback argument");
    PrepareLogger(); // Must not reach the aborting Config stub after terminal close.
    spdlog::shutdown(); // Model registry resource destruction after DLL detach.
    LOG_ERROR("late callback after registry destruction");
    std::cout << "PASS: " << mode << " close, late macro/direct logging, flush and repeated close\n";
}
