#include "WasapiLoopback.h"

#if defined(_WIN32)

#include <Windows.h>
#include <Audioclient.h>
#include <Propkeydef.h>
#include <Functiondiscoverykeys_devpkey.h>
#include <Ksmedia.h>
#include <Mmdeviceapi.h>
#include <Propidl.h>
#include <Propsys.h>
#include <Mmreg.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace
{
class ScopedComApartment final
{
public:
    ScopedComApartment() : result_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
    ~ScopedComApartment() { if (SUCCEEDED(result_)) CoUninitialize(); }
    HRESULT result() const noexcept { return result_; }

private:
    HRESULT result_;
};

template <typename Interface>
class ScopedComPtr final
{
public:
    ScopedComPtr() = default;
    ~ScopedComPtr() { reset(); }
    ScopedComPtr(const ScopedComPtr&) = delete;
    ScopedComPtr& operator=(const ScopedComPtr&) = delete;

    Interface* get() const noexcept { return pointer_; }
    Interface* operator->() const noexcept { return pointer_; }
    Interface** put() noexcept
    {
        reset();
        return &pointer_;
    }
    explicit operator bool() const noexcept { return pointer_ != nullptr; }

    void reset(Interface* replacement = nullptr) noexcept
    {
        if (pointer_ != nullptr)
            pointer_->Release();
        pointer_ = replacement;
    }

private:
    Interface* pointer_ = nullptr;
};

juce::String hresultMessage(const char* operation, HRESULT result)
{
    return juce::String(operation) + " failed (HRESULT 0x"
         + juce::String::toHexString(static_cast<int64_t>(static_cast<uint32_t>(result))) + ")";
}

bool isFloatFormat(const WAVEFORMATEX* format) noexcept
{
    if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
        return true;

    if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE && format->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX))
    {
        const auto* extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
        return IsEqualGUID(extensible->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) != FALSE;
    }

    return false;
}

bool isPcmFormat(const WAVEFORMATEX* format) noexcept
{
    if (format->wFormatTag == WAVE_FORMAT_PCM)
        return true;

    if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE && format->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX))
    {
        const auto* extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
        return IsEqualGUID(extensible->SubFormat, KSDATAFORMAT_SUBTYPE_PCM) != FALSE;
    }

    return false;
}

float decodeSample(const BYTE* data, WORD bitsPerSample, bool floatingPoint) noexcept
{
    if (floatingPoint && bitsPerSample == 32)
    {
        float sample = 0.0f;
        std::memcpy(&sample, data, sizeof(sample));
        return std::isfinite(sample) ? juce::jlimit(-1.0f, 1.0f, sample) : 0.0f;
    }

    if (!floatingPoint && bitsPerSample == 16)
    {
        int16_t sample = 0;
        std::memcpy(&sample, data, sizeof(sample));
        return static_cast<float>(sample) / 32768.0f;
    }

    if (!floatingPoint && bitsPerSample == 24)
    {
        int32_t sample = static_cast<int32_t>(data[0])
                       | (static_cast<int32_t>(data[1]) << 8)
                       | (static_cast<int32_t>(data[2]) << 16);
        if ((sample & 0x00800000) != 0)
            sample |= static_cast<int32_t>(0xff000000);
        return static_cast<float>(sample) / 8388608.0f;
    }

    if (!floatingPoint && bitsPerSample == 32)
    {
        int32_t sample = 0;
        std::memcpy(&sample, data, sizeof(sample));
        return static_cast<float>(static_cast<double>(sample) / 2147483648.0);
    }

    return 0.0f;
}
} // namespace

WasapiLoopbackCapture::~WasapiLoopbackCapture()
{
    stop();
}

bool WasapiLoopbackCapture::start(SpscStereoFifo& destination, juce::String& error)
{
    stop();
    destination_ = &destination;
    destination.reset();
    stopping_.store(false, std::memory_order_relaxed);
    connected_.store(false, std::memory_order_relaxed);
    setStatus({}, {});
    error.clear();

    const auto stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (stopEvent == nullptr)
    {
        error = "Could not create the WASAPI loopback stop event.";
        setStatus({}, error);
        return false;
    }

    stopEvent_ = stopEvent;
    thread_ = std::thread([this] { run(); });
    return true;
}

