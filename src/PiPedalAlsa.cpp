// Copyright (c) Robin E.R. Davies
// Copyright (c) Gabriel Hernandez
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
// the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#include "pch.h"
#include "PiPedalAlsa.hpp"
#include "alsa/asoundlib.h"
#include "Lv2Log.hpp"
#include <mutex>
#include <algorithm>
#include "Finally.hpp"
#include "PiPedalException.hpp"

using namespace pipedal;

static uint32_t RATES[] = {
    22050, 24000, 44100, 48000, 44100 * 2, 48000 * 2, 44100 * 4, 48000 * 4};

std::mutex alsaMutex;

bool PiPedalAlsaDevices::getCachedDevice(const std::string &name, AlsaDeviceInfo *pResult)
{
    auto it = cachedDevices.find(name);
    if (it != cachedDevices.end())
    {
        *pResult = it->second;
        return true;
    }
    return false;
}
void PiPedalAlsaDevices::cacheDevice(const std::string &name, const AlsaDeviceInfo &deviceInfo)
{
    cachedDevices[name] = deviceInfo;
}

static bool isSupportedAudioDevice(const AlsaDeviceInfo &d)
{
    std::string name = d.name_ + " " + d.longName_;
    std::transform(name.begin(), name.end(), name.begin(), [](char c)
                   { return std::tolower(c); });
    if (name.find("hdmi") != std::string::npos) return false;
//    if (name.find("bcm2835") != std::string::npos) return false;
    return true;
};

