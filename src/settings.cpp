#include "settings.h"

#include <windows.h>

#include <fstream>

#include "json.hpp"
#include "log.h"

using nlohmann::json;

void Settings::Load() {
    std::ifstream f(path_);
    if (!f) return;
    try {
        json j = json::parse(f);
        std::lock_guard<std::mutex> l(m_);
        c_.enabled = j.value("enabled", c_.enabled);
        if (j.contains("target") && j["target"].is_number()) {
            c_.target = j["target"].get<double>();
            c_.hasTarget = true;
        }
        if (j.contains("max_pct") && j["max_pct"].is_number()) c_.maxPct = (int)std::lround(j["max_pct"].get<double>());
        if (j.contains("min_pct") && j["min_pct"].is_number()) c_.minPct = (int)std::lround(j["min_pct"].get<double>());
        c_.attack = j.value("attack", c_.attack);
        c_.release = j.value("release", c_.release);
        c_.window = j.value("window", c_.window);
        c_.gate = j.value("gate", c_.gate);
        if (j.contains("calibration") && j["calibration"].is_object()) {
            for (auto& [id, arr] : j["calibration"].items()) {
                CalTable t;
                for (auto& p : arr)
                    if (p.is_array() && p.size() == 2) t.emplace_back(p[0].get<double>(), p[1].get<double>());
                if (!t.empty()) cal_[id] = std::move(t);
            }
        }
    } catch (const std::exception& e) {
        Log("could not read settings (%s), using defaults", e.what());
    }
}

bool Settings::Save() {
    json j;
    {
        std::lock_guard<std::mutex> l(m_);
        j["enabled"] = c_.enabled;
        j["target"] = c_.hasTarget ? json(c_.target) : json(nullptr);
        j["max_pct"] = c_.maxPct >= 0 ? json(c_.maxPct) : json(nullptr);
        j["min_pct"] = c_.minPct >= 0 ? json(c_.minPct) : json(nullptr);
        j["attack"] = c_.attack;
        j["release"] = c_.release;
        j["window"] = c_.window;
        j["gate"] = c_.gate;
        json cal = json::object();
        for (auto& [id, t] : cal_) {
            json arr = json::array();
            for (auto& [pct, db] : t) arr.push_back({pct, db});
            cal[id] = arr;
        }
        j["calibration"] = cal;
        dirty_ = false;
    }
    std::wstring tmp = path_ + L".tmp";
    {
        std::ofstream f(tmp, std::ios::trunc);
        if (!f) return false;
        f << j.dump(2);
        if (!f) return false;
    }
    return MoveFileExW(tmp.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}

bool Settings::SaveIfDirty() {
    {
        std::lock_guard<std::mutex> l(m_);
        if (!dirty_) return true;
    }
    return Save();
}

bool Settings::GetCalibration(const std::string& deviceId, CalTable& out) {
    std::lock_guard<std::mutex> l(m_);
    auto it = cal_.find(deviceId);
    if (it == cal_.end()) return false;
    out = it->second;
    return true;
}

void Settings::SetCalibration(const std::string& deviceId, const CalTable& t) {
    std::lock_guard<std::mutex> l(m_);
    cal_[deviceId] = t;
    dirty_ = true;
}
