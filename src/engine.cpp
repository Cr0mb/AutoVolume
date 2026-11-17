#include "engine.h"

#include <initguid.h>  // instantiate the GUIDs / property keys used below

#include <audioclient.h>
#include <endpointvolume.h>
#include <propkeydef.h>
#include <functiondiscoverykeys_devpkey.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <cmath>
#include <cstdio>
#include <memory>

#include "log.h"
#include "loudness.h"
#include "util.h"

using Microsoft::WRL::ComPtr;

namespace {

double Interp(double x, const std::vector<double>& xs, const std::vector<double>& ys) {
    if (xs.empty()) return x;
    if (x <= xs.front()) return ys.front();
    if (x >= xs.back()) return ys.back();
    auto it = std::upper_bound(xs.begin(), xs.end(), x);
    size_t i = (size_t)(it - xs.begin());
    double t = (x - xs[i - 1]) / (xs[i] - xs[i - 1]);
    return ys[i - 1] + t * (ys[i] - ys[i - 1]);
}

std::string HrText(const char* what, HRESULT hr) {
    char buf[160];
    snprintf(buf, sizeof(buf), "%s failed (0x%08lX)", what, (unsigned long)hr);
    return buf;
}

std::string DefaultDeviceId(IMMDeviceEnumerator* en) {
    ComPtr<IMMDevice> dev;
    if (FAILED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev))) return {};
    LPWSTR id = nullptr;
    if (FAILED(dev->GetId(&id))) return {};
    std::string s = ToUtf8(id);
    CoTaskMemFree(id);
    return s;
}

}  // namespace

Engine::Engine(Settings& s) : s_(s) { stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr); }

Engine::~Engine() {
    Stop();
    if (stopEvent_) CloseHandle(stopEvent_);
}

void Engine::Start() {
    ResetEvent(stopEvent_);
    thread_ = std::thread([this] { Run(); });
}

void Engine::Stop() {
    if (!thread_.joinable()) return;
    SetEvent(stopEvent_);
    thread_.join();
}

EngineStatus Engine::Status() {
    std::lock_guard<std::mutex> l(m_);
    return st_;
}

void Engine::History(std::vector<HistPoint>& out) {
    std::lock_guard<std::mutex> l(m_);
    out.assign(hist_.begin(), hist_.end());
}

void Engine::SetState(const std::string& s, bool error) {
    std::lock_guard<std::mutex> l(m_);
    st_.state = s;
    st_.error = error;
}

double Engine::PctToDb(double pct) const { return Interp(pct, tPct_, tDb_); }
double Engine::DbToPct(double db) const { return Interp(db, tDb_, tPct_); }

void Engine::Run() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    while (WaitForSingleObject(stopEvent_, 0) == WAIT_TIMEOUT) {
        if (!Session()) WaitForSingleObject(stopEvent_, 3000);  // error: retry shortly
    }
    CoUninitialize();
}