static std::string ToLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c)
                   { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool pipedal::IsRaspberryPiCodecZero(
    const std::string &cardId,
    const std::string &driver,
    const std::string &cardName,
    const std::string &longName)
{
    const std::string lowerCardId = ToLower(cardId);
    const std::string lowerDriver = ToLower(driver);
    const std::string identity = ToLower(cardName + " " + longName);
    if (lowerDriver == "usb-audio")
        return false;

    // Match the stable card id and the ASoC card identity. In particular, do
    // not classify a USB card merely because its marketing name contains
    // "Codec Zero" or "Raspberry Pi".
    if (lowerCardId == "zero")
    {
        return lowerDriver == "rpi_codec_zero" ||
               identity.find("rpi codec zero") != std::string::npos ||
               identity.find("raspberry pi codec zero") != std::string::npos;
    }
    if (lowerCardId == "iqaudiocodec")
    {
        return lowerDriver == "iqaudiocodec" ||
               identity.find("iqaudio codec") != std::string::npos ||
               identity.find("iqaudiocodec") != std::string::npos;
    }
    return false;
}

namespace
{
    struct CodecZeroControl
    {
        const char *name;
        snd_ctl_elem_type_t type;
        long integerValue;
        const char *enumValue;
    };

    constexpr CodecZeroControl IntegerControl(const char *name, long value)
    {
        return {name, SND_CTL_ELEM_TYPE_INTEGER, value, nullptr};
    }

    constexpr CodecZeroControl BooleanControl(const char *name, bool value)
    {
        return {name, SND_CTL_ELEM_TYPE_BOOLEAN, value ? 1L : 0L, nullptr};
    }

    constexpr CodecZeroControl EnumControl(const char *name, const char *value)
    {
        return {name, SND_CTL_ELEM_TYPE_ENUMERATED, 0, value};
    }

    // Values are the Raspberry Pi stereo AUX-IN / AUX-OUT reference profile.
    // The raw volume values correspond to 0 dB AUX, +6 dB mixin PGA, 0 dB
    // ADC/DAC and -8 dB headphone/AUX output.
    constexpr CodecZeroControl CodecZeroCaptureControls[] = {
        IntegerControl("Aux Volume", 53),
        IntegerControl("Mixin PGA Volume", 7),
        IntegerControl("ADC Volume", 112),
        BooleanControl("Mic 1 Switch", false),
        BooleanControl("Mic 2 Switch", false),
        BooleanControl("Aux Switch", true),
        BooleanControl("Mixin PGA Switch", true),
        BooleanControl("ADC Switch", true),
        BooleanControl("DMIC Switch", false),
        BooleanControl("ALC Switch", false),
        BooleanControl("ADC HPF Switch", false),
        BooleanControl("ADC Voice Mode Switch", false),
        BooleanControl("AUX Jack Switch", true),
        BooleanControl("Aux ZC Switch", false),
        BooleanControl("Mixin PGA ZC Switch", false),
        BooleanControl("Aux Gain Ramping Switch", true),
        BooleanControl("Mixin Gain Ramping Switch", false),
        BooleanControl("ADC Gain Ramping Switch", false),
        BooleanControl("Mixin Left Aux Left Switch", true),
        BooleanControl("Mixin Left Mic 1 Switch", false),
        BooleanControl("Mixin Left Mic 2 Switch", false),
        BooleanControl("Mixin Left Mixin Right Switch", false),
        BooleanControl("Mixin Right Aux Right Switch", true),
        BooleanControl("Mixin Right Mic 1 Switch", false),
        BooleanControl("Mixin Right Mic 2 Switch", false),
        BooleanControl("Mixin Right Mixin Left Switch", false),
        EnumControl("DAI Left Source MUX", "ADC Left"),
        EnumControl("DAI Right Source MUX", "ADC Right"),
    };

    constexpr CodecZeroControl CodecZeroPlaybackControls[] = {
        IntegerControl("DAC Volume", 112),
        IntegerControl("Headphone Volume", 49),
        BooleanControl("Headphone Switch", true),
        BooleanControl("Lineout Switch", false),
        BooleanControl("DAC Soft Mute Switch", false),
        BooleanControl("DAC EQ Switch", false),
        BooleanControl("DAC HPF Switch", false),
        BooleanControl("DAC Voice Mode Switch", false),
        BooleanControl("DAC NG Switch", false),
        BooleanControl("DAC Mono Switch", false),
        BooleanControl("DAC Invert Switch", false),
        BooleanControl("HP Jack Switch", true),
        BooleanControl("Headphone ZC Switch", false),
        BooleanControl("DAC Gain Ramping Switch", false),
        BooleanControl("Headphone Gain Ramping Switch", true),
        EnumControl("DAC Left Source MUX", "DAI Input Left"),
        EnumControl("DAC Right Source MUX", "DAI Input Right"),
        BooleanControl("Mixout Left Aux Left Switch", false),
        BooleanControl("Mixout Left Mixin Left Switch", false),
        BooleanControl("Mixout Left Mixin Right Switch", false),
        BooleanControl("Mixout Left DAC Left Switch", true),
        BooleanControl("Mixout Left Aux Left Invert Switch", false),
        BooleanControl("Mixout Left Mixin Left Invert Switch", false),
        BooleanControl("Mixout Left Mixin Right Invert Switch", false),
        BooleanControl("Mixout Right Aux Right Switch", false),
        BooleanControl("Mixout Right Mixin Right Switch", false),
        BooleanControl("Mixout Right Mixin Left Switch", false),
        BooleanControl("Mixout Right DAC Right Switch", true),
        BooleanControl("Mixout Right Aux Right Invert Switch", false),
        BooleanControl("Mixout Right Mixin Right Invert Switch", false),
        BooleanControl("Mixout Right Mixin Left Invert Switch", false),
    };

    constexpr CodecZeroControl CodecZeroSharedControls[] = {
        EnumControl("Gain Ramping Rate", "nominal rate * 8"),
    };

    void ValidateCodecZeroControl(snd_ctl_t *ctl, const CodecZeroControl &control)
    {
        snd_ctl_elem_id_t *id = nullptr;
        snd_ctl_elem_info_t *info = nullptr;
        snd_ctl_elem_id_alloca(&id);
        snd_ctl_elem_info_alloca(&info);
        snd_ctl_elem_id_set_interface(id, SND_CTL_ELEM_IFACE_MIXER);
        snd_ctl_elem_id_set_name(id, control.name);
        snd_ctl_elem_info_set_id(info, id);

        int error = snd_ctl_elem_info(ctl, info);
        if (error < 0)
        {
            throw PiPedalException(SS("Codec Zero mixer control '" << control.name
                                      << "' is unavailable: " << snd_strerror(error)));
        }
        if (snd_ctl_elem_info_get_type(info) != control.type ||
            snd_ctl_elem_info_get_count(info) == 0)
        {
            throw PiPedalException(SS("Codec Zero mixer control '" << control.name
                                      << "' has an unexpected type or channel count."));
        }
        if (control.type == SND_CTL_ELEM_TYPE_INTEGER &&
            (control.integerValue < snd_ctl_elem_info_get_min(info) ||
             control.integerValue > snd_ctl_elem_info_get_max(info)))
        {
            throw PiPedalException(SS("Codec Zero mixer control '" << control.name
                                      << "' cannot accept the reference value."));
        }
        if (control.type == SND_CTL_ELEM_TYPE_ENUMERATED)
        {
            bool found = false;
            const unsigned int items = snd_ctl_elem_info_get_items(info);
            for (unsigned int item = 0; item < items; ++item)
            {
                snd_ctl_elem_info_set_item(info, item);
                if (snd_ctl_elem_info(ctl, info) < 0)
                    break;
                if (control.enumValue == std::string(snd_ctl_elem_info_get_item_name(info)))
                {
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                throw PiPedalException(SS("Codec Zero mixer control '" << control.name
                                          << "' does not support value '" << control.enumValue << "'."));
            }
        }
    }

    void SetCodecZeroControl(snd_ctl_t *ctl, const CodecZeroControl &control)
    {
        snd_ctl_elem_id_t *id = nullptr;
        snd_ctl_elem_info_t *info = nullptr;
        snd_ctl_elem_value_t *value = nullptr;
        snd_ctl_elem_id_alloca(&id);
        snd_ctl_elem_info_alloca(&info);
        snd_ctl_elem_value_alloca(&value);

        snd_ctl_elem_id_set_interface(id, SND_CTL_ELEM_IFACE_MIXER);
        snd_ctl_elem_id_set_name(id, control.name);
        snd_ctl_elem_info_set_id(info, id);

        int error = snd_ctl_elem_info(ctl, info);
        if (error < 0)
        {
            throw PiPedalException(SS("Codec Zero mixer control '" << control.name
                                      << "' is unavailable: " << snd_strerror(error)));
        }

        const snd_ctl_elem_type_t actualType = snd_ctl_elem_info_get_type(info);
        if (actualType != control.type)
        {
            throw PiPedalException(SS("Codec Zero mixer control '" << control.name
                                      << "' has an unexpected type."));
        }

        snd_ctl_elem_value_set_id(value, id);
        error = snd_ctl_elem_read(ctl, value);
        if (error < 0)
        {
            throw PiPedalException(SS("Unable to read Codec Zero mixer control '" << control.name
                                      << "': " << snd_strerror(error)));
        }

        const unsigned int count = snd_ctl_elem_info_get_count(info);
        if (control.type == SND_CTL_ELEM_TYPE_ENUMERATED)
        {
            unsigned int selectedItem = snd_ctl_elem_info_get_items(info);
            for (unsigned int item = 0; item < snd_ctl_elem_info_get_items(info); ++item)
            {
                snd_ctl_elem_info_set_item(info, item);
                error = snd_ctl_elem_info(ctl, info);
                if (error < 0)
                {
                    break;
                }
                if (control.enumValue == std::string(snd_ctl_elem_info_get_item_name(info)))
                {
                    selectedItem = item;
                    break;
                }
            }
            if (selectedItem == snd_ctl_elem_info_get_items(info))
            {
                throw PiPedalException(SS("Codec Zero mixer control '" << control.name
                                          << "' does not support value '" << control.enumValue << "'."));
            }
            for (unsigned int index = 0; index < count; ++index)
            {
                snd_ctl_elem_value_set_enumerated(value, index, selectedItem);
            }
        }
        else if (control.type == SND_CTL_ELEM_TYPE_BOOLEAN)
        {
            for (unsigned int index = 0; index < count; ++index)
            {
                snd_ctl_elem_value_set_boolean(value, index, control.integerValue);
            }
        }
        else
        {
            for (unsigned int index = 0; index < count; ++index)
            {
                snd_ctl_elem_value_set_integer(value, index, control.integerValue);
            }
        }

        error = snd_ctl_elem_write(ctl, value);
        if (error < 0)
        {
            throw PiPedalException(SS("Unable to set Codec Zero mixer control '" << control.name
                                      << "': " << snd_strerror(error)));
        }
    }

    template <size_t N>
    void ValidateCodecZeroControls(snd_ctl_t *ctl, const CodecZeroControl (&controls)[N])
    {
        for (const auto &control : controls)
        {
            ValidateCodecZeroControl(ctl, control);
        }
    }

    template <size_t N>
    void SetCodecZeroControls(snd_ctl_t *ctl, const CodecZeroControl (&controls)[N])
    {
        for (const auto &control : controls)
        {
            SetCodecZeroControl(ctl, control);
        }
    }
}

void pipedal::ConfigureAlsaDeviceForPiPedal(
    const std::string &deviceId,
    bool configureCapture,
    bool configurePlayback)
{
    if ((!configureCapture && !configurePlayback) || deviceId.empty() || deviceId == "null")
    {
        return;
    }

    snd_ctl_t *ctl = nullptr;
    int error = snd_ctl_open(&ctl, deviceId.c_str(), 0);
    if (error < 0)
    {
        const std::string lowerDeviceId = ToLower(deviceId);
        if (lowerDeviceId == "hw:zero" || lowerDeviceId == "hw:iqaudiocodec")
        {
            throw PiPedalException(SS("Unable to open Raspberry Pi Codec Zero mixer controls: "
                                      << snd_strerror(error)));
        }
        // Opening the PCM will provide the normal user-facing error for missing
        // or busy devices. Do not change behavior for ordinary ALSA devices.
        return;
    }
    Finally closeCtl{[ctl]() { snd_ctl_close(ctl); }};

    snd_ctl_card_info_t *cardInfo = nullptr;
    snd_ctl_card_info_alloca(&cardInfo);
    error = snd_ctl_card_info(ctl, cardInfo);
    if (error < 0)
    {
        const std::string lowerDeviceId = ToLower(deviceId);
        if (lowerDeviceId == "hw:zero" || lowerDeviceId == "hw:iqaudiocodec")
        {
            throw PiPedalException(SS("Unable to identify Raspberry Pi Codec Zero mixer controls: "
                                      << snd_strerror(error)));
        }
        return;
    }

    if (!IsRaspberryPiCodecZero(
            snd_ctl_card_info_get_id(cardInfo),
            snd_ctl_card_info_get_driver(cardInfo),
            snd_ctl_card_info_get_name(cardInfo),
            snd_ctl_card_info_get_longname(cardInfo)))
    {
        return;
    }

    // Validate the complete requested profile before changing any control, so
    // a driver-version mismatch cannot leave a half-applied routing setup.
    if (configureCapture)
        ValidateCodecZeroControls(ctl, CodecZeroCaptureControls);
    if (configurePlayback)
        ValidateCodecZeroControls(ctl, CodecZeroPlaybackControls);
    ValidateCodecZeroControls(ctl, CodecZeroSharedControls);

    if (configureCapture)
    {
        SetCodecZeroControls(ctl, CodecZeroCaptureControls);
    }
    if (configurePlayback)
    {
        SetCodecZeroControls(ctl, CodecZeroPlaybackControls);
    }
    SetCodecZeroControls(ctl, CodecZeroSharedControls);

    Lv2Log::info(SS("Configured Raspberry Pi Codec Zero for "
                    << (configureCapture ? "stereo AUX input" : "")
                    << (configureCapture && configurePlayback ? " and " : "")
                    << (configurePlayback ? "stereo AUX output" : "") << "."));
}


struct ProcAlsaDevice {
    int cardId; 
    int subdeviceId;
    bool audioCapture;
    bool audioPlayback;
    bool rawMidi;
};

static std::vector<ProcAlsaDevice> getProcAlsaDevices()
{
    std::vector<ProcAlsaDevice> result;

    std::ifstream f {"/proc/asound/devices"};
    if (f.is_open())
    {
        std::string line;
        while (std::getline(f, line))
        {
            // Parse each line of /proc/alsa/devices
            // Format: cardnum: [devicenum- subdevicenum]: type : name
            std::istringstream iss(line);
            std::string token;
            
            // Skip leading whitespace and get card number
            if (!std::getline(iss, token, ':'))
            {
                continue;
            }
            try {
                int cardId = std::stoi(token.substr(token.find_first_not_of(" \t")));
                
                // Get device-subdevice part
                if (!std::getline(iss, token, ':'))
                {
                    continue;
                }
                size_t dashPos = token.find('-');
                if (dashPos == std::string::npos)
                {
                    continue;
                }
                int deviceId = std::stoi(token.substr(token.find_first_not_of(" \t["), dashPos));
                int subdeviceId = std::stoi(token.substr(dashPos + 1, token.find(']') - dashPos - 1));
                        
                // Get type
                if (!std::getline(iss, token, ':'))
                {
                    continue;
                }
                std::string type = token.substr(token.find_first_not_of(" \t"));

                ProcAlsaDevice *pDevice = nullptr;
                for (size_t i = 0; i < result.size(); ++i)
                {
                    if (result[i].cardId == deviceId && result[i].subdeviceId == subdeviceId)
                    {
                        pDevice = &result[i];
                        break;
                    }
                }
                if (pDevice == nullptr)
                {
                    ProcAlsaDevice newDevice;
                    newDevice.cardId = deviceId;
                    newDevice.subdeviceId = subdeviceId;
                    newDevice.audioCapture = false;
                    newDevice.audioPlayback = false;
                    newDevice.rawMidi = false;
                    result.push_back(newDevice);
                    pDevice = &(result[result.size()-1]);
                }
                if (type.find("digital audio capture") != std::string::npos)
                {
                    pDevice->audioCapture = true;
                }
                if ((type.find("digital audio playback") != std::string::npos))
                {
                    pDevice->audioPlayback = true;
                }
                if (type.find("rawmidi") != std::string::npos)
                {
                    pDevice->rawMidi = true;
                }
            } catch (const std::exception &e)
            {
                Lv2Log::error(SS("invalid ALSA proc entry: " << line));
            }
        }
    }

    return result;
}


std::vector<AlsaDeviceInfo> PiPedalAlsaDevices::GetAlsaDevices()
{

    std::lock_guard guard{alsaMutex};

    std::vector<AlsaDeviceInfo> result;

    int err;

    std::vector<ProcAlsaDevice> procAlsaDevices = getProcAlsaDevices();


    snd_pcm_info_t *pcminfo = nullptr;
    snd_pcm_info_alloca(&pcminfo);

    for (const auto &procAlsaDevice: procAlsaDevices)
    {
        std::stringstream ss;
        if (!procAlsaDevice.audioCapture && !procAlsaDevice.audioPlayback) {
            continue;
        }
        ss << "hw:" << procAlsaDevice.cardId;

        std::string cardId = ss.str();

        snd_ctl_t *hDevice = nullptr;

        if ((err = snd_ctl_open(&hDevice, cardId.c_str(), 0)) < 0)
        {
            continue;
        }

        Finally ffhDevice{[hDevice]()
                            { snd_ctl_close(hDevice); }};

        snd_ctl_card_info_t *alsaInfo = nullptr;
        if (snd_ctl_card_info_malloc(&alsaInfo) != 0)
        {
            Lv2Log::error("Failed to allocate ALSA card info");
            continue;
        }

        Finally ffCardInfo{[alsaInfo]()
                            { snd_ctl_card_info_free(alsaInfo); }};

        err = snd_ctl_card_info(hDevice, alsaInfo);
        if (err == 0)
        {

            AlsaDeviceInfo info;
            info.cardId_ = procAlsaDevice.cardId;
            info.id_ = std::string("hw:") + snd_ctl_card_info_get_id(alsaInfo);
            const char *driver = snd_ctl_card_info_get_driver(alsaInfo);
            (void)driver;

            snd_pcm_info_set_device(pcminfo, procAlsaDevice.subdeviceId);
            snd_pcm_info_set_subdevice(pcminfo, 0);
            snd_pcm_info_set_stream(pcminfo, SND_PCM_STREAM_PLAYBACK);

            if ((err = snd_ctl_pcm_info(hDevice, pcminfo)) < 0) {
                snd_pcm_info_set_device(pcminfo, procAlsaDevice.subdeviceId);
                snd_pcm_info_set_subdevice(pcminfo, 0);
                snd_pcm_info_set_stream(pcminfo, SND_PCM_STREAM_CAPTURE);

                if ((err = snd_ctl_pcm_info(hDevice, pcminfo)) < 0) {
                    if (err != -ENOENT)
                        Lv2Log::warning("control digital audio info (%i): %s", info.cardId_, snd_strerror(err));
                    continue;
                }
            }
            /*
            Names and IDs are f-ed up in ALSA. id seems to be vaguely reliable. names are garbage!
            Sample data from Unbuntu 24.04.

            snd_ctl_card_info_get_id(info) snd_ctl_card_info_get_name(alsaInfo) subdevice snd_pcm_info_get_id(pcminfo) snd_pcm_info_get_name(pcminfo)
            Generic                        HD-Audio Generic                     3         HDMI 0                       Acer S231HL
            Generic                        HD-Audio Generic                     3         HDMI 0                       ASUS MG28U
            Generic_1                      HD-Audio Generic                     0         CX11970 Analog               CX11970 Analog
            M2                             M2                                   USB Audio                    USB Audio
            CODEC                          USB AUDIO  CODEC                     USB Audio                    USB Audio

            */
            std::string name;
            {
                // let's assume (without evidence) that names get localized, but ids do not. :-/
                std::string cardId = snd_ctl_card_info_get_id(alsaInfo);
                std::string cardName = snd_ctl_card_info_get_name(alsaInfo);
                std::string pcmId = snd_pcm_info_get_id(pcminfo);
                std::string pcmName = snd_pcm_info_get_name(pcminfo);
                std::string driver = snd_ctl_card_info_get_driver(alsaInfo);
                if (driver == "USB-Audio")
                {
                    name = SS("USB - " << cardId);
                }
                else if (pcmId == pcmName)
                {
                    name = pcmId;
                } else {
                    name = SS(pcmId << '[' << pcmName << ']');
                }
            }
            info.name_ = name;
            info.longName_ = snd_ctl_card_info_get_longname(alsaInfo);
            if (IsRaspberryPiCodecZero(
                    snd_ctl_card_info_get_id(alsaInfo),
                    snd_ctl_card_info_get_driver(alsaInfo),
                    snd_ctl_card_info_get_name(alsaInfo),
                    info.longName_))
            {
                info.deviceProfile_ = ALSA_DEVICE_PROFILE_CODEC_ZERO_AUX;
                info.name_ = "Raspberry Pi Codec Zero (Stereo AUX)";
            }

            // we can't read our own device if it's open so use data that gets
            // cached before we open audio devices.

            AlsaDeviceInfo cachedInfo;
            if (getCachedDevice(info.name_, &cachedInfo))
            {
                // may have been plugged into a different USB connector.
                cachedInfo.cardId_ = info.cardId_;
                cachedInfo.id_ = info.id_;
                result.push_back(cachedInfo);
                continue;
            }


            snd_pcm_t *captureDevice = nullptr;
            snd_pcm_t *playbackDevice = nullptr;
            auto rc = snd_pcm_open(&captureDevice, cardId.c_str(), SND_PCM_STREAM_CAPTURE, SND_PCM_NONBLOCK);
            bool captureOk =  rc == 0;

            Finally ffCaptureDevice{
                [captureDevice]
                { if (captureDevice) snd_pcm_close(captureDevice); }};

            rc  = snd_pcm_open(&playbackDevice, cardId.c_str(), SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK);
            bool playbackOk = rc == 0;

            Finally ffPlaybackDevice{
                [playbackDevice]
                { if (playbackDevice) snd_pcm_close(playbackDevice); }};
            if (procAlsaDevice.audioCapture && !captureOk)
            {
                info.captureBusy_ = true;
            }
            if (procAlsaDevice.audioPlayback && !playbackOk)
            {
                info.playbackBusy_ = true;
            }

            info.supportsCapture_ = captureOk;
            info.supportsPlayback_ = playbackOk;

            if (captureOk || playbackOk)
            {

                snd_pcm_t *hDevice = captureOk ? captureDevice : playbackDevice;
                snd_pcm_hw_params_t *params = nullptr;
                err = snd_pcm_hw_params_malloc(&params);

                if (err == 0)
                {
                    Finally ffParams{[params]
                                        { snd_pcm_hw_params_free(params); }};

                    err = snd_pcm_hw_params_any(hDevice, params);
                    if (err == 0)
                    {
                        unsigned int minRate = 0, maxRate = 0;
                        snd_pcm_uframes_t minBufferSize = 0, maxBufferSize = 0;
                        int dir;

                        err = snd_pcm_hw_params_get_rate_min(params, &minRate, &dir);
                        if (err == 0)
                        {
                            err = snd_pcm_hw_params_get_rate_max(params, &maxRate, &dir);
                            if (err == 0)
                            {
                                for (size_t i = 0; i < sizeof(RATES) / sizeof(RATES[0]); ++i)
                                {
                                    uint32_t rate = RATES[i];
                                    if (rate >= minRate && rate <= maxRate)
                                    {
                                        info.sampleRates_.push_back(rate);
                                    }
                                }
                            }
                            else
                            {
                                Lv2Log::warning(SS("Failed to get maximum sample rate for device '" << info.name_ << "'."));
                            }
                        }
                        else
                        {
                            Lv2Log::warning(SS("Failed to get minimum sample rate for device '" << info.name_ << "'."));
                        }

                        if (err == 0)
                        {
                            err = snd_pcm_hw_params_get_buffer_size_min(params, &minBufferSize);
                            if (err == 0)
                            {
                                err = snd_pcm_hw_params_get_buffer_size_max(params, &maxBufferSize);
                            }
                        }
                        if (err == 0)
                        {
                            if (minBufferSize < 16)
                            {
                                minBufferSize = 16;
                            }

                            info.minBufferSize_ = (uint32_t)minBufferSize;
                            info.maxBufferSize_ = (uint32_t)maxBufferSize;
                        }
                    }
                }
                if (!info.captureBusy_ && !info.playbackBusy_)
                {
                    cacheDevice(info.name_, info);
                    result.push_back(info);
                }
            } else {
                if (info.captureBusy_ || info.playbackBusy_)
                {
                    result.push_back(info);
                }

            }
        }
    }

    Lv2Log::debug("GetAlsaDevices --");

    std::vector<AlsaDeviceInfo> filtered;
    for (auto &device : result)
    {
        if (isSupportedAudioDevice(device))
        {
            filtered.push_back(device);
            Lv2Log::debug(
                SS("   "
                   << device.name_ << " " << device.longName_ << " " << device.cardId_
                   << (device.supportsCapture_ ? " in" : "")
                   << (device.supportsPlayback_ ? " out" : "")
                   << (device.captureBusy_ ? " in(busy)" : "")
                   << (device.captureBusy_ ? " out(busy)" : "")
                ));
        }
    }
    return filtered;
}

static void AddMidiCardDevicesToList(snd_ctl_t *ctl, int card, int device, AlsaMidiDeviceInfo::Direction direction, std::vector<AlsaMidiDeviceInfo> *result)
{
    snd_rawmidi_info_t *info = nullptr;
    const char *name = nullptr;
    const char *sub_name = nullptr;
    int subs = 0, subs_in = 0, subs_out = 0;
    int sub = 0;
    int err = 0;
    ;

    snd_rawmidi_info_alloca(&info);
    snd_rawmidi_info_set_device(info, device);

    snd_rawmidi_info_set_stream(info, SND_RAWMIDI_STREAM_INPUT);
    err = snd_ctl_rawmidi_info(ctl, info);
    if (err >= 0)
        subs_in = snd_rawmidi_info_get_subdevices_count(info);
    else
        subs_in = 0;

    snd_rawmidi_info_set_stream(info, SND_RAWMIDI_STREAM_OUTPUT);
    err = snd_ctl_rawmidi_info(ctl, info);
    if (err >= 0)
        subs_out = snd_rawmidi_info_get_subdevices_count(info);
    else
        subs_out = 0;

    subs = subs_in > subs_out ? subs_in : subs_out;
    if (!subs)
        return;

    switch (direction)
    {
    case AlsaMidiDeviceInfo::In:
        if (subs_out == 0) // out for the device, in for us.
        {
            return; // no input devices to add
        }
        break;
    case AlsaMidiDeviceInfo::Out: // in for the device, out for us.
        if (subs_in == 0)
        {
            return; // no output devices to add
        }
        break;
    case AlsaMidiDeviceInfo::InOut:
        if (subs_in == 0 || subs_out == 0)
        {
            return; // no input or output devices to add
        }
        break;
    case AlsaMidiDeviceInfo::None:
        return; // no devices to add
    }

    for (sub = 0; sub < subs; ++sub)
    {
        snd_rawmidi_info_set_stream(info, direction == AlsaMidiDeviceInfo::Direction::Out ? SND_RAWMIDI_STREAM_INPUT : SND_RAWMIDI_STREAM_OUTPUT);
        snd_rawmidi_info_set_subdevice(info, sub);
        err = snd_ctl_rawmidi_info(ctl, info);
        if (err < 0)
        {
            throw std::runtime_error(
                SS("snd_ctl_rawmidi_info failed  for card " << card << ", device " << device << ", subdevice " << sub << ": " << snd_strerror(err)));
        }
        name = snd_rawmidi_info_get_name(info);
        sub_name = snd_rawmidi_info_get_subdevice_name(info);
        // get card name.

        std::string cardName;
        snd_ctl_card_info_t *card_info = nullptr;
        snd_ctl_card_info_malloc(&card_info);
        if (snd_ctl_card_info(ctl, card_info) == 0)
        {
            cardName = snd_ctl_card_info_get_name(card_info);
        }
        snd_ctl_card_info_free(card_info);
        if (cardName.length() == 0)
        {
            return;
        }

        if (sub == 0 && sub_name[0] == '\0')
        {
            AlsaMidiDeviceInfo info;
            info.name_ = SS("hw:CARD=" << cardName << ",DEV=" << device);
            info.description_ = SS("Virtual MIDI " << card << "-" << device);
            info.card_ = card;
            info.device_ = device;
            info.subdevice_ = 0;
            info.isVirtual_ = true;
            info.subDevices_ = subs;
            if (subs_in > 0 && subs_out > 0)
            {
                info.direction_ = AlsaMidiDeviceInfo::InOut;
            }
            else if (subs_in > 0)
            {
                info.direction_ = AlsaMidiDeviceInfo::In;
            }
            else if (subs_out > 0)
            {
                info.direction_ = AlsaMidiDeviceInfo::Out;
            }
            else
            {
                info.direction_ = AlsaMidiDeviceInfo::None;
            }
            result->push_back(info);
            break;
        }
        else
        {
            AlsaMidiDeviceInfo info;
            info.name_ = SS("hw:CARD=" << cardName << ",DEV=" << device);
            if (sub != 0)
            {
                info.name_ = SS(info.name_ << "," << sub);
            }
            info.description_ = sub_name;
            info.card_ = card;
            info.device_ = device;
            info.subdevice_ = sub;
            info.isVirtual_ = false;
            info.subDevices_ = 1;
            result->push_back(info);
        }
    }
}
static void AddMidiCardToList(int card, std::vector<AlsaMidiDeviceInfo> *result, AlsaMidiDeviceInfo::Direction direction)
{
    snd_ctl_t *ctl = nullptr;
    char name[32];
    int device;
    int err;

    sprintf(name, "hw:%d", card);
    if ((err = snd_ctl_open(&ctl, name, 0)) < 0)
    {
        throw std::runtime_error(SS("cannot open control for card " << card << ": " << snd_strerror(err)));
    }
    device = -1;
    for (;;)
    {
        if ((err = snd_ctl_rawmidi_next_device(ctl, &device)) < 0)
        {
            snd_ctl_close(ctl);
            throw std::runtime_error(SS("cannot get next rawmidi device for card " << card << ": " << snd_strerror(err)));
        }
        if (device < 0)
            break;
        AddMidiCardDevicesToList(ctl, card, device, direction, result);
    }
    snd_ctl_close(ctl);
}

static std::vector<AlsaMidiDeviceInfo> GetAlsaDevices(const char *devname, const char *direction_)
{
    std::vector<AlsaMidiDeviceInfo> result;

    int card, err;

    AlsaMidiDeviceInfo::Direction direction;
    if (strcmp(direction_, "Input") == 0)
    {
        direction = AlsaMidiDeviceInfo::In;
    }
    else if (strcmp(direction_, "Output") == 0)
    {
        direction = AlsaMidiDeviceInfo::Out;
    }
    else if (strcmp(direction_, "InOut") == 0)
    {
        direction = AlsaMidiDeviceInfo::InOut;
    }
    else
    {
        direction = AlsaMidiDeviceInfo::None;
        return result;
    }

    card = -1;
    if ((err = snd_card_next(&card)) < 0)
    {
        throw std::runtime_error(SS("snd_card_next failed.: " << snd_strerror(err)));
    }
    if (card < 0)
    {
        // no devices!
        return result;
    }
    do
    {
        AddMidiCardToList(card, &result, direction);
        if ((err = snd_card_next(&card)) < 0)
        {
            throw std::runtime_error(SS("snd_card_next failed: " << snd_strerror(err)));
        }
    } while (card >= 0);

    return result;
}
static std::vector<AlsaMidiDeviceInfo> OldGetAlsaDevices(const char *devname, const char *direction)
{
    std::vector<AlsaMidiDeviceInfo> result;
    {

        char **hints;
        int err;
        char **n;
        char *name;
        char *desc;
        char *ioid;

        /* Enumerate sound devices */
        err = snd_device_name_hint(-1, devname, (void ***)&hints);
        if (err != 0)
        {
            return result;
        }

        n = hints;
        while (*n != NULL)
        {

            name = snd_device_name_get_hint(*n, "NAME");
            desc = snd_device_name_get_hint(*n, "DESC");
            ioid = snd_device_name_get_hint(*n, "IOID");

            if (desc != nullptr) // skip virtual device
            {
                if (ioid == nullptr || strcmp(ioid, direction) == 0)
                {
                    result.push_back(AlsaMidiDeviceInfo(name, desc));
                }
            }
            // translate rawmidi device name to device number.

            if (name && strcmp("null", name) != 0)
                free(name);
            if (desc && strcmp("null", desc) != 0)
                free(desc);
            if (ioid && strcmp("null", ioid) != 0)
                free(ioid);
            n++;
        }

        // Free hint buffer too
        snd_device_name_free_hint((void **)hints);
    }
    return result;
}

std::vector<AlsaMidiDeviceInfo> pipedal::LegacyGetAlsaMidiInputDevices()
{
    return GetAlsaDevices("rawmidi", "Input");
}
std::vector<AlsaMidiDeviceInfo> pipedal::LegacyGetAlsaMidiOutputDevices()
{
    return GetAlsaDevices("rawmidi", "Output");
}

AlsaMidiDeviceInfo::AlsaMidiDeviceInfo(const char *name, const char *description)
    : name_(name)
{
    // extract just the display name from description.
    // undocumented but e.g.:   M2, M2\nM2 Raw Midi
    const char *p = description;
    const char *pEnd = p;
    // undocumented but e.g.:   M2, M2\nM2 Raw Midi
    while (pEnd != nullptr && *pEnd != 0 && *pEnd != ',' && *pEnd != '\n')
    {
        ++pEnd;
    }
    if (p != pEnd)
    {
        description_ = std::string(p, pEnd);
    }
    else
    {
        description = name;
    }
}

AlsaMidiDeviceInfo::AlsaMidiDeviceInfo(const char *name, const char *description, int card, int device, int subdevice)
    : AlsaMidiDeviceInfo(name, description)
{
    this->card_ = card;
    this->device_ = device;
    this->subdevice_ = subdevice;
}

std::unique_ptr<PiPedalAlsaDevices> PiPedalAlsaDevices::instance_;

PiPedalAlsaDevices&PiPedalAlsaDevices::instance() {
    if (!instance_)
    {
        instance_ = std::unique_ptr<PiPedalAlsaDevices>(new PiPedalAlsaDevices());
    }
    return (*instance_);
}


JSON_MAP_BEGIN(AlsaDeviceInfo)
JSON_MAP_REFERENCE(AlsaDeviceInfo, cardId)
JSON_MAP_REFERENCE(AlsaDeviceInfo, id)
JSON_MAP_REFERENCE(AlsaDeviceInfo, name)
JSON_MAP_REFERENCE(AlsaDeviceInfo, longName)
JSON_MAP_REFERENCE(AlsaDeviceInfo, deviceProfile)
JSON_MAP_REFERENCE(AlsaDeviceInfo, sampleRates)
JSON_MAP_REFERENCE(AlsaDeviceInfo, minBufferSize)
JSON_MAP_REFERENCE(AlsaDeviceInfo, maxBufferSize)
JSON_MAP_REFERENCE(AlsaDeviceInfo, supportsCapture)
JSON_MAP_REFERENCE(AlsaDeviceInfo, supportsPlayback)
JSON_MAP_REFERENCE(AlsaDeviceInfo, captureBusy)
JSON_MAP_REFERENCE(AlsaDeviceInfo, playbackBusy)
JSON_MAP_END()

JSON_MAP_BEGIN(AlsaMidiDeviceInfo)
JSON_MAP_REFERENCE(AlsaMidiDeviceInfo, name)
JSON_MAP_REFERENCE(AlsaMidiDeviceInfo, description)
JSON_MAP_END()
