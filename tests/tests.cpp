// Unit tests for the loudness meter and leveling controller (no audio device needed).
#include <cmath>
#include <cstdio>
#include <vector>

#include "loudness.h"

static int g_failures = 0;
#define CHECK(cond, ...)                                    \
    do {                                                    \
        if (cond) {                                         \
            std::printf("  ok    ");                        \
        } else {                                            \
            std::printf("  FAIL  ");                        \
            ++g_failures;                                   \
        }                                                   \
        std::printf(__VA_ARGS__);                           \
        std::printf("\n");                                  \
    } while (0)

static double SineLoudness(double freq, double amp, bool bothChannels) {
    const double fs = 48000;
    KWeighting kw(fs, 2);
    std::vector<float> buf(48000 * 2);
    double ss = 0;
    for (int pass = 0; pass < 2; ++pass) {  // 1 s to settle the filters, then 1 s measured
        for (int i = 0; i < 48000; ++i) {
            const double t = (pass * 48000 + i) / fs;
            const float v = (float)(amp * std::sin(2 * kPi * freq * t));
            buf[i * 2] = v;
            buf[i * 2 + 1] = bothChannels ? v : 0.0f;
        }
        ss = kw.Process(buf.data(), 48000);
    }
    return PowerToLufs(ss / 48000);
}

static void TestKWeighting() {
    std::printf("K-weighting (ITU-R BS.1770)\n");
    double l = SineLoudness(997, 1.0, false);
    CHECK(std::fabs(l - (-3.01)) < 0.1, "997 Hz 0 dBFS in one channel = %.2f LUFS (expect -3.01)", l);
    double l2 = SineLoudness(997, 0.1, true);
    CHECK(std::fabs(l2 - (-20.0)) < 0.15, "997 Hz -20 dBFS in both channels = %.2f LUFS (expect -20.0)", l2);
    double l3 = SineLoudness(20, 1.0, false);
    CHECK(l3 < l - 10.0, "20 Hz rumble is weighted down: %.2f LUFS", l3);
}

// Feed constant-loudness 20 ms blocks into a Leveler, like the engine does.
struct Sim {
    Leveler lev;
    double t = 0, target = -36, minDb = -96, maxDb = 0, attack = 0.25, release = 4.0;
    double vol = 0;
    void Run(double lufs, double seconds) {  // NaN = silence (no packets)
        for (double end = t + seconds; t < end - 1e-9; t += 0.02) {
            if (!std::isnan(lufs)) lev.AddBlock(t, std::pow(10.0, (lufs + 0.691) / 10.0) * 960, 960);
            double c = lev.Measure(t, 0.4, -50);
            lev.Update(c, 0.02, attack, release);
            if (lev.HasEnv()) vol = Leveler::Desired(target, lev.Env(), minDb, maxDb);
        }
    }
};

static void TestLeveler() {
    std::printf("Leveler\n");
    Sim s;
    s.Run(-32, 3);
    CHECK(std::fabs(s.vol - (-4)) < 0.3, "dialogue at -32 LUFS -> volume %.1f dB (expect -4)", s.vol);
    s.Run(-14, 0.6);
    CHECK(s.vol < -18, "loud action: 0.6 s after onset volume is %.1f dB (expect < -18)", s.vol);
    s.Run(-14, 2.4);
    CHECK(std::fabs(s.vol - (-22)) < 0.5, "sustained action -> volume %.1f dB (expect -22)", s.vol);
    s.Run(NAN, 2);
    CHECK(std::fabs(s.vol - (-22)) < 0.5, "silence holds the volume: %.1f dB", s.vol);
    s.Run(-32, 1.5);
    CHECK(s.vol > -14, "dialogue returns: 1.5 s later volume is back up to %.1f dB (expect > -14)", s.vol);
    s.Run(-32, 10);
    CHECK(std::fabs(s.vol - (-4)) < 1.0, "eases fully back in after ~10 s: %.1f dB (expect ~ -4)", s.vol);

    Sim q;
    q.Run(-60, 1);
    CHECK(!q.lev.HasEnv(), "audio below the silence threshold is ignored");
    CHECK(Leveler::Desired(-36, -80, -40, -10) == -10, "boost is capped at the max volume");
    CHECK(Leveler::Desired(-36, 0, -40, -10) == -36, "cut stops at the min volume");
}

int main() {
    TestKWeighting();
    TestLeveler();
    std::printf(g_failures ? "\n%d test(s) FAILED\n" : "\nall tests passed\n", g_failures);
    return g_failures ? 1 : 0;
}
