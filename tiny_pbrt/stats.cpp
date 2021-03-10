//
// Created by 秦浩晨 on 2020/10/15.
//

#include "stats.h"
#include <signal.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cinttypes>
#include <functional>
#include <mutex>
#include <type_traits>
#include "parallel.h"
#include "stringprint.h"
#ifdef PBRT_HAVE_ITIMER
#include <sys/time.h>
#endif  // PBRT_HAVE_ITIMER

namespace pbrt {

// Local variables
std::vector<std::function<void(StatsAccumulator &)>> *StatRegisterer::funcs;
static StatsAccumulator statsAccumulator;

// For a given profiler state (a set of bits corresponding to profiling
// categories that are active), ProfileSample stores a count of the number
// of times that state has been active when the timer interrupt to record
// a profiling sample has fired.
struct ProfileSample {
    std::atomic<uint64_t> profilerState{0};
    std::atomic<uint64_t> count{0};
};

// We use a hash table to keep track of the profiler state counts. Because
// we can't do dynamic memory allocation in a signal handler (and because
// the counts are updated in a signal handler), we can't easily use
// std::unordered_map. We therefore allocate a fixed size hash table and
// use linear probing if there's a conflict
static const int profileHashSize = 256;
static std::array<ProfileSample, profileHashSize> profileSamples;
static std::chrono::system_clock::time_point profileStartTime;

#ifdef PBRT_HAVE_ITIMER
    static void ReportProfileSample(int, siginfo_t *, void *);
#endif  // PBRT_HAVE_ITIMER

void ReportThreadStats() {
    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);
    StatRegisterer::CallCallbacks(statsAccumulator);
}

void StatRegisterer::CallCallbacks(StatsAccumulator &accum) {
    for (auto func : *funcs) func(accum);
}

void PrintStats(FILE *dest) { statsAccumulator.Print(dest); }

void ClearStats() { statsAccumulator.Clear(); }

static void getCategoryAndTitle(const std::string &str, std::string *category,
        std::string *title) {
    const char *s = str.c_str();
    const char *slash = strchr(s, '/');
    if (!slash)
        *title = str;
    else {
        *category = std::string(s, slash - s);
        *title = std::string(slash + 1);
    }
}

void StatsAccumulator::Print(FILE *dest) {
    fprintf(dest, "Statistics:\n");
    std::map<std::string, std::vector<std::string>> toPrint;

    for (auto &counter : counters) {
        if (counter.second == 0) continue;
        std::string category, title;
        getCategoryAndTitle(counter.first, &category, &title);
        toPrint[category].push_back(StringPrintf(
                "%-42s               %12" PRIu64, title.c_str(), counter.second));
    }

    for (auto &counter : memoryCounters) {
        if (counter.second == 0) continue;
        std::string category, title;
        getCategoryAndTitle(counter.first, &category, &title);
        double kb = (double)counter.second / 1024.;
        if (kb < 1024.)
            toPrint[category].push_back(StringPrintf(
                    "%-42s                  %9.2f kB", title.c_str(), kb));
        else {
            float mib = kb / 1024.;
            if (mib < 1024.)
                toPrint[category].push_back(StringPrintf(
                        "%-42s                  %9.2f MiB", title.c_str(), mib));
            else {
                float gib = mib / 1024.;
                toPrint[category].push_back(StringPrintf(
                        "%-42s                  %9.2f GiB", title.c_str(), gib));
            }
        }
    }

    for (auto &distributionSum : intDistributionSums) {
        const std::string &name = distributionSum.first;
        if (intDistributionCounts[name] == 0) continue;
        std::string category, title;
        getCategoryAndTitle(name, &category, &title);
        double avg = (double)distributionSum.second /
                     (double)intDistributionCounts[name];
        toPrint[category].push_back(
                StringPrintf("%-42s                      %.3f avg [range %" PRIu64
                             " - %" PRIu64 "]",
                             title.c_str(), avg, intDistributionMins[name],
                             intDistributionMaxs[name]));
    }

    for (auto &distributionSum : floatDistributionSums) {
        const std::string &name = distributionSum.first;
        if (floatDistributionCounts[name] == 0) continue;
        std::string category, title;
        getCategoryAndTitle(name, &category, &title);
        double avg = (double)distributionSum.second /
                     (double)floatDistributionCounts[name];
        toPrint[category].push_back(
                StringPrintf("%-42s                      %.3f avg [range %f - %f]",
                             title.c_str(), avg, floatDistributionMins[name],
                             floatDistributionMaxs[name]));
    }
    for (auto &percentage : percentages) {
        if (percentage.second.second == 0) continue;
        int64_t num = percentage.second.first;
        int64_t denom = percentage.second.second;
        std::string category, title;
        getCategoryAndTitle(percentage.first, &category, &title);
        toPrint[category].push_back(
                StringPrintf("%-42s%12" PRIu64 " / %12" PRIu64 " (%.2f%%)",
                             title.c_str(), num, denom, (100.f * num) / denom));
    }
    for (auto &ratio : ratios) {
        if (ratio.second.second == 0) continue;
        int64_t num = ratio.second.first;
        int64_t denom = ratio.second.second;
        std::string category, title;
        getCategoryAndTitle(ratio.first, &category, &title);
        toPrint[category].push_back(StringPrintf(
                "%-42s%12" PRIu64 " / %12" PRIu64 " (%.2fx)", title.c_str(), num,
                denom, (double)num / (double)denom));
    }

    for (auto &categories : toPrint) {
        fprintf(dest, "  %s\n", categories.first.c_str());
        for (auto &item : categories.second)
            fprintf(dest, "    %s\n", item.c_str());
    }
}

