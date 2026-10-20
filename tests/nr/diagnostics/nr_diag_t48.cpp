#include <nr/diagnostics/ContractObservation.h>

#include <algorithm>
#include <atomic>
#include <barrier>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace M0 = Neurotic::Diagnostics::M0;
namespace C = Neurotic::Contracts;

namespace
{
using Clock = std::chrono::steady_clock;
constexpr std::size_t WarmupBatches = 5;
constexpr std::size_t Samples = 41;
constexpr std::size_t Iterations = 128;
constexpr std::size_t Runs = 3;
constexpr std::size_t TotalOperations = (WarmupBatches + Samples) * Iterations;
constexpr std::size_t MeasurementQueueCapacity = 8192;
constexpr std::size_t ConcurrentQueueCapacity = 16384;
static_assert(TotalOperations < MeasurementQueueCapacity);
static_assert(2 * TotalOperations < ConcurrentQueueCapacity);
volatile std::uint64_t blackhole = 0;

struct AlwaysContendedGate
{
    bool TryEnter() noexcept { return false; }
    void Leave() noexcept {}
};

M0::SourceSnapshot Source()
{
    auto source = M0::SourceSnapshot::OwnerPublication(
        C::OwnerDomain::Presentation, "Benchmark.Presentation", "nr_diag_t48", 1, "PresentAttempt");
    source.Add("alpha.presentFlags", std::uint64_t {0});
    source.Add("alpha.present1", false);
    return source;
}

template<class Work>
std::vector<double> Measure(Work&& work)
{
    std::vector<double> samples;
    samples.reserve(Samples);
    for (std::size_t warmup = 0; warmup < WarmupBatches; ++warmup)
        work();
    for (std::size_t sample = 0; sample < Samples; ++sample)
    {
        const auto started = Clock::now();
        work();
        const auto ended = Clock::now();
        samples.push_back(std::chrono::duration<double, std::nano>(ended - started).count() / Iterations);
    }
    std::sort(samples.begin(), samples.end());
    return samples;
}

template<class Work>
std::vector<double> MeasureTwoProducers(Work&& work)
{
    constexpr std::size_t batches = WarmupBatches + Samples;
    std::barrier start(3);
    std::barrier finish(3);
    std::atomic<std::uint64_t> sink {0};
    auto worker = [&] {
        std::uint64_t local = 0;
        for (std::size_t batch = 0; batch < batches; ++batch)
        {
            start.arrive_and_wait();
            for (std::size_t i = 0; i < Iterations; ++i)
                local += work();
            finish.arrive_and_wait();
        }
        sink.fetch_add(local, std::memory_order_relaxed);
    };
    std::thread first(worker);
    std::thread second(worker);
    std::vector<double> samples;
    samples.reserve(Samples);
    for (std::size_t batch = 0; batch < batches; ++batch)
    {
        const auto started = Clock::now();
        start.arrive_and_wait();
        finish.arrive_and_wait();
        const auto ended = Clock::now();
        if (batch >= WarmupBatches)
            samples.push_back(std::chrono::duration<double, std::nano>(ended - started).count() /
                              (2.0 * Iterations));
    }
    first.join();
    second.join();
    blackhole += sink.load(std::memory_order_relaxed);
    std::sort(samples.begin(), samples.end());
    return samples;
}

double Percentile(const std::vector<double>& values, double p)
{
    const auto index = static_cast<std::size_t>(p * static_cast<double>(values.size() - 1));
    return values[index];
}

struct Result
{
    double p50 = 0, p95 = 0, p99 = 0, maximum = 0;
};

Result Summarize(const std::vector<double>& values)
{
    return {Percentile(values, 0.50), Percentile(values, 0.95), Percentile(values, 0.99), values.back()};
}

using RunResults = std::map<std::string, Result>;

void Add(RunResults& results, std::string name, std::vector<double> values)
{
    results.emplace(std::move(name), Summarize(values));
}

RunResults RunSuite()
{
    const auto source = Source();
    M0::Observer<1, 1000000> offObserver(M0::Mode::Off);
    auto acceptedObserver = std::make_unique<M0::Observer<MeasurementQueueCapacity, 1000000>>(M0::Mode::Shadow);
    auto ownerSnapshotObserver = std::make_unique<M0::Observer<MeasurementQueueCapacity, 1000000>>(M0::Mode::Shadow);
    M0::Observer<1, 1000000> fullObserver(M0::Mode::Shadow);
    assert(fullObserver.TryPublish("t48-prefill", &source) == M0::PublishResult::Accepted);
    M0::Observer<1, 1000000, AlwaysContendedGate> syntheticContended(M0::Mode::Shadow);
    auto consumerObserver = std::make_unique<M0::Observer<MeasurementQueueCapacity, 1000000>>(M0::Mode::Shadow);
    for (std::size_t i = 0; i < TotalOperations; ++i)
        assert(consumerObserver->TryPublish("t48-consumer-prefill", &source) == M0::PublishResult::Accepted);
    auto concurrentObserver = std::make_unique<M0::Observer<ConcurrentQueueCapacity, 1000000>>(M0::Mode::Shadow);

    RunResults results;
    Add(results, "baseline_a", Measure([&] {
        for (std::size_t i = 0; i < Iterations; ++i)
            blackhole = blackhole + i + 1;
    }));
    Add(results, "baseline_b", Measure([&] {
        for (std::size_t i = 0; i < Iterations; ++i)
            blackhole = blackhole + i + 1;
    }));
    Add(results, "off", Measure([&] {
        for (std::size_t i = 0; i < Iterations; ++i)
            blackhole += static_cast<std::uint64_t>(offObserver.TryPublish("t48-off", &source));
    }));
    Add(results, "shadow_accept_prebuilt_source", Measure([&] {
        for (std::size_t i = 0; i < Iterations; ++i)
            blackhole += static_cast<std::uint64_t>(acceptedObserver->TryPublish("t48-accepted", &source));
    }));
    Add(results, "shadow_build_owner_snapshot_and_accept", Measure([&] {
        for (std::size_t i = 0; i < Iterations; ++i)
            blackhole += static_cast<std::uint64_t>(ownerSnapshotObserver->ObserveLazy(
                "t48-owner-snapshot", []() noexcept { return Source(); }));
    }));
    Add(results, "shadow_queue_full", Measure([&] {
        for (std::size_t i = 0; i < Iterations; ++i)
            blackhole += static_cast<std::uint64_t>(fullObserver.TryPublish("t48-full", &source));
    }));
    Add(results, "shadow_synthetic_contended", Measure([&] {
        for (std::size_t i = 0; i < Iterations; ++i)
            blackhole += static_cast<std::uint64_t>(syntheticContended.TryPublish("t48-contended", &source));
    }));
    M0::EventClosure event;
    Add(results, "consumer_pop_and_build_event", Measure([&] {
        for (std::size_t i = 0; i < Iterations; ++i)
        {
            const bool popped = consumerObserver->TryPop(event);
            assert(popped);
            blackhole += event.event.producerSequence;
        }
    }));
    Add(results, "shadow_two_producer_contention", MeasureTwoProducers([&] {
        return static_cast<std::uint64_t>(concurrentObserver->TryPublish("t48-concurrent", &source));
    }));
    const auto contention = concurrentObserver->Snapshot();
    assert(contention.session.successfullyEnqueued + contention.drops.queueFull <= 2 * TotalOperations);
    return results;
}
} // namespace

