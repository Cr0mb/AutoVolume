#pragma once
#include <windows.h>

#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "settings.h"

struct HistPoint {
    double t;
    float content;  // LUFS, NaN = silence
    float volDb;    // Windows master volume (dB)
    float target;   // listening level (dB)
};

struct EngineStatus {
    std::string device;
    std::string state = "Starting";  // "Running", "Starting", "Switching device", "Error: ..."
    bool error = false;
    bool hasContent = false;
    double content = 0;  // LUFS of what's playing
    bool hasVol = false;
    double volDb = 0, volPct = 0;
    bool hasBase = false;
    double baseDb = 0;  // volume to return to when switched off / on exit
    bool muted = false;
};

// Background thread: listens to the default output (WASAPI loopback), measures loudness and
// rides the Windows master volume.
class Engine {
public:
    static constexpr double kHistSeconds = 30.0;

    explicit Engine(Settings& s);
    ~Engine();
    void Start();
    void Stop();

    EngineStatus Status();
    void History(std::vector<HistPoint>& out);

private:
    void Run();
    bool Session();  // one device session; false on error
    void SetState(const std::string& s, bool error = false);
    double PctToDb(double pct) const;
    double DbToPct(double db) const;

    Settings& s_;
    std::thread thread_;
    HANDLE stopEvent_ = nullptr;
    std::mutex m_;
    EngineStatus st_;
    std::deque<HistPoint> hist_;
    std::vector<double> tPct_, tDb_;  // calibration table, dB strictly increasing
};
