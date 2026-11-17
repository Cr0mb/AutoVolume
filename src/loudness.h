#pragma once
// Loudness measurement (ITU-R BS.1770 K-weighting) and the leveling controller.
// No Windows dependencies, so it can be unit tested on its own.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <limits>
#include <vector>

constexpr double kPi = 3.14159265358979323846;

inline double PowerToLufs(double meanSquare) { return -0.691 + 10.0 * std::log10(meanSquare + 1e-15); }

struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
};

struct BiquadState {
    double z1 = 0, z2 = 0;
    inline double Run(const Biquad& q, double x) {  // transposed direct form II
        double y = q.b0 * x + z1;
        z1 = q.b1 * x - q.a1 * y + z2;
        z2 = q.b2 * x - q.a2 * y;
        return y;
    }
};

class KWeighting {
public:
    KWeighting(double fs, int channels) : ch_(channels), s1_(channels), s2_(channels), w_(channels, 1.0) {
        {  // stage 1: high shelf (+4 dB above ~1.7 kHz, models the head)
            const double G = 3.999843853973347, Q = 0.7071752369554196, fc = 1681.974450955533;
            const double K = std::tan(kPi * fc / fs);
            const double Vh = std::pow(10.0, G / 20.0), Vb = std::pow(Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            shelf_ = {(Vh + Vb * K / Q + K * K) / a0, 2.0 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0,
                      2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0};
        }
        {  // stage 2: high pass at ~38 Hz
            const double Q = 0.5003270373238773, fc = 38.13547087602444;
            const double K = std::tan(kPi * fc / fs);
            const double a0 = 1.0 + K / Q + K * K;
            hp_ = {1.0, -2.0, 1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0};
        }
        // channel order L R C LFE Ls Rs ...: LFE excluded, surrounds weighted +1.5 dB
        if (channels >= 6) {
            w_[3] = 0.0;
            w_[4] = w_[5] = 1.41;
        }
    }

    // Interleaved float samples -> sum over frames of channel-weighted K-filtered squares.
    // Divide by frame count for mean-square power.
    double Process(const float* x, uint32_t frames) {
        double acc = 0.0;
        for (uint32_t f = 0; f < frames; ++f) {
            for (int c = 0; c < ch_; ++c) {
                double v = x ? x[(size_t)f * ch_ + c] : 0.0;
                v = s1_[c].Run(shelf_, v);
                v = s2_[c].Run(hp_, v);
                acc += w_[c] * v * v;
            }
        }
        return acc;
    }

    int Channels() const { return ch_; }

private:
    int ch_;
    Biquad shelf_, hp_;
    std::vector<BiquadState> s1_, s2_;
    std::vector<double> w_;
};

// Turns measured loudness into a target volume.
//  * Measure(): momentary loudness over a short window, NaN when silent.
//  * Update(): smoothed loudness that rises fast (attack) and falls slowly (release).
//  * Desired(): volume (dB) that puts the smoothed loudness on target, within limits.
class Leveler {
public:
    void AddBlock(double t, double sumSquares, uint32_t frames) {
        blocks_.push_back({t, sumSquares, frames});
        last_ = t;
    }

    double Measure(double now, double window, double gate) {
        while (!blocks_.empty() && blocks_.front().t < now - window) blocks_.pop_front();
        if (blocks_.empty() || now - last_ > 0.25) return std::numeric_limits<double>::quiet_NaN();
        double ss = 0, n = 0;
        for (const auto& b : blocks_) {
            ss += b.sumSquares;
            n += b.frames;
        }
        if (n <= 0) return std::numeric_limits<double>::quiet_NaN();
        double l = PowerToLufs(ss / n);
        return l > gate ? l : std::numeric_limits<double>::quiet_NaN();
    }

    void Update(double content, double dt, double attack, double release) {
        if (std::isnan(content)) return;  // silence: hold
        if (!hasEnv_) {
            env_ = content;
            hasEnv_ = true;
            return;
        }
        double tau;
        if (content > env_) {
            tau = attack;
        } else {
            // big drop (explosion -> dialogue): recover faster, then ease in gently
            double gap = env_ - content;
            tau = release * std::clamp(6.0 / std::max(gap, 0.1), 0.25, 1.0);
        }
        env_ += (content - env_) * (1.0 - std::exp(-dt / std::max(tau, 0.01)));
    }

    static double Desired(double target, double env, double minDb, double maxDb) {
        return std::clamp(target - env, std::min(minDb, maxDb), maxDb);
    }

    bool HasEnv() const { return hasEnv_; }
    double Env() const { return env_; }

private:
    struct Block {
        double t, sumSquares;
        uint32_t frames;
    };
    std::deque<Block> blocks_;
    double last_ = -1e9;
    double env_ = 0;
    bool hasEnv_ = false;
};
