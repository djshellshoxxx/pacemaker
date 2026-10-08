// End-to-end tests of the web host (W5) and the live simulated drummer.
#include "TestFramework.h"
#include "Host.h"
#include <arpa/inet.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

using namespace pacemaker;

namespace {
struct Reply { int status = 0; std::string body; };

Reply request(int port, const std::string& method, const std::string& path, const std::string& body = {}, int timeoutMs = 4000)
{
    Reply r;
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a {}; a.sin_family = AF_INET; a.sin_port = htons((uint16_t) port); inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
    timeval tv { timeoutMs / 1000, (timeoutMs % 1000) * 1000 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    if (::connect(fd, (sockaddr*) &a, sizeof a) != 0) { ::close(fd); return r; }
    const std::string req = method + " " + path + " HTTP/1.1\r\nHost: x\r\nContent-Type: application/json\r\nContent-Length: " +
                            std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
    ::send(fd, req.data(), req.size(), MSG_NOSIGNAL);
    std::string all; char buf[8192]; ssize_t n;
    while ((n = ::recv(fd, buf, sizeof buf, 0)) > 0) all.append(buf, (size_t) n);
    ::close(fd);
    if (all.rfind("HTTP/1.1 ", 0) == 0) r.status = std::atoi(all.c_str() + 9);
    const size_t he = all.find("\r\n\r\n");
    if (he != std::string::npos) r.body = all.substr(he + 4);
    return r;
}

Json getJson(int port, const std::string& path)
{
    Json j; Json::parse(request(port, "GET", path).body, j); return j;
}

HostOptions testOptions(double speed)
{
    HostOptions o; o.port = 0; o.speed = speed; o.configPath = "pacemaker-test-config.json"; return o;
}
}

TEST_CASE(W5_HttpServesUiStateAndActions)
{
    std::remove("pacemaker-test-config.json");
    Host host(testOptions(4.0));
    std::string err;
    REQUIRE_MSG(host.start(&err), err);
    const int port = host.port();
    const Reply page = request(port, "GET", "/");
    CHECK(page.status == 200 && page.body.find("<title>Pacemaker</title>") != std::string::npos);
    CHECK(page.body.find("src=\"http") == std::string::npos && page.body.find("href=\"http") == std::string::npos
          && page.body.find("@import") == std::string::npos);   // no external requests
    const Json st = getJson(port, "/api/state");
    CHECK(st.has("state") && st.has("settings") && st.get("settings").get("stateVersion").asInt() == 1);
    CHECK(request(port, "POST", "/api/action", "{\"name\":\"relock\"}").status == 200);
    CHECK(request(port, "POST", "/api/action", "{\"name\":\"bogus\"}").status == 400);
    CHECK(request(port, "POST", "/api/action", "{not json").status == 400);
    CHECK(request(port, "GET", "/nope").status == 404);
    CHECK(request(port, "POST", "/api/settings", "{\"engine\":{\"meterNum\":\"x\"}}").status == 400);
    CHECK(request(port, "POST", "/api/settings", "{\"engine\":{\"referenceBpm\":110},\"ui\":{\"theme\":\"light\"}}").status == 200);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    CHECK(getJson(port, "/api/state").get("settings").get("engine").get("referenceBpm").asNumber() == 110.0);
    const std::string huge(300 * 1024, 'x');
    CHECK(request(port, "POST", "/api/settings", huge).status == 413);
    CHECK(request(port, "POST", "/api/songs", "{\"csv\":\"name,bpm\\nA,100\\nB,140\"}").status == 200);
    CHECK(request(port, "POST", "/api/song", "{\"name\":\"B\"}").status == 200);
    CHECK(request(port, "POST", "/api/song", "{\"index\":9}").status == 400);
    const Reply rep = request(port, "GET", "/api/report");
    CHECK(rep.status == 200);
    CHECK(request(port, "GET", "/api/tempomap.mid").body.compare(0, 4, "MThd") == 0);
    host.stop();
    std::remove("pacemaker-test-config.json");
}

TEST_CASE(HostLocksOntoSimulatedDrummer)
{
    std::remove("pacemaker-test-config.json");
    Host host(testOptions(10.0));   // 10x faster than real time: 50 s of music in 5 s
    std::string err;
    REQUIRE_MSG(host.start(&err), err);
    host.drummer().drift = 0.0; host.drummer().jitterMs = 4.0; host.drummer().targetBpm = 112.0;
    std::this_thread::sleep_for(std::chrono::milliseconds(5500));
    const Json st = getJson(host.port(), "/api/state");
    std::printf("  state=%s bpm=%.2f conf=%.2f t=%.1f\n", st.get("state").asString().c_str(), st.get("bpm").asNumber(),
                st.get("confidence").asNumber(), st.get("t").asNumber());
    CHECK(st.get("t").asNumber() > 30.0);
    CHECK(st.get("state").asString() == "LOCKED");
    CHECK(std::fabs(st.get("bpm").asNumber() - 112.0) < 2.0);
    CHECK(st.get("trace").get("v").items().size() > 100);
    CHECK(st.get("beats").items().size() > 10);
    CHECK(st.get("grid").has("period"));
    // The drift log filled up and the report summarises it.
    const Json rep = getJson(host.port(), "/api/report");
    REQUIRE(rep.get("songs").items().size() >= 1);
    CHECK(rep.get("rows").asNumber() > 20);
    // Calibration endpoint measures the simulated interface to the sample.
    Json cal; Json::parse(request(host.port(), "POST", "/api/calibrate", "{\"mode\":\"loopback\"}").body, cal);
    CHECK(cal.get("result").get("ok").asBool());
    CHECK(std::fabs(cal.get("result").get("hidden").asNumber() - 37.0) <= 1.0);
    host.stop();
    std::remove("pacemaker-test-config.json");
}

TEST_CASE(HostPersistsSettingsAtomically)
{
    std::remove("pacemaker-test-config.json");
    {
        Host host(testOptions(4.0));
        std::string err; REQUIRE_MSG(host.start(&err), err);
        CHECK(request(host.port(), "POST", "/api/settings", "{\"outputs\":{\"osc\":{\"host\":\"10.1.2.3\",\"port\":7000}}}").status == 200);
        host.stop();   // flushes pending writes
    }
    {
        Host host(testOptions(4.0));
        std::string err; REQUIRE_MSG(host.start(&err), err);
        const Json st = getJson(host.port(), "/api/state");
        CHECK(st.get("settings").get("outputs").get("osc").get("host").asString() == "10.1.2.3");
        CHECK(st.get("settings").get("outputs").get("osc").get("port").asInt() == 7000);
        host.stop();
    }
    std::remove("pacemaker-test-config.json");
}