int main()
{
    std::vector<RunResults> runs;
    runs.reserve(Runs);
    for (std::size_t run = 0; run < Runs; ++run)
        runs.push_back(RunSuite());

    std::cout << "record,run,case,samples,p50_ns,p95_ns,p99_ns,max_ns,run_p50_min_ns,run_p50_max_ns,run_p50_spread_ns\n";
    for (std::size_t run = 0; run < runs.size(); ++run)
        for (const auto& [name, result] : runs[run])
            std::cout << "sample," << (run + 1) << ',' << name << ',' << Samples << ',' << std::fixed
                      << std::setprecision(3) << result.p50 << ',' << result.p95 << ',' << result.p99 << ','
                      << result.maximum << ",,,\n";

    for (const auto& [name, ignored] : runs.front())
    {
        (void) ignored;
        double minimum = runs.front().at(name).p50;
        double maximum = minimum;
        for (const auto& run : runs)
        {
            minimum = std::min(minimum, run.at(name).p50);
            maximum = std::max(maximum, run.at(name).p50);
        }
        std::cout << "run_variance,all," << name << ',' << Runs << ",,,,," << minimum << ',' << maximum << ','
                  << (maximum - minimum) << '\n';
    }

    std::cout << "memory,all,sizeof_ObservationDraft,1,,,," << sizeof(M0::ObservationDraft) << ",,,\n";
    std::cout << "memory,all,sizeof_EventClosure,1,,,," << sizeof(M0::EventClosure) << ",,,\n";
    std::cout << "memory,all,sizeof_GlobalObserver,1,,,," << sizeof(M0::GlobalObserver) << ",,,\n";
    std::cout << "T48 measurement only: no threshold, PASS, or supported-runtime claim is issued by this program.\n";
}
