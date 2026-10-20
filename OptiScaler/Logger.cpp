#include "pch.h"
#include "Logger.h"
#include "Config.h"
#include <atomic>
#include <iostream>
#include <mutex>
#include <unordered_map>

#include "spdlog/async.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/sinks/callback_sink.h"
#include <include/spdlog_sink/debug_sink.h>

#include "Util.h"

namespace
{
std::mutex loggerConfigurationMutex;
std::mutex ngxLoggingMutex;
thread_local bool inNgxLoggingCallback = false;

// The default logger remains fixed after startup. Each call borrows an immutable
// backend; queued async messages retain their original logger and sinks through
// completion instead of losing them when a destination changes.
class LoggerRouter final : public spdlog::sinks::sink
{
    struct Backend
    {
        std::shared_ptr<spdlog::logger> logger;
        spdlog::sink_ptr sdk;
    };
    // Destroy the backend before draining/destroying its queue at CRT teardown.
    std::shared_ptr<spdlog::details::thread_pool> queue_;
    std::atomic<std::shared_ptr<const Backend>> backend_;
    int queueThreads_ = 0;
  public:
    void Publish(std::shared_ptr<spdlog::logger> logger, spdlog::sink_ptr sdk)
    {
        backend_.store(logger ? std::make_shared<const Backend>(Backend{std::move(logger), std::move(sdk)}) : nullptr);
    }
    auto Queue(int threads)
    {
        if (!queue_)
        {
            queue_ = std::make_shared<spdlog::details::thread_pool>(8192, threads);
            queueThreads_ = threads;
        }
        return queue_;
    }
    int QueueThreads() const noexcept { return queueThreads_; }
    void log(const spdlog::details::log_msg& message) override
    {
        if (const auto backend = backend_.load())
        {
            if (inNgxLoggingCallback)
            {
                // A deferred callback cannot enqueue onto its own full queue.
                // Keep nested records on the other synchronized sinks, without
                // reinjecting them into SDK logging after the TLS scope ends.
                for (const auto& sink : backend->logger->sinks())
                    if (sink != backend->sdk && sink->should_log(message.level))
                    { sink->log(message); sink->flush(); }
                return;
            }
            backend->logger->log(message.time, message.source, message.level, message.payload);
        }
    }
    void flush() override
    {
        if (const auto backend = backend_.load())
        {
            if (inNgxLoggingCallback)
            {
                for (const auto& sink : backend->logger->sinks()) if (sink != backend->sdk) sink->flush();
                return;
            }
            backend->logger->flush();
        }
    }
    void set_pattern(const std::string& pattern) override
    { if (const auto backend = backend_.load()) backend->logger->set_pattern(pattern); }
    void set_formatter(std::unique_ptr<spdlog::formatter> formatter) override
    { if (const auto backend = backend_.load()) backend->logger->set_formatter(std::move(formatter)); }
};

struct LoggerRuntime
{
    std::shared_ptr<LoggerRouter> router = std::make_shared<LoggerRouter>();
    std::shared_ptr<spdlog::logger> front = std::make_shared<spdlog::logger>("neurotic", router);
    std::unordered_map<std::wstring, std::weak_ptr<spdlog::sinks::basic_file_sink_mt>> files;
    spdlog::sink_ptr debug, console;
    bool published = false;
    std::uint64_t generation = 0;
    ULONGLONG processCreated = [] {
        FILETIME created{}, exited{}, kernel{}, user{};
        if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return ULONGLONG{0};
        return (static_cast<ULONGLONG>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
    }();
};
LoggerRuntime& Runtime() { static LoggerRuntime runtime; return runtime; }

void FormatSink(const spdlog::sink_ptr& sink)
{
    sink->set_level(spdlog::level::trace);
#ifdef LOG_ASYNC
    sink->set_pattern("%H:%M:%S.%f\t%L\t%v");
#else
    sink->set_pattern("[%H:%M:%S.%f] [%L] %v");
#endif
}

void ForwardNgxLog(const spdlog::details::log_msg& message)
{
    // Check recursion before locking. SDK callbacks may themselves log; their
    // nested messages still reach other sinks without reentering this callback.
    if (inNgxLoggingCallback || loggerClosed.load(std::memory_order_relaxed)) return;
    struct Scope
    {
        Scope() { inNgxLoggingCallback = true; }
        ~Scope() { inNgxLoggingCallback = false; }
    } scope;
    std::lock_guard lock(ngxLoggingMutex);
    if (loggerClosed.load(std::memory_order_relaxed)) return;
    const auto& logging = State::Instance().NVNGX_Logger;
    if (logging.LoggingCallback && logging.MinimumLoggingLevel != NVSDK_NGX_LOGGING_LEVEL_OFF &&
        (logging.MinimumLoggingLevel == NVSDK_NGX_LOGGING_LEVEL_VERBOSE || message.level >= spdlog::level::info))
    {
        // log_msg payload is a bounded view, not necessarily NUL terminated.
        std::string payload(message.payload.data(), message.payload.size());
        logging.LoggingCallback(payload.data(), NVSDK_NGX_LOGGING_LEVEL_ON, NVSDK_NGX_Feature_SuperSampling);
    }
}
}

static bool InitializeConsole()
{
    // Allocate a console for this app
    if (!AllocConsole())
        return false;

    FILE* pFile;

    // Redirect STDIN if the console has an input handle
    if (GetStdHandle(STD_INPUT_HANDLE) != INVALID_HANDLE_VALUE)
    {
        if (freopen_s(&pFile, "CONIN$", "r", stdin) != 0)
            return false;
    }

    // Redirect STDOUT if the console has an output handle
    if (GetStdHandle(STD_OUTPUT_HANDLE) != INVALID_HANDLE_VALUE)
    {
        if (freopen_s(&pFile, "CONOUT$", "w", stdout) != 0)
            return false;
    }

    // Redirect STDERR if the console has an error handle
    if (GetStdHandle(STD_ERROR_HANDLE) != INVALID_HANDLE_VALUE)
    {
        if (freopen_s(&pFile, "CONOUT$", "w", stderr) != 0)
            return false;
    }

    // Clear the error state for each of the C++ standard streams
    std::cin.clear();
    std::cout.clear();
    std::cerr.clear();
    std::wcin.clear();
    std::wcout.clear();
    std::wcerr.clear();

    // Make C++ standard streams point to console as well.
    std::ios::sync_with_stdio();

    WaitForEnter();

    return true;
}

void WaitForEnter()
{
    if (Config::Instance()->DebugWait.value_or_default())
    {
        std::cout << "Press ENTER to continue..." << std::endl;
        std::cin.get();
    }
}

void PrepareLogger()
{
    if (loggerClosed.load(std::memory_order_relaxed)) return;
    std::shared_ptr<spdlog::logger> configuredLogger;
    int requestedLevel = 0, effectiveLevel = 6, requestedThreads = 0, effectiveThreads = 0;
    bool asynchronous = false, anyDestination = false;
    std::uint64_t generation = 0;
    ULONGLONG processCreated = 0;
    // Keep this owner through a failed attempt's catch. Reacquiring there
    // would let an older failure disable a newer successful configuration.
    std::unique_lock configurationLock(loggerConfigurationMutex, std::defer_lock);
    try
    {
        {
            configurationLock.lock();
            if (loggerClosed.load(std::memory_order_relaxed)) return;
            auto& runtime = Runtime();
            const auto* config = Config::Instance();
            requestedLevel = config->LogLevel.value_or_default();
            asynchronous = config->LogAsync.value_or_default();
            requestedThreads = config->LogAsyncThreads.value_or_default();
            anyDestination = config->LogToConsole.value_or_default() || config->LogToFile.value_or_default() ||
                             config->LogToNGX.value_or_default() || config->LogToDebug.value_or_default();
            std::shared_ptr<spdlog::logger> backend;
            spdlog::sink_ptr sdkSink;
            if (anyDestination)
            {
                if (config->OpenConsole.value_or_default()) InitializeConsole();
                std::vector<spdlog::sink_ptr> sinks;
                if (config->LogToDebug.value_or_default())
                {
                    if (!runtime.debug)
                    {
                        runtime.debug = std::make_shared<spdlog::sinks::debug_sink_mt>();
                        FormatSink(runtime.debug);
                    }
                    sinks.push_back(runtime.debug);
                }
                if (config->LogToConsole.value_or_default())
                {
                    if (!runtime.console)
                    {
                        runtime.console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
                        runtime.console->set_level(spdlog::level::info);
                        runtime.console->set_pattern("[%H:%M:%S.%f] [%L] %v");
                    }
                    sinks.push_back(runtime.console);
                }
                // Weak entries never retain historical files. Async in-flight
                // backends may still own a sink; reuse its synchronized handle.
                std::erase_if(runtime.files, [](const auto& item) { return item.second.expired(); });
                if (config->LogToFile.value_or_default())
                {
                    const auto filename = config->LogFileName.value_or_default();
                    auto file = runtime.files[filename].lock();
                    if (!file)
                    {
                        file = std::make_shared<spdlog::sinks::basic_file_sink_mt>(filename, false);
                        FormatSink(file);
                        runtime.files[filename] = file;
                    }
                    sinks.push_back(std::move(file));
                }
                if (config->LogToNGX.value_or_default())
                {
                    // The callback supplies its own process-wide serialization
                    // and recursion guard, outside the base sink's lock.
                    auto callback = std::make_shared<spdlog::sinks::callback_sink_st>(ForwardNgxLog);
                    callback->set_level(spdlog::level::trace);
                    sdkSink = callback;
                    sinks.push_back(std::move(callback));
                }
                if (asynchronous)
                {
                    // spdlog's actual queue contract; no machine-specific cap.
                    if (requestedThreads < 1 || requestedThreads > 1000)
                        throw spdlog::spdlog_ex("LogAsyncThreads must be between 1 and 1000");
                    backend = std::make_shared<spdlog::async_logger>("neurotic-backend", sinks.begin(), sinks.end(),
                        runtime.router->Queue(requestedThreads), spdlog::async_overflow_policy::block);
                    effectiveThreads = runtime.router->QueueThreads();
                }
                else backend = std::make_shared<spdlog::logger>("neurotic-backend", sinks.begin(), sinks.end());
                backend->set_level(spdlog::level::trace);
                backend->flush_on(spdlog::level::trace);
            }
            if (loggerClosed.load(std::memory_order_relaxed)) return;
            runtime.router->Publish(std::move(backend), std::move(sdkSink));
            const auto level = anyDestination ? Neurotic::Logging::RuntimeLevel(requestedLevel) : spdlog::level::off;
            effectiveLevel = static_cast<int>(level);
            runtime.front->set_level(level);
            // The backend flushes accepted records; avoid double async flushes.
            runtime.front->flush_on(spdlog::level::off);
            if (!runtime.published)
            {
                // First configuration belongs to process startup. Later calls
                // never replace the raw default pointer used by free functions.
                spdlog::set_default_logger(runtime.front);
                runtime.published = true;
            }
            generation = ++runtime.generation;
            processCreated = runtime.processCreated;
            configuredLogger = runtime.front;
        }
        configurationLock.unlock();
        // Sink/SDK callbacks must not run under loggerConfigurationMutex.
        configuredLogger->info("NeuRotic log session: pid={} processCreatedUtc100ns={} configuration={} "
                               "requestedLevel={} effectiveLevel={} async={} requestedThreads={} effectiveThreads={}",
                               GetCurrentProcessId(), processCreated, generation, requestedLevel,
                               effectiveLevel, asynchronous, requestedThreads, effectiveThreads);
        if (!Neurotic::Logging::SupportedLevel(requestedLevel))
            configuredLogger->warn("Unknown saved LogLevel {}; using Trace (0) without changing the saved preference", requestedLevel);
        if (asynchronous && anyDestination && requestedThreads != effectiveThreads)
            configuredLogger->warn("Logging uses {} async threads; restart to apply the requested {} threads", effectiveThreads, requestedThreads);
    }
    catch (const std::exception& error)
    {
        // Failed configuration must not leave a prior file destination accepting
        // new records after an explicit opt-out. Preserve its already accepted
        // records and keep terminal Close semantics; a later valid edit can retry.
        if (configurationLock.owns_lock())
        {
            if (auto logger = spdlog::default_logger()) logger->set_level(spdlog::level::off);
            configurationLock.unlock();
            std::cerr << "Unable to configure logging; new output is disabled: " << error.what() << std::endl;
        }
        else
        {
            // A post-commit diagnostic failure cannot disable a newer setup.
            std::cerr << "Unable to record logging configuration: " << error.what() << std::endl;
        }
    }
}

void CloseLogger()
{
    if (loggerClosed.exchange(true))
        return;

    std::shared_ptr<spdlog::logger> logger;
    {
        std::lock_guard lock(loggerConfigurationMutex);
        logger = spdlog::default_logger();
        // Driver callbacks can still log after DLL_PROCESS_DETACH starts.
        // spdlog's free functions use the raw default pointer, so shutdown()
        // here would invalidate both LOG_* and direct spdlog callers. Keep the
        // logger/registry alive until CRT teardown, and atomically stop accepting
        // output without replacing the default pointer under concurrent callers.
        if (logger) logger->set_level(spdlog::level::off);
    }
    // A full async queue may be drained by a callback awaiting configuration.
    // Retain the logger, but never hold that mutex while posting its flush.
    if (logger) logger->flush();
}