// Also, remember to clear the temp data at appropriate time
void StatsAccumulator::Clear() {
    counters.clear();
    memoryCounters.clear();
    intDistributionSums.clear();
    intDistributionCounts.clear();
    intDistributionMins.clear();
    intDistributionMaxs.clear();
    floatDistributionSums.clear();
    floatDistributionCounts.clear();
    floatDistributionMins.clear();
    floatDistributionMaxs.clear();
    percentages.clear();
    ratios.clear();
}

PBRT_THREAD_LOCAL uint64_t ProfilerState;
static std::atomic<bool> profilerRunning{false};

void InitProfiler() {
    CHECK(!profilerRunning);

    // Access the per-thread ProfilerState variable now, so that there's no
    // risk of its first access being in the signal handler (which in turn
    // would cause dynamic memory allocation, which is illegal in a signal
    // handler).
    ProfilerState = ProfToBits(Prof::SceneConstruction);

    CleanupProfiler();

    profileStartTime = std::chrono::system_clock::now();
// Set timer to periodically interrupt the system for profiling
#ifdef PBRT_HAVE_ITIMER
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = ReportProfileSample;
    sa.sa_flags = SA_RESTART | SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGPROF, &sa, NULL);

    static struct itimerval timer;
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = 1000000 / 100;  // 100 Hz sampling
    timer.it_value = timer.it_interval;

    CHECK_EQ(setitimer(ITIMER_PROF, &timer, NULL), 0)
        << "Timer could not be initialized: " << strerror(errno);
#endif
    profilerRunning = true;

}

static std::atomic<int> profilerSuspendCount{0};

void SuspendProfiler() { ++profilerSuspendCount; }

void ResumeProfiler() { CHECK_GE(--profilerSuspendCount, 0); }

void ProfilerWorkerThreadInit() {
#ifdef PBRT_HAVE_ITIMER
    // The per-thread initialization in the worker threads has to happen
// *before* the profiling signal handler is installed.
CHECK(!profilerRunning || profilerSuspendCount > 0);

// ProfilerState is a thread-local variable that is accessed in the
// profiler signal handler. It's important to access it here, which
// causes the dynamic memory allocation for the thread-local storage to
// happen now, rather than in the signal handler, where this isn't
// allowed.
ProfilerState = ProfToBits(Prof::SceneConstruction);
#endif  // PBRT_HAVE_ITIMER
}

void ClearProfiler() {
    for (ProfileSample &ps : profileSamples) {
        ps.profilerState = 0;
        ps.count = 0;
    }
}



}

