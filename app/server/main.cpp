// pacemaker_server: engine + simulated drummer + outputs + web UI (RS-07).
#include "Host.h"
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

static std::atomic<bool> g_quit { false };
static void onSignal(int) { g_quit = true; }

int main(int argc, char** argv)
{
    pacemaker::HostOptions o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](const char* name) -> const char* {
            if (i + 1 >= argc) { std::fprintf(stderr, "missing value for %s\n", name); std::exit(2); }
            return argv[++i];
        };
        if (a == "--port") o.port = std::atoi(next("--port"));
        else if (a == "--bind") o.bind = next("--bind");
        else if (a == "--config") o.configPath = next("--config");
        else if (a == "--ui-dir") o.uiDir = next("--ui-dir");
        else if (a == "--speed") o.speed = std::atof(next("--speed"));
        else if (a == "--no-autoplay") o.autoPlay = false;
        else if (a == "--help" || a == "-h") {
            std::printf("pacemaker_server [--port 8080] [--bind 127.0.0.1] [--config pacemaker.json] [--ui-dir DIR] [--speed 1] [--no-autoplay]\n"
                        "Open http://<bind>:<port>/ in a browser.\n");
            return 0;
        } else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
    }
    std::signal(SIGINT, onSignal); std::signal(SIGTERM, onSignal);
    pacemaker::Host host(o);
    std::string err;
    if (!host.start(&err)) { std::fprintf(stderr, "cannot start: %s\n", err.c_str()); return 1; }
    std::printf("Pacemaker listening on http://%s:%d/\n", o.bind.c_str(), host.port());
    std::fflush(stdout);
    while (!g_quit.load()) std::this_thread::sleep_for(std::chrono::milliseconds(200));
    host.stop();
    return 0;
}