void WasapiLoopbackCapture::stop()
{
    stopping_.store(true, std::memory_order_relaxed);
    if (stopEvent_ != nullptr)
        SetEvent(static_cast<HANDLE>(stopEvent_));

    if (thread_.joinable())
        thread_.join();

    if (stopEvent_ != nullptr)
    {
        CloseHandle(static_cast<HANDLE>(stopEvent_));
        stopEvent_ = nullptr;
    }

    connected_.store(false, std::memory_order_relaxed);
    destination_ = nullptr;
}

juce::String WasapiLoopbackCapture::getSourceDeviceName() const
{
    const std::lock_guard<std::mutex> lock(statusMutex_);
    return sourceDeviceName_;
}

juce::String WasapiLoopbackCapture::getLastError() const
{
    const std::lock_guard<std::mutex> lock(statusMutex_);
    return lastError_;
}

void WasapiLoopbackCapture::setStatus(const juce::String& deviceName, const juce::String& error)
{
    const std::lock_guard<std::mutex> lock(statusMutex_);
    sourceDeviceName_ = deviceName;
    lastError_ = error;
}

void WasapiLoopbackCapture::run()
{
    ScopedComApartment comApartment;
    if (FAILED(comApartment.result()))
    {
        setStatus({}, hresultMessage("CoInitializeEx", comApartment.result()));
        return;
    }

    ScopedComPtr<IMMDeviceEnumerator> enumerator;
    auto hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                               __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(enumerator.put()));
    if (FAILED(hr))
    {
        setStatus({}, hresultMessage("CoCreateInstance(MMDeviceEnumerator)", hr));
        return;
    }

    ScopedComPtr<IMMDevice> device;
    hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, device.put());
    if (FAILED(hr))
    {
        setStatus({}, hresultMessage("GetDefaultAudioEndpoint", hr));
        return;
    }

    juce::String friendlyName("Default render endpoint");
    ScopedComPtr<IPropertyStore> properties;
    if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, properties.put())))
    {
        PROPVARIANT value;
        PropVariantInit(&value);
        if (SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName, &value)) && value.vt == VT_LPWSTR)
            friendlyName = juce::String(value.pwszVal);
        PropVariantClear(&value);
    }
    setStatus(friendlyName, {});

    ScopedComPtr<IAudioClient> audioClient;
    hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                          reinterpret_cast<void**>(audioClient.put()));
    if (FAILED(hr))
    {
        setStatus(friendlyName, hresultMessage("Activate(IAudioClient)", hr));
        return;
    }

    WAVEFORMATEX* mixFormat = nullptr;
    hr = audioClient->GetMixFormat(&mixFormat);
    if (FAILED(hr) || mixFormat == nullptr)
    {
        setStatus(friendlyName, hresultMessage("GetMixFormat", hr));
        if (mixFormat != nullptr)
            CoTaskMemFree(mixFormat);
        return;
    }

    const auto floatingPoint = isFloatFormat(mixFormat);
    const auto pcm = isPcmFormat(mixFormat);
    const auto formatChannels = mixFormat->nChannels;
    const auto formatBits = mixFormat->wBitsPerSample;
    const auto formatBlockAlign = mixFormat->nBlockAlign;
    const auto formatBytesPerSample = static_cast<int>(formatBits / 8);
    if ((!floatingPoint && !pcm)
        || (floatingPoint && formatBits != 32)
        || (pcm && formatBits != 16 && formatBits != 24 && formatBits != 32)
        || formatChannels == 0
        || formatBlockAlign == 0)
    {
        setStatus(friendlyName, "The endpoint mix format is not supported by the loopback converter.");
        CoTaskMemFree(mixFormat);
        return;
    }

    const auto audioEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (audioEvent == nullptr)
    {
        setStatus(friendlyName, "Could not create the WASAPI sample-ready event.");
        CoTaskMemFree(mixFormat);
        return;
    }

    constexpr REFERENCE_TIME requestedBufferDuration = 1000000; // 100 ms in 100 ns units
    hr = audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                 AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                 requestedBufferDuration, 0, mixFormat, nullptr);
    CoTaskMemFree(mixFormat);

    if (FAILED(hr))
    {
        setStatus(friendlyName, hresultMessage("IAudioClient::Initialize(loopback)", hr));
        CloseHandle(audioEvent);
        return;
    }

    hr = audioClient->SetEventHandle(audioEvent);
    if (FAILED(hr))
    {
        setStatus(friendlyName, hresultMessage("SetEventHandle", hr));
        CloseHandle(audioEvent);
        return;
    }

    ScopedComPtr<IAudioCaptureClient> captureClient;
    hr = audioClient->GetService(__uuidof(IAudioCaptureClient),
                                 reinterpret_cast<void**>(captureClient.put()));
    if (FAILED(hr))
    {
        setStatus(friendlyName, hresultMessage("GetService(IAudioCaptureClient)", hr));
        CloseHandle(audioEvent);
        return;
    }

    hr = audioClient->Start();
    if (FAILED(hr))
    {
        setStatus(friendlyName, hresultMessage("IAudioClient::Start", hr));
        CloseHandle(audioEvent);
        return;
    }

    connected_.store(true, std::memory_order_relaxed);
    HANDLE events[2] { static_cast<HANDLE>(stopEvent_), audioEvent };
    std::array<float, 4096> leftBlock {};
    std::array<float, 4096> rightBlock {};

    while (!stopping_.load(std::memory_order_relaxed))
    {
        const auto waitResult = WaitForMultipleObjects(2, events, FALSE, 1000);
        if (waitResult == WAIT_OBJECT_0)
            break;
        if (waitResult == WAIT_TIMEOUT)
            continue;
        if (waitResult != WAIT_OBJECT_0 + 1)
        {
            setStatus(friendlyName, "WASAPI loopback wait failed.");
            break;
        }

        UINT32 packetFrames = 0;
        while (SUCCEEDED(captureClient->GetNextPacketSize(&packetFrames)) && packetFrames > 0)
        {
            BYTE* packetData = nullptr;
            UINT32 framesAvailable = 0;
            DWORD flags = 0;
            hr = captureClient->GetBuffer(&packetData, &framesAvailable, &flags, nullptr, nullptr);
            if (FAILED(hr))
            {
                setStatus(friendlyName, hresultMessage("IAudioCaptureClient::GetBuffer", hr));
                break;
            }

            auto frameOffset = 0u;
            while (frameOffset < framesAvailable && destination_ != nullptr)
            {
                const auto frames = static_cast<int>(std::min<UINT32>(
                    static_cast<UINT32>(leftBlock.size()), framesAvailable - frameOffset));

                for (int i = 0; i < frames; ++i)
                {
                    float leftSample = 0.0f;
                    float rightSample = 0.0f;
                    if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) == 0 && packetData != nullptr)
                    {
                        const auto* frame = packetData
                            + static_cast<std::size_t>(frameOffset + static_cast<UINT32>(i)) * formatBlockAlign;
                        leftSample = decodeSample(frame, formatBits, floatingPoint);
                        rightSample = formatChannels > 1
                            ? decodeSample(frame + formatBytesPerSample, formatBits, floatingPoint)
                            : leftSample;
                    }
                    leftBlock[static_cast<std::size_t>(i)] = leftSample;
                    rightBlock[static_cast<std::size_t>(i)] = rightSample;
                }

                destination_->push(leftBlock.data(), rightBlock.data(), frames);
                frameOffset += static_cast<UINT32>(frames);
            }

            captureClient->ReleaseBuffer(framesAvailable);
            packetFrames = 0;
        }
    }

    audioClient->Stop();
    connected_.store(false, std::memory_order_relaxed);
    CloseHandle(audioEvent);
}

#else

WasapiLoopbackCapture::~WasapiLoopbackCapture() = default;
bool WasapiLoopbackCapture::start(SpscStereoFifo&, juce::String& error)
{
    error = "WASAPI loopback is only available on Windows.";
    return false;
}
void WasapiLoopbackCapture::stop() {}
juce::String WasapiLoopbackCapture::getSourceDeviceName() const { return {}; }
juce::String WasapiLoopbackCapture::getLastError() const { return "WASAPI is unavailable."; }
void WasapiLoopbackCapture::setStatus(const juce::String&, const juce::String&) {}
void WasapiLoopbackCapture::run() {}

#endif