#define HR_CHECK(expr)                                  \
    do {                                                \
        HRESULT hr_ = (expr);                           \
        if (FAILED(hr_)) {                              \
            std::string e_ = HrText(#expr, hr_);        \
            Log("audio error: %s", e_.c_str());         \
            SetState("Error: " + e_ + " - retrying", true); \
            return false;                               \
        }                                               \
    } while (0)

bool Engine::Session() {
    ComPtr<IMMDeviceEnumerator> en;
    HR_CHECK(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en)));
    ComPtr<IMMDevice> dev;
    HR_CHECK(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev));

    LPWSTR idw = nullptr;
    HR_CHECK(dev->GetId(&idw));
    const std::string devId = ToUtf8(idw);
    CoTaskMemFree(idw);

    std::string devName = "Default output";
    {
        ComPtr<IPropertyStore> ps;
        if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &ps))) {
            PROPVARIANT pv;
            PropVariantInit(&pv);
            if (SUCCEEDED(ps->GetValue(PKEY_Device_FriendlyName, &pv)) && pv.vt == VT_LPWSTR) devName = ToUtf8(pv.pwszVal);
            PropVariantClear(&pv);
        }
    }

    ComPtr<IAudioEndpointVolume> vol;
    HR_CHECK(dev->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, &vol));
    float loF = -96, hiF = 0, incF = 0;
    HR_CHECK(vol->GetVolumeRange(&loF, &hiF, &incF));
    const double lo = loF, hi = hiF;

    auto getDb = [&] {
        float v = 0;
        vol->GetMasterVolumeLevel(&v);
        return (double)v;
    };
    auto setDb = [&](double db) {
        vol->SetMasterVolumeLevel((float)Clamp(db, lo, hi), nullptr);
        return getDb();
    };

    // ---- volume curve calibration (once per device, cached in settings)
    CalTable cal;
    if (!s_.GetCalibration(devId, cal)) {
        Log("calibrating volume curve for %s", devId.c_str());
        float orig = 0;
        BOOL wasMuted = FALSE;
        vol->GetMasterVolumeLevelScalar(&orig);
        vol->GetMute(&wasMuted);
        vol->SetMute(TRUE, nullptr);
        for (int pct = 0; pct <= 100; pct += 2) {
            vol->SetMasterVolumeLevelScalar(pct / 100.0f, nullptr);
            cal.emplace_back(pct, std::round(getDb() * 1000.0) / 1000.0);
        }
        vol->SetMasterVolumeLevelScalar(orig, nullptr);
        vol->SetMute(wasMuted, nullptr);
        s_.SetCalibration(devId, cal);
    }
    std::sort(cal.begin(), cal.end());
    tPct_.clear();
    tDb_.clear();
    for (auto& [pct, db] : cal) {
        if (tDb_.empty() || db > tDb_.back() + 1e-6) {
            tPct_.push_back(pct);
            tDb_.push_back(db);
        }
    }
    if (tDb_.size() < 2) {
        tPct_ = {0.0, 100.0};
        tDb_ = {lo, hi};
    }

    // ---- first-run defaults, based on the volume you use today
    {
        const double cur = getDb();
        s_.Update([&](Core& c) {
            if (!c.hasTarget) {
                c.target = std::round((cur - 26.0) * 10.0) / 10.0;  // assume typical dialogue ~ -26 LUFS
                c.hasTarget = true;
            }
            if (c.maxPct < 0) c.maxPct = (int)std::lround(std::max(DbToPct(std::min(cur + 12.0, 0.0)), DbToPct(cur)));
            if (c.minPct < 0) c.minPct = (int)std::lround(DbToPct(cur - 20.0));
        });
    }

    // ---- loopback capture of everything the device plays
    ComPtr<IAudioClient> client;
    HR_CHECK(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client));
    WAVEFORMATEX* wfRaw = nullptr;
    HR_CHECK(client->GetMixFormat(&wfRaw));
    std::unique_ptr<WAVEFORMATEX, decltype(&CoTaskMemFree)> wf(wfRaw, &CoTaskMemFree);
    bool isFloat = wf->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
    if (wf->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        auto* ext = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(wf.get());
        isFloat = IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) != FALSE;
    }
    const int ch = wf->nChannels;
    const int bits = wf->wBitsPerSample;
    const double sr = wf->nSamplesPerSec;
    HR_CHECK(client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 2000000, 0, wf.get(), nullptr));
    ComPtr<IAudioCaptureClient> capture;
    HR_CHECK(client->GetService(IID_PPV_ARGS(&capture)));
    HR_CHECK(client->Start());
    Log("listening on %s (%.0f Hz, %d ch, %s%d), volume range %.1f..%.1f dB", devName.c_str(), sr, ch,
        isFloat ? "float" : "int", bits, lo, hi);

    KWeighting kw(sr, ch);
    Leveler lev;
    std::vector<float> conv;

    double now = NowSeconds();
    double cur = getDb();
    double lastSet = cur, base = cur;
    double lastEnvT = now, lastApply = 0, lastHist = 0, lastCheck = now;
    bool enabledPrev = s_.Get().enabled;
    bool ok = true;
    {
        std::lock_guard<std::mutex> l(m_);
        st_.device = devName;
        st_.state = "Running";
        st_.error = false;
        st_.hasBase = true;
        st_.baseDb = base;
    }

    while (WaitForSingleObject(stopEvent_, 10) == WAIT_TIMEOUT) {
        // ---- pull captured audio
        UINT32 packet = 0;
        HRESULT hr;
        while (SUCCEEDED(hr = capture->GetNextPacketSize(&packet)) && packet > 0) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            hr = capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
            if (FAILED(hr)) break;
            const float* samples = nullptr;
            if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT)) {
                if (isFloat && bits == 32) {
                    samples = reinterpret_cast<const float*>(data);
                } else {
                    conv.resize((size_t)frames * ch);
                    for (size_t i = 0; i < conv.size(); ++i) {
                        if (bits == 16) conv[i] = reinterpret_cast<const int16_t*>(data)[i] / 32768.0f;
                        else if (bits == 32) conv[i] = (float)(reinterpret_cast<const int32_t*>(data)[i] / 2147483648.0);
                        else conv[i] = 0.0f;
                    }
                    samples = conv.data();
                }
            }
            const double ss = kw.Process(samples, frames);
            capture->ReleaseBuffer(frames);
            lev.AddBlock(NowSeconds(), ss, frames);
        }
        if (FAILED(hr)) {
            Log("capture stopped (0x%08lX), restarting", (unsigned long)hr);
            SetState("Switching device");
            break;
        }

        now = NowSeconds();
        Core c = s_.Get();

        // ---- loudness now, and the smoothed loudness the volume follows
        const double content = lev.Measure(now, c.window, c.gate);
        lev.Update(content, now - lastEnvT, c.attack, c.release);
        lastEnvT = now;

        cur = getDb();
        BOOL muted = FALSE;
        vol->GetMute(&muted);

        // ---- you moved the volume yourself -> learn from it
        if (std::fabs(cur - lastSet) > 0.6) {
            const double delta = cur - lastSet;
            if (c.enabled) {
                const double pct = DbToPct(cur);
                s_.Update([&](Core& x) {
                    x.target = std::round((x.target + delta) * 100.0) / 100.0;
                    if (pct > x.maxPct + 0.5) x.maxPct = (int)std::lround(pct);
                    if (pct < x.minPct - 0.5) x.minPct = (int)std::lround(pct);
                });
                base = Clamp(base + delta, lo, hi);
                Log("manual change %+.1f dB -> target %.1f", delta, c.target + delta);
                c = s_.Get();
            }
            lastSet = cur;
        }

        // ---- switched on / off
        if (c.enabled != enabledPrev) {
            if (c.enabled) base = cur;
            else cur = lastSet = setDb(base);
            enabledPrev = c.enabled;
        }

        // ---- ride the volume
        if (c.enabled && !muted && lev.HasEnv() && now - lastApply >= 0.05) {
            const double desired = Leveler::Desired(c.target, lev.Env(), PctToDb(c.minPct), PctToDb(c.maxPct));
            if (std::fabs(desired - cur) >= 0.5) {
                cur = lastSet = setDb(desired);
                lastApply = now;
            }
        }

        {
            std::lock_guard<std::mutex> l(m_);
            st_.hasContent = !std::isnan(content);
            st_.content = st_.hasContent ? content : 0.0;
            st_.hasVol = true;
            st_.volDb = cur;
            st_.volPct = DbToPct(cur);
            st_.baseDb = base;
            st_.muted = muted != FALSE;
            if (now - lastHist >= 0.1) {
                hist_.push_back({now, (float)content, (float)cur, (float)c.target});
                while (!hist_.empty() && hist_.front().t < now - kHistSeconds - 1.0) hist_.pop_front();
                lastHist = now;
            }
        }

        // ---- default output changed (e.g. headphones plugged in)?
        if (now - lastCheck > 2.0) {
            lastCheck = now;
            if (DefaultDeviceId(en.Get()) != devId) {
                Log("default output device changed, restarting");
                SetState("Switching device");
                break;
            }
        }
    }

    // leave the volume where you had it, not wherever the last explosion pushed it
    if (s_.Get().enabled) setDb(base);
    client->Stop();
    {
        std::lock_guard<std::mutex> l(m_);
        st_.hasBase = false;
    }
    return ok;
}
