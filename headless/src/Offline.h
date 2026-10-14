// Non-device modes of pacemaker_headless: --wav-in, --selftest, --bench.
#pragma once
#include "Config.h"
#include <atomic>
#include <string>

namespace pmhl {

extern std::atomic<bool> g_stopRequested;      // set by SIGTERM / SIGINT

struct WavOptions {
    std::string wav;
    bool realtime = false;          // pace to the wall clock and drive the real outputs
    double lingerSec = 0;           // keep the web UI up this long after the file ends (negative = until SIGTERM)
    std::string outDir = ".";       // drift log, tempo map and report are written here
    int webPort = -1;               // <0: use the config (when web.enabled); 0 = off
    int block = 256;
};

int runWavIn(const HeadlessConfig& cfg, const WavOptions& opt);
int runSelftest();
int runBench(double seconds, int block);

} // namespace pmhl
