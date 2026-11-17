#pragma once
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

// User-tunable values. Stored in settings.json next to the executable.
struct Core {
    bool enabled = true;
    bool hasTarget = false;  // false until the first run picks a default from the current volume
    double target = -34.0;   // desired "what you hear" loudness = content LUFS + volume dB
    int maxPct = -1;         // highest Windows volume AutoVolume may use (-1 = not chosen yet)
    int minPct = -1;         // lowest Windows volume AutoVolume may use
    double attack = 0.25;    // s: how fast it turns DOWN when things get loud
    double release = 4.0;    // s: how fast it turns back UP when things get quiet
    double window = 0.4;     // s of audio averaged per loudness reading
    double gate = -50.0;     // LUFS: anything quieter counts as silence (volume is held)
};

// Windows volume % -> dB table for one output device (the slider isn't linear and differs per device).
using CalTable = std::vector<std::pair<double, double>>;

class Settings {
public:
    explicit Settings(std::wstring path) : path_(std::move(path)) {}

    void Load();
    bool Save();
    bool SaveIfDirty();

    Core Get() {
        std::lock_guard<std::mutex> l(m_);
        return c_;
    }

    template <class F>
    void Update(F&& f) {
        std::lock_guard<std::mutex> l(m_);
        f(c_);
        dirty_ = true;
    }

    bool GetCalibration(const std::string& deviceId, CalTable& out);
    void SetCalibration(const std::string& deviceId, const CalTable& t);

private:
    std::mutex m_;
    Core c_;
    std::map<std::string, CalTable> cal_;
    bool dirty_ = false;
    std::wstring path_;
};
