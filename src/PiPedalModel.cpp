// Copyright (c) 2022-2024 Robin Davies
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
#include <future>
#include <iomanip>
#include "ServiceConfiguration.hpp"
#include "AudioConfig.hpp"
#include "ConfigUtil.hpp"
#include <sched.h>
#include "PiPedalModel.hpp"
#include "AudioHost.hpp"
#include "Lv2Log.hpp"
#include <set>
#include "PiPedalConfiguration.hpp"
#include "AdminClient.hpp"
#include "SplitEffect.hpp"
#include "CpuGovernor.hpp"
#include "RegDb.hpp"
#include "RingBufferReader.hpp"
#include "PiPedalUI.hpp"
#include "atom_object.hpp"
#include "Lv2PluginChangeMonitor.hpp"
#include "HotspotManager.hpp"
#include "DBusToLv2Log.hpp"
#include "SysExec.hpp"
#include "Updater.hpp"
#include "util.hpp"
#include "DBusLog.hpp"
#include "AvahiService.hpp"
#include "DummyAudioDriver.hpp"
#include "AudioFiles.hpp"
#include "CrashGuard.hpp"
#include "Tone3000Downloader.hpp"
#include "HtmlHelper.hpp"

#ifndef NO_MLOCK
#include <sys/mman.h>
#endif /* NO_MLOCK */

using namespace pipedal;
namespace fs = std::filesystem;

template <typename T>
T &constMutex(const T &mutex)
{
    return const_cast<T &>(mutex);
}

static const char *hexChars = "0123456789ABCDEF";

static std::string BytesToHex(const std::vector<uint8_t> &bytes)
{
    std::stringstream s;

    for (size_t i = 0; i < bytes.size(); ++i)
    {
        uint8_t b = bytes[i];
        s << hexChars[(b >> 4) & 0x0F] << hexChars[b & 0x0F];
    }
    return s.str();
}

PiPedalModel::PiPedalModel()
    : pluginHost(),
      atomConverter(pluginHost.GetMapFeature())
{
    this->updater = Updater::Create();
    this->currentUpdateStatus = updater->GetCurrentStatus();
    this->pedalboard = Pedalboard::MakeDefault();

    this->jackServerSettings = this->storage.GetJackServerSettings();

    updater->SetUpdateListener(
        [this](const UpdateStatus &updateStatus)
        {
            this->OnUpdateStatusChanged(updateStatus);
        });

    DbusLogToLv2Log();
    SetDBusLogLevel(DBusLogLevel::Info);

    hotspotManager = HotspotManager::Create();
    hotspotManager->SetNetworkChangingListener(
        [this](bool ethernetConnected, bool hotspotEnabling)
        {
            OnNetworkChanging(ethernetConnected, hotspotEnabling);
        });
    hotspotManager->SetHasWifiListener(
        [this](bool hasWifi)
        {
            this->SetHasWifi(hasWifi);
        });
    // don't actuall start the hotspotManager until after LV2 is initialized (in order to avoid logging oddities)
}

void PrepareSnapshostsForSave(Pedalboard &pedalboard)
{
    if (pedalboard.selectedSnapshot() != -1)
    {
        auto &currentSnapshot = pedalboard.snapshots()[pedalboard.selectedSnapshot()];
        if (!currentSnapshot || currentSnapshot->isModified_)
        {
            pedalboard.selectedSnapshot(-1);
        }
    }
    for (auto &snapshot : pedalboard.snapshots())
    {
        if (snapshot)
        {
            snapshot->isModified_ = false;
        }
    }
}

void PiPedalModel::Close()
{
    std::unique_ptr<AudioHost> oldAudioHost;
    std::unique_ptr<GpioManager> oldGpioManager;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex);

        if (closed)
        {
            return;
        }
        if (tone3000Downloader)
        {
            tone3000Downloader->Close();
        }
        closed = true;

        oldGpioManager = std::move(gpioManager);

        CancelAudioRetry();

        if (avahiService)
        {
            this->avahiService = nullptr; // and close.
        }

        // take a snapshot incase a client unsusbscribes in the notification handler (in which case the mutex won't protect us)
        std::vector<IPiPedalModelSubscriber::ptr> t{subscribers.begin(), subscribers.end()};
        for (auto &subscriber : t)
        {
            subscriber->Close();
        }
        this->subscribers.resize(0);

        oldAudioHost = std::move(this->audioHost);
    } // end lock.

    if (oldGpioManager)
    {
        oldGpioManager->Close();
    }

    // lockless to avoid deadlocks while shutting down the audio thread.
    if (oldAudioHost)
    {
        oldAudioHost->Close();
    }
}

PiPedalModel::~PiPedalModel()
{
    if (gpioManager)
    {
        gpioManager->Close();
        gpioManager = nullptr;
    }
    CancelNetworkChangingTimer();
    hotspotManager = nullptr; // turn off the hotspot.

    pluginChangeMonitor = nullptr; // stop monitorin LV2 directories.
    try
    {
        adminClient.UnmonitorGovernor();
    }
    catch (...) // noexcept here!
    {
    }

    try
    {
        CurrentPreset currentPreset;
        currentPreset.modified_ = this->hasPresetChanged;
        currentPreset.preset_ = this->pedalboard;
        storage.SaveCurrentPreset(currentPreset);
    }
    catch (...)
    {
    }

    try
    {
        if (audioHost)
        {
            audioHost->Close();
        }
    }
    catch (...)
    {
    }
}

#include <fstream>

void PiPedalModel::Init(const PiPedalConfiguration &configuration)
{
    std::lock_guard<std::recursive_mutex> lock(mutex); // prevent callbacks while we're initializing.
    if (updaterEnabled)
    {
        this->updater->Start();
    }

    this->configuration = configuration;
    pluginHost.SetConfiguration(configuration);
    storage.SetConfigRoot(configuration.GetDocRoot());
    storage.SetDataRoot(configuration.GetLocalStoragePath());
    storage.Initialize(this);
    pluginHost.SetPluginStoragePath(storage.GetPluginUploadDirectory());

    this->systemMidiBindings = storage.GetSystemMidiBindings();
    this->gpioSettings = storage.GetGpioSettings();
    if (this->gpioSettings.EnsureStandardEncoderRoles())
    {
        try
        {
            storage.SetGpioSettings(this->gpioSettings);
        }
        catch (const std::exception &e)
        {
            Lv2Log::warning(SS("Unable to save migrated GPIO encoder roles. " << e.what()));
        }
    }

    this->jackServerSettings = storage.GetJackServerSettings();
    try
    {
        this->jackConfiguration.AlsaInitialize(jackServerSettings);
    }
    catch (const std::exception &)
    {
        //
    }
    RefreshCodecZeroPresence();

    this->channelRouterSettings = storage.GetChannelRouterSettings();
    pluginHost.OnConfigurationChanged(
        jackConfiguration,
        storage.GetChannelSelection());
}

int64_t PiPedalModel::DownloadModelsFromTone3000(
    const std::string &uri,
    const Tone3000PkceParams &pckeParams,
    const std::string &downloadPath,
    Tone3000DownloadType downloadType)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);

    // Validate the download path
    std::filesystem::path path{downloadPath};
    if (!IsInUploadsDirectory(path))
    {
        throw PiPedalException("Invalid path: not in uploads directory.");
    }

    // Check for ".." in path components (security check)
    for (const auto &component : path)
    {
        if (component == "..")
        {
            throw PiPedalException("Invalid path: contains '..' component.");
        }
    }

    if (!tone3000Downloader)
    {
        tone3000Downloader = Tone3000Downloader::Create();
        tone3000Downloader->SetListener(this);
    }
    return tone3000Downloader->RequestTone3000Download(
        uri,
        pckeParams,
        downloadPath,
        downloadType);
}

void PiPedalModel::CancelTone3000Download(
    int64_t clientId,
    int64_t downloadHandle)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);

    if (tone3000Downloader)
    {
        tone3000Downloader->CancelDownload(downloadHandle);
    }
}

// Tone3000Downloader::Listener implementation
void PiPedalModel::OnStartTone3000Download(int64_t handle, const std::string &title)
{

    SubscriberList subscribers;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        subscribers = this->subscribers;
    }
    for (auto &subscriber : this->subscribers)
    {
        subscriber->OnTone3000DownloadStarted(handle, title);
    }
}

void PiPedalModel::OnTone3000Progress(const Tone3000DownloadProgress &progress)
{
    SubscriberList subscribers;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        subscribers = this->subscribers;
    }
    for (auto &subscriber : subscribers)
    {
        subscriber->OnTone3000DownloadProgress(progress);
    }
}

void PiPedalModel::OnTone3000DownloadComplete(int64_t handle, const std::string &resultPath)
{
    SubscriberList subscribers;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        subscribers = this->subscribers;
    }
    for (auto subscriber : subscribers)
    {
        subscriber->OnTone3000DownloadComplete(handle, resultPath);
    }
}

void PiPedalModel::OnTone3000DownloadError(int64_t handle, const std::string &errorMessage)
{
    SubscriberList subscribers;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        subscribers = this->subscribers;
    }
    for (auto subscriber : subscribers)
    {
        subscriber->OnTone3000DownloadError(handle, errorMessage);
    }
}

void PiPedalModel::LoadLv2PluginInfo()
{
    // Not all Lv2 directories have the lv2 base declarations. Load a full set of plugin classes generated on a default /usr/local/lib/lv2 directory.
    std::filesystem::path pluginClassesPath = configuration.GetDocRoot() / "plugin_classes.json";
    try
    {
        if (!std::filesystem::exists(pluginClassesPath))
            throw PiPedalException("File not found.");
        pluginHost.LoadPluginClassesFromJson(pluginClassesPath);
    }
    catch (const std::exception &e)
    {
        std::stringstream s;
        s << "Unable to load " << pluginClassesPath << ". " << e.what();
        throw PiPedalException(s.str().c_str());
    }

    pluginChangeMonitor = std::make_unique<Lv2PluginChangeMonitor>(*this);
    pluginHost.LoadLilv(configuration.GetLv2Path().c_str());

    // Copy all presets out of Lilv data to json files
    // so that we can close lilv while we're actually
    // running.

    uint64_t pluginPresetIndexVersion = storage.GetPluginPresetIndexVersion();
    for (const auto &plugin : pluginHost.GetPlugins())
    {
        if (plugin->has_factory_presets())
        {
            if (!storage.HasPluginPresets(plugin->uri()))
            {
                PluginPresets pluginPresets = pluginHost.GetFactoryPluginPresets(plugin->uri());
                storage.SavePluginPresets(plugin->uri(), pluginPresets);
            }
            else
            {
                if (pluginPresetIndexVersion == 0)
                {
                    if (plugin->uri() == "http://two-play.com/plugins/toob-convolution-reverb" || plugin->uri() == "http://two-play.com/plugins/toob-convolution-reverb-stereo")
                    {
                        // overwrite previous factory presets!
                        PluginPresets pluginPresets = pluginHost.GetFactoryPluginPresets(plugin->uri());
                        for (auto &pluginPreset : pluginPresets.presets_)
                        {
                            storage.SavePluginPreset(
                                plugin->uri(),
                                pluginPreset);
                        }
                    }
                }
            }
        }
    }
    storage.SetPluginPresetIndexVersion(1);
}
void PiPedalModel::Load()
{
    this->webRoot = configuration.GetWebRoot();
    this->webPort = (uint16_t)configuration.GetSocketServerPort();

    adminClient.MonitorGovernor(storage.GetGovernorSettings());

    // pluginHost.Load(configuration.GetLv2Path().c_str());

    this->pedalboard = storage.GetCurrentPreset(); // the current *saved* preset.

    // the current edited preset, saved only across orderly shutdowns.

    CrashGuard::SetCrashGuardFileName(storage.GetDataRoot() / "crash_guard.data");

    if (CrashGuard::HasCrashed())
    {
        // ignore the current preset, and load a blank pedalboard in order to avoid a potential plugin crash.
        this->pedalboard = Pedalboard::MakeDefault();
    }
    else
    {
        CurrentPreset currentPreset;
        try
        {
            if (storage.RestoreCurrentPreset(&currentPreset))
            {
                this->pedalboard = currentPreset.preset_;
                this->UpdateDefaults(&pedalboard);
                this->hasPresetChanged = currentPreset.modified_;
            }
        }
        catch (const std::exception &e)
        {
            Lv2Log::warning(SS("Failed to load current preset. " << e.what()));
        }
    }

    UpdateDefaults(&this->pedalboard);

    std::unique_ptr<AudioHost> p{AudioHost::CreateInstance(pluginHost.asIHost())};
    this->audioHost = std::move(p);

    this->audioHost->SetNotificationCallbacks(this);

    this->systemMidiBindings = storage.GetSystemMidiBindings();

    this->audioHost->SetSystemMidiBindings(this->systemMidiBindings);

    audioHost->SetAlsaSequencerConfiguration(storage.GetAlsaSequencerConfiguration());

    if (configuration.GetMLock())
    {
#ifndef NO_MLOCK
        int result = mlockall(MCL_CURRENT | MCL_FUTURE);
        if (result)
        {
            throw PiPedalStateException("mlockall failed. You can disable the call to mlockall  in 'config.json'.");
        }

#endif
    }

    {
        // The AirPlay receiver always starts off. Leaving it enabled across a
        // restart advertises a receiver nobody asked for, and a sender that was
        // mid-session when the Pi went down comes back to a half-open session
        // rather than a clean one. Volume and output routing are remembered.
        AirplaySettings airplaySettings = storage.GetAirplaySettings();
        audioHost->SetAirplayVolume(airplaySettings.volume_);
        audioHost->SetAirplayOutputChannel(airplaySettings.outputChannel_);
        audioHost->SetAirplayStreamEnabled(false);
        if (airplaySettings.enabled_)
        {
            airplaySettings.enabled_ = false;
            try
            {
                storage.SetAirplaySettings(airplaySettings);
                UpdateAirplayServiceConfiguration(airplaySettings);
            }
            catch (const std::exception &e)
            {
                Lv2Log::warning(SS("Unable to stop the AirPlay receiver at startup. " << e.what()));
            }
        }
    }

    // Device publication can lag service initialization during boot. Refresh
    // after WaitForAudioDeviceToComeOnline() and before any GPIO is opened.
    RefreshCodecZeroPresence();
    RestartAudio();

    gpioManager = GpioManager::Create();
    gpioManager->SetEventCallback(
        [this](const GpioInputEvent &event)
        {
            try
            {
                Post(
                    [this, event]()
                    {
                        if (!closed)
                        {
                            HandleGpioInputEvent(event);
                        }
                    });
            }
            catch (const std::exception &e)
            {
                Lv2Log::warning(SS("Unable to dispatch GPIO input. " << e.what()));
            }
        });
    gpioManager->SetStatusCallback(
        [this](const GpioInputStatus &status)
        {
            try
            {
                Post(
                    [this, status]()
                    {
                        if (!closed)
                        {
                            FireGpioInputStatusChanged(status);
                        }
                    });
            }
            catch (const std::exception &)
            {
            }
        });
    AudioHost *waveformHost = audioHost.get();
    gpioManager->SetWaveformProvider(
        [waveformHost](bool output, std::array<float, 128> *values)
        {
            return waveformHost && waveformHost->GetWaveform(output, values);
        });
    gpioManager->SetTunerSampleProvider(
        [waveformHost](
            uint64_t *readIndex,
            float *values,
            size_t capacity,
            uint32_t *sampleRate)
        {
            return waveformHost
                       ? waveformHost->ReadGpioTunerInput(
                             readIndex, values, capacity, sampleRate)
                       : 0;
        });
    ConfigureGpioManager();
    UpdateGpioDashboard();
}

IPiPedalModelSubscriber *PiPedalModel::GetNotificationSubscriber(int64_t clientId)
{
    for (size_t i = 0; i < subscribers.size(); ++i)
    {
        if (subscribers[i]->GetClientId() == clientId)
        {
            return subscribers[i].get();
        }
    }
    return nullptr;
}

void PiPedalModel::AddNotificationSubscription(std::shared_ptr<IPiPedalModelSubscriber> pSubscriber)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    this->subscribers.push_back(pSubscriber);
}
void PiPedalModel::RemoveNotificationSubsription(std::shared_ptr<IPiPedalModelSubscriber> pSubscriber)
{
    {
        std::lock_guard<std::recursive_mutex> lock(mutex);

        for (auto it = this->subscribers.begin(); it != this->subscribers.end(); ++it)
        {
            if ((*it).get() == pSubscriber.get())
            {
                this->subscribers.erase(it);
                break;
            }
        }
        int64_t clientId = pSubscriber->GetClientId();

        this->DeleteMidiListeners(clientId);
        this->DeleteAtomOutputListeners(clientId);

        for (int i = 0; i < this->outstandingParameterRequests.size(); ++i)
        {
            if (outstandingParameterRequests[i]->clientId == clientId)
            {
                outstandingParameterRequests.erase(outstandingParameterRequests.begin() + i);
                --i;
            }
        }
    }
}

void PiPedalModel::PreviewControl(int64_t clientId, int64_t pedalItemId, const std::string &symbol, float value)
{
    IEffect *effect = lv2Pedalboard->GetEffect(pedalItemId);
    if (!effect)
    {
        return;
    }
    if (effect->IsVst3())
    {
        int index = lv2Pedalboard->GetControlIndex(pedalItemId, symbol);
        if (index != -1)
        {
            effect->SetControl(index, value);
        }
    }
    else
    {
        audioHost->SetControlValue(pedalItemId, symbol, value);
    }
}

// we were explicitly told that the state was changed by the plugin
void PiPedalModel::OnNotifyLv2StateChanged(uint64_t instanceId)
{
    // a sent PATCH_Set, or an explicit state changed notification.
    OnNotifyMaybeLv2StateChanged(instanceId);
}

// The plugin notified us that a  path path property changed. The state *purrobably changed.
bool PiPedalModel::OnNotifyMaybeLv2StateChanged(uint64_t instanceId)
{
    // one or more received PATCH_Sets, which MAY change the state.
    std::lock_guard<std::recursive_mutex> lock(mutex);
    PedalboardItem *item = pedalboard.GetItem(instanceId);
    if (item != nullptr)
    {
        if (!audioHost)
        {
            return false;
        }
        bool changed = this->audioHost->UpdatePluginState(*item);
        if (changed)
        {

            item->stateUpdateCount(item->stateUpdateCount() + 1);

            Lv2PluginState newState = item->lv2State();

            FireLv2StateChanged(instanceId, newState);
            this->SetPresetChanged(-1, true, false);
            return true;
        }
    }
    return false;
}

void PiPedalModel::SetInputVolume(float value)
{
    PreviewInputVolume(value);
    {
        SubscriberList subscribers;
        {
            std::lock_guard<std::recursive_mutex> lock(mutex);
            subscribers = this->subscribers;

            this->pedalboard.input_volume_db(value);
        }
        // take a snapshot incase a client unsusbscribes in the notification handler (in which case the mutex won't protect us)
        for (auto &subscriber : subscribers)
        {
            subscriber->OnInputVolumeChanged(value);
        }

        this->SetPresetChanged(-1, true);
    }
}
void PiPedalModel::SetOutputVolume(float value)
{
    PreviewOutputVolume(value);
    {
        SubscriberList subscribers;
        {
            std::lock_guard<std::recursive_mutex> lock(mutex);
            subscribers = this->subscribers;

            this->pedalboard.output_volume_db(value);
        }
        for (auto &subscriber : subscribers)
        {
            subscriber->OnOutputVolumeChanged(value);
        }

        this->SetPresetChanged(-1, true);
    }
}
void PiPedalModel::PreviewInputVolume(float value)
{
    audioHost->SetInputVolume(value);
}
void PiPedalModel::PreviewOutputVolume(float value)
{
    audioHost->SetOutputVolume(value);
}

void PiPedalModel::SetControl(int64_t clientId, int64_t pedalItemId, const std::string &symbol, float value)
{
    SubscriberList subscribers;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        subscribers = this->subscribers;

        if (!this->pedalboard.SetControlValue(pedalItemId, symbol, value))
        {
            return;
        }

        PedalboardItem *item = pedalboard.GetItem(pedalItemId);

        // change of split type requires rebuild of the effect
        // since it can change the number of output channels.
        if (item != nullptr && item->isSplit() && symbol == "splitType")
        {
            this->FirePedalboardChanged(clientId);
            return;
        }
        PreviewControl(clientId, pedalItemId, symbol, value);
    }

    for (auto &subscriber : subscribers)
    {
        subscriber->OnControlChanged(clientId, pedalItemId, symbol, value);
    }

    this->SetPresetChanged(clientId, true);

    // A change from anywhere -- hardware, web interface, or a MIDI binding --
    // refreshes the OLED when it is one of the four parameters on screen.
    bool updateDashboard = false;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        const GpioParameter changed{pedalItemId, symbol};
        updateDashboard = std::find(
            gpioShownParameters.begin(), gpioShownParameters.end(), changed) !=
            gpioShownParameters.end();
    }
    if (updateDashboard)
        UpdateGpioDashboard();
}

void PiPedalModel::FireJackConfigurationChanged(const JackConfiguration &jackConfiguration)
{

    SubscriberList subscribers;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        subscribers = this->subscribers;
    }

    // noify subscribers.

    for (auto &subscriber : subscribers)
    {
        subscriber->OnJackConfigurationChanged(jackConfiguration);
    }
}

void PiPedalModel::FireBanksChanged(int64_t clientId)
{
    SubscriberList subscribers;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        subscribers = this->subscribers;
    }
    // noify subscribers.
    for (auto &subscriber : subscribers)
    {
        subscriber->OnBankIndexChanged(this->storage.GetBanks());
    }
}

void PiPedalModel::FirePedalboardChanged(int64_t clientId, bool loadAudioThread)
{
    SubscriberList subscribers;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        subscribers = this->subscribers;
        RemoveUnreachableGpioBindings();

        if (loadAudioThread)
        {
            // notify the audio thread.
            if (audioHost && audioHost->IsOpen())
            {
                LoadCurrentPedalboard();

                UpdateRealtimeVuSubscriptions();
                UpdateRealtimeMonitorPortSubscriptions();
            }
        }
    }
    // noify subscribers.
    for (auto &subscriber : subscribers)
    {
        subscriber->OnPedalboardChanged(clientId, this->pedalboard);
    }
    // Re-apply physical analog and maintained switch positions using the new
    // preset's mappings. Trigger/toggle bindings ignore refresh events.
    {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        if (gpioManager)
        {
            gpioManager->Refresh();
        }
    }
    UpdateGpioDashboard();
}
void PiPedalModel::SetPedalboard(int64_t clientId, Pedalboard &pedalboard)
{
    {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        CarryGpioScrollForward(pedalboard);
        this->pedalboard = pedalboard;
        UpdateDefaults(&this->pedalboard);
    }
    this->FirePedalboardChanged(clientId);
    this->SetPresetChanged(clientId, true);
}

void PiPedalModel::SetSnapshot(int64_t selectedSnapshot)
{
    bool pedalboardChanged = false;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        if (this->pedalboard.ApplySnapshot(selectedSnapshot, pluginHost))
        {
            this->pedalboard.selectedSnapshot(selectedSnapshot);
            for (auto snapshot : this->pedalboard.snapshots())
            {
                if (snapshot)
                {
                    snapshot->isModified_ = false;
                }
            }
            pedalboardChanged = true;
        } else if (selectedSnapshot == -1) 
        {
            this->pedalboard.selectedSnapshot(-1);
            pedalboardChanged = true;
        }

    }
    if (pedalboardChanged)
    {
        FirePedalboardChanged(-1, true);
    }
}

void PiPedalModel::SetSnapshots(std::vector<std::shared_ptr<Snapshot>> &snapshots, int64_t selectedSnapshot)
{
    {
        std::lock_guard<std::recursive_mutex> lock(mutex);

        UpdateVst3Settings(pedalboard);

        this->pedalboard.snapshots(std::move(snapshots));

        if (selectedSnapshot != -1)
        {
            this->pedalboard.selectedSnapshot(selectedSnapshot);
            for (auto &snapshot : pedalboard.snapshots())
            {
                if (snapshot)
                {
                    snapshot->isModified_ = false;
                }
            }
        }
    }

    this->FirePedalboardChanged(-1, false); // notify clients (but don't change the running pedalboard, because it's still the same)
                                            // this means that all clients get an up-to-date copy of the snapshots AND the currently selected snapshot if that applies
                                            // (and a fresh copy of the pedalboard settings as well, which is harmless, since they have not changed)
    this->SetPresetChanged(-1, true, false);
}

void PiPedalModel::UpdateCurrentPedalboard(int64_t clientId, Pedalboard &pedalboard)
{
    {
        std::lock_guard<std::recursive_mutex> lock(mutex);

        // update vst3 presets if neccessary.
        // the pedalboard must be a manipualted instance of the current Lv2Pedalboard.

        UpdateVst3Settings(pedalboard);
        CarryGpioScrollForward(pedalboard);

        Lv2PedalboardErrorList errorMessages;
        std::shared_ptr<Lv2Pedalboard> lv2Pedalboard{
            this->pluginHost.UpdateLv2PedalboardStructure(pedalboard, this->lv2Pedalboard.get(), errorMessages)};
        this->lv2Pedalboard = lv2Pedalboard;

        // apply the error messages to the lv2Pedalboard.
        // return true if the error messages have changed
        audioHost->SetPedalboard(lv2Pedalboard);
        this->pedalboard = pedalboard;
        previousPedalboard = this->pedalboard;
        previousPedalboardLoaded = true;
        this->pedalboard = pedalboard;

        UpdateRealtimeVuSubscriptions();
        UpdateRealtimeMonitorPortSubscriptions();
    }
    this->FirePedalboardChanged(clientId, false);
    this->SetPresetChanged(clientId, true);
}

void PiPedalModel::SetPedalboardItemUseModUi(int64_t clientId, int64_t instanceId, bool enabled)
{
    std::lock_guard<std::recursive_mutex> guard{mutex};
    {
        this->pedalboard.SetItemUseModUi(instanceId, enabled);

        // Notify clients.
        std::vector<IPiPedalModelSubscriber::ptr> t{subscribers.begin(), subscribers.end()};
        for (auto &subscriber : t)
        {
            subscriber->OnItemUseModUiChanged(clientId, instanceId, enabled);
        }
    }
    this->SetPresetChanged(clientId, true);
}

void PiPedalModel::SetPedalboardItemEnable(int64_t clientId, int64_t pedalItemId, bool enabled)
{
    SubscriberList subscribers;
    std::lock_guard<std::recursive_mutex> guard{mutex};
    {
        subscribers = this->subscribers;

        this->pedalboard.SetItemEnabled(pedalItemId, enabled);
        PedalboardItem *pPedalboardItem = this->pedalboard.GetItem(pedalItemId);
        if (pPedalboardItem)
        {
            Lv2PluginInfo::ptr pluginInfo = GetPluginInfo(pPedalboardItem->uri());
            if (pluginInfo)
            {
                for (auto &port : pluginInfo->ports())
                {
                    if (port->is_bypass())
                    {
                        pPedalboardItem->SetControlValue(port->symbol(), enabled ? 1.0 : 0.0f);
                    }
                }
            }
        }
        // Notify audo thread.
    }
    this->audioHost->SetBypass(pedalItemId, enabled);

    // Notify clients.
    for (auto &subscriber : subscribers)
    {
        subscriber->OnItemEnabledChanged(clientId, pedalItemId, enabled);
    }
    this->SetPresetChanged(clientId, true);
}

void PiPedalModel::GetPresets(PresetIndex *pResult)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);

    this->storage.GetPresetIndex(pResult);
    pResult->presetChanged(this->hasPresetChanged);
}

Pedalboard PiPedalModel::GetPreset(int64_t instanceId)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    return this->storage.GetPreset(instanceId);
}
void PiPedalModel::GetBank(int64_t instanceId, BankFile *pResult)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    this->storage.GetBankFile(instanceId, pResult);
}

void PiPedalModel::SetPresetChanged(int64_t clientId, bool value, bool changeSnapshotSelect)
{
    if (changeSnapshotSelect && value && this->pedalboard.selectedSnapshot() != -1)
    {
        auto &snapshot = this->pedalboard.snapshots()[pedalboard.selectedSnapshot()];
        if (snapshot)
        {
            if (!snapshot->isModified_)
            {
                snapshot->isModified_ = true;
                FireSnapshotModified(pedalboard.selectedSnapshot(), true);
            }
        }

        this->pedalboard.SetCurrentSnapshotModified(true);
    }
    if (value != this->hasPresetChanged)
    {
        hasPresetChanged = value;
        FirePresetChanged(value);
    }
}

void PiPedalModel::FireSnapshotModified(int64_t snapshotIndex, bool modified)
{
    SubscriberList subscribers;
    std::lock_guard<std::recursive_mutex> guard{mutex};
    {
        subscribers = this->subscribers;
    }
    for (auto &subscriber : subscribers)
    {
        subscriber->OnSnapshotModified(snapshotIndex, modified);
    }
}

void PiPedalModel::FireSelectedSnapshotChanged(int64_t selectedSnapshot)
{
    SubscriberList subscribers;
    std::lock_guard<std::recursive_mutex> guard{mutex};
    {
        subscribers = this->subscribers;
        // take a snapshot incase a client unsusbscribes in the notification handler (in which case the mutex won't protect us)
    }
    for (auto &subscriber : subscribers)
    {
        subscriber->OnSelectedSnapshotChanged(selectedSnapshot);
    }
}

void PiPedalModel::FirePresetChanged(bool changed)
{
    SubscriberList subscribers;
    PresetIndex presets;
    std::lock_guard<std::recursive_mutex> guard{mutex};
    {
        subscribers = this->subscribers;
        // take a snapshot incase a client unsusbscribes in the notification handler (in which case the mutex won't protect us)

        GetPresets(&presets);
    }
    for (auto &subscriber : subscribers)
    {
        subscriber->OnPresetChanged(changed);
    }
}

void PiPedalModel::FirePresetsChanged(int64_t clientId)
{
    SubscriberList subscribers;
    PresetIndex presets;
    std::lock_guard<std::recursive_mutex> guard{mutex};
    {
        subscribers = this->subscribers;
        GetPresets(&presets);
    }
    for (auto &subscriber : subscribers)
    {
        subscriber->OnPresetsChanged(clientId, presets);
    }
}
void PiPedalModel::FirePluginPresetsChanged(const std::string &pluginUri)
{
    SubscriberList subscribers;
    std::lock_guard<std::recursive_mutex> guard{mutex};
    {
        subscribers = this->subscribers;
        // take a snapshot incase a client unsusbscribes in the notification handler (in which case the mutex won't protect us)
    }
    for (auto &subscriber : subscribers)
    {
        subscriber->OnPluginPresetsChanged(pluginUri);
    }
}

void PiPedalModel::UpdateVst3Settings(Pedalboard &pedalboard)
{
    // get the vst3 state bundle from lv2Pedalboard for the current pedalboard.
#if ENABLE_VST3
    Pedalboard pb;
    for (IEffect *effect : lv2Pedalboard->GetEffects())
    {
        if (effect->IsVst3())
        {
            PedalboardItem *item = pedalboard.GetItem(effect->GetInstanceId());
            if (item)
            {
                Vst3Effect *vst3Effect = (Vst3Effect *)effect;
                std::vector<uint8_t> state;
                if (vst3Effect->GetState(&state))
                {
                    item->vstState(BytesToHex(state));
                }
            }
        }
    }
#endif
}

void PiPedalModel::FireLv2StateChanged(int64_t instanceId, const Lv2PluginState &lv2State)
{
    SubscriberList subscribers;
    {
        std::lock_guard<std::recursive_mutex> guard{mutex};
        subscribers = this->subscribers;
    }

    for (auto &subscriber : subscribers)
    {
        subscriber->OnLv2StateChanged(instanceId, lv2State);
    }
}

// referesh the plugin state for all plugins.
bool PiPedalModel::SyncLv2State()
{
    bool changed = false;
    auto pedalboardItems = pedalboard.GetAllPlugins();
    for (PedalboardItem *item : pedalboardItems)
    {
        if (!item->isSplit())
        {
            if (audioHost->UpdatePluginState(*item))
            {
                FireLv2StateChanged(item->instanceId(), item->lv2State());
                changed = true;
            }
        }
    }
    return changed;
}
void PiPedalModel::SaveCurrentPreset(int64_t clientId)
{
    std::lock_guard<std::recursive_mutex> guard{mutex};

    UpdateVst3Settings(this->pedalboard);
    SyncLv2State();
    PrepareSnapshostsForSave(pedalboard);

    storage.SaveCurrentPreset(this->pedalboard);
    this->SetPresetChanged(clientId, false);
}

uint64_t PiPedalModel::CopyPluginPreset(const std::string &pluginUri, uint64_t presetId)
{
    uint64_t result = storage.CopyPluginPreset(pluginUri, presetId);
    FirePluginPresetsChanged(pluginUri);
    return result;
}

void PiPedalModel::UpdatePluginPresets(const PluginUiPresets &pluginPresets)
{
    storage.UpdatePluginPresets(pluginPresets);
    FirePluginPresetsChanged(pluginPresets.pluginUri_);
}
int64_t PiPedalModel::SavePluginPresetAs(int64_t instanceId, const std::string &name)
{
    PedalboardItem *item = this->pedalboard.GetItem(instanceId);
    if (!item)
    {
        throw PiPedalException("Plugin not found.");
    }
    uint64_t presetId = storage.SavePluginPreset(name, *item);
    FirePluginPresetsChanged(item->uri());
    return presetId;
}

int64_t PiPedalModel::SaveCurrentPresetAs(int64_t clientId, int64_t bankInstanceId, const std::string &name, int64_t saveAfterInstanceId)
{
    std::lock_guard<std::recursive_mutex> guard{mutex};

    SyncLv2State();
    auto pedalboard = this->pedalboard.DeepCopy();
    PrepareSnapshostsForSave(pedalboard);

    UpdateVst3Settings(pedalboard);
    pedalboard.name(name);
    int64_t result = storage.SaveCurrentPresetAs(pedalboard, bankInstanceId, name, saveAfterInstanceId);
    FirePresetsChanged(clientId);
    return result;
}

void PiPedalModel::UploadPluginPresets(const PluginPresets &pluginPresets)
{
    if (pluginPresets.pluginUri_.length() == 0)
    {
        throw PiPedalException("Invalid plugin presets.");
    }
    std::lock_guard<std::recursive_mutex> guard{mutex};
    storage.MergePluginPresets(pluginPresets.pluginUri_, pluginPresets);
    FirePluginPresetsChanged(pluginPresets.pluginUri_);
}
int64_t PiPedalModel::UploadPreset(const BankFile &bankFile, int64_t uploadAfter)
{
    std::lock_guard<std::recursive_mutex> guard{mutex};

    int64_t newPreset = this->storage.UploadPreset(bankFile, uploadAfter);
    FirePresetsChanged(-1);
    return newPreset;
}
int64_t PiPedalModel::UploadBank(BankFile &bankFile, int64_t uploadAfter)
{
    std::lock_guard<std::recursive_mutex> guard{mutex};

    int64_t newPreset = this->storage.UploadBank(bankFile, uploadAfter);
    FireBanksChanged(-1);
    return newPreset;
}

void PiPedalModel::NextBank(Direction direction)
{
    std::lock_guard<std::recursive_mutex> guard{mutex};

    auto bankIndex = this->GetBankIndex();
    if (bankIndex.entries().size() == 0)
    {
        return;
    }
    size_t index = 0;
    for (size_t i = 0; i < bankIndex.entries().size(); ++i)
    {
        auto &entry = bankIndex.entries()[i];
        if (entry.instanceId() == bankIndex.selectedBank())
        {
            index = i;
            break;
        }
    }
    if (direction == Direction::Increase)
    {
        ++index;
        if (index >= bankIndex.entries().size())
        {
            index = 0;
        }
    }
    else
    {
        if (index == 0)
        {
            index = bankIndex.entries().size() - 1;
        }
        else
        {
            --index;
        }
    }
    this->OpenBank(-1, bankIndex.entries()[index].instanceId());
}
void PiPedalModel::NextPreset(Direction direction)
{
    PresetIndex index;

    storage.GetPresetIndex(&index);
    auto currentPresetId = storage.GetCurrentPresetId();
    size_t currentPresetIndex = 0;

    for (size_t i = 0; i < index.presets().size(); ++i)
    {
        if (index.presets()[i].instanceId() == currentPresetId)
        {
            currentPresetIndex = i;
            break;
        }
    }
    if (index.presets().size() == 0)
    {
        return;
    }
    if (direction == Direction::Decrease)
    {
        if (currentPresetIndex == 0)
        {
            currentPresetIndex = index.presets().size() - 1;
        }
        else
        {
            --currentPresetIndex;
        }
    }
    else
    {
        ++currentPresetIndex;
        if (currentPresetIndex >= index.presets().size())
        {
            currentPresetIndex = 0;
        }
    }
    LoadPreset(-1, index.presets()[currentPresetIndex].instanceId());
}

void PiPedalModel::NextSnapshot(Direction direction)
{
    std::lock_guard<std::recursive_mutex> guard{mutex};

    auto &snapshots = this->pedalboard.snapshots();
    if (snapshots.size() == 0)
    {
        return;
    }
    int64_t currentSnapshot = this->pedalboard.selectedSnapshot();
    int64_t nextSnapshot = -1;

    if (direction == Direction::Increase)
    {
        for (int64_t i = currentSnapshot + 1; i < (int64_t)snapshots.size(); ++i)
        {
            if (snapshots[i])
            {
                nextSnapshot = i;
                break;
            }
        }
        if (nextSnapshot == -1)
        {
            for (int64_t i = 0; i <= currentSnapshot && i < (int64_t)snapshots.size(); ++i)
            {
                if (snapshots[i])
                {
                    nextSnapshot = i;
                    break;
                }
            }
        }
    }
    else
    {
        for (int64_t i = currentSnapshot - 1; i >= 0; --i)
        {
            if (snapshots[i])
            {
                nextSnapshot = i;
                break;
            }
        }
        if (nextSnapshot == -1)
        {
            for (int64_t i = (int64_t)snapshots.size() - 1; i > currentSnapshot; --i)
            {
                if (snapshots[i])
                {
                    nextSnapshot = i;
                    break;
                }
            }
        }
    }
    if (nextSnapshot != -1 && nextSnapshot != currentSnapshot)
    {
        SetSnapshot(nextSnapshot);
    }
}

void PiPedalModel::OnNotifyNextMidiSnapshot(const RealtimeNextMidiProgramRequest &request)
{
    std::lock_guard<std::recursive_mutex> guard{mutex};
    try
    {
        if (request.direction >= 0)
        {
            NextSnapshot();
        }
        else
        {
            PreviousSnapshot();
        }
    }
    catch (std::exception &e)
    {
        Lv2Log::error(e.what());
    }
    if (this->audioHost)
    {
        this->audioHost->AckMidiProgramRequest(request.requestId);
    }
}

void PiPedalModel::OnNotifyNextMidiProgram(const RealtimeNextMidiProgramRequest &request)
{
    std::lock_guard<std::recursive_mutex> guard{mutex};
    try
    {

        if (request.direction >= 0)
        {
            NextPreset();
        }
        else
        {
            PreviousPreset();
        }
    }
    catch (std::exception &e)
    {

        Lv2Log::error(e.what());
    }
    if (this->audioHost)
    {
        this->audioHost->AckMidiProgramRequest(request.requestId);
    }
}

void PiPedalModel::OnNotifyMidiRealtimeSnapshotRequest(int32_t snapshotIndex, int64_t snapshotRequestId)
{
    try
    {
        SetSnapshot((int64_t)snapshotIndex);
    }
    catch (const std::exception &e)
    {
        Lv2Log::error(SS("SetSnapshot failed. " << e.what()));
    }
    if (this->audioHost)
    {
        this->audioHost->AckSnapshotRequest(snapshotRequestId);
    }
}

void PiPedalModel::OnNotifyNextMidiBank(const RealtimeNextMidiProgramRequest &request)
{
    std::lock_guard<std::recursive_mutex> guard{mutex};
    try
    {

        if (request.direction >= 0)
        {
            NextBank();
        }
        else
        {
            PreviousBank();
        }
    }
    catch (std::exception &e)
    {

        Lv2Log::error(e.what());
    }
    if (this->audioHost)
    {
        this->audioHost->AckMidiProgramRequest(request.requestId);
    }
}

void PiPedalModel::OnNotifyMidiProgramChange(RealtimeMidiProgramRequest &midiProgramRequest)
{
    std::lock_guard<std::recursive_mutex> guard{mutex};
    try
    {
        if (midiProgramRequest.bank >= 0)
        {
            int64_t bankId = storage.GetBankByMidiBankNumber(midiProgramRequest.bank);
            if (bankId == -1)
                throw PiPedalException("Bank not found.");
            if (bankId != this->storage.GetBanks().selectedBank())
            {
                storage.LoadBank(bankId);

                FireBanksChanged(-1);
                FirePresetsChanged(-1);
            }
        }
        int64_t presetId = storage.GetPresetByProgramNumber(midiProgramRequest.program);
        if (presetId == -1)
            throw PiPedalException("No valid preset.");
        LoadPreset(-1, presetId);
    }
    catch (std::exception &e)
    {

        Lv2Log::error(e.what());
    }
    if (this->audioHost)
    {
        this->audioHost->AckMidiProgramRequest(midiProgramRequest.requestId);
    }
}

void PiPedalModel::LoadPreset(int64_t clientId, int64_t instanceId)
{
    std::lock_guard<std::recursive_mutex> guard{mutex};

    if (storage.LoadPreset(instanceId))
    {
        this->pedalboard = storage.GetCurrentPreset();
        UpdateDefaults(&this->pedalboard);
        // The scroll position travels with the preset; only the browse
        // candidate is per-session state.
        gpioPendingPresetId = -1;
        gpioSelectedEffectId = -1;

        this->hasPresetChanged = false; // no fire.
        this->FirePedalboardChanged(clientId);
        this->FirePresetsChanged(clientId); // fire now.
    }
}

int64_t PiPedalModel::CopyPreset(int64_t clientId, int64_t from, int64_t to)
{
    std::lock_guard<std::recursive_mutex> guard{mutex};

    int64_t result = storage.CopyPreset(from, to);
    if (result != -1)
    {
        this->FirePresetsChanged(clientId); // fire now.
    }
    else
    {
        throw PiPedalStateException("Copy failed.");
    }
    return result;
}
bool PiPedalModel::UpdatePresets(int64_t clientId, const PresetIndex &presets)
{
    std::lock_guard<std::recursive_mutex> guard{mutex};
    storage.SetPresetIndex(presets);
    FirePresetsChanged(clientId);
    return true;
}

void PiPedalModel::MoveBank(int64_t clientId, int from, int to)
{
    std::lock_guard<std::recursive_mutex> guard{mutex};
    storage.MoveBank(from, to);
    FireBanksChanged(clientId);
}
int64_t PiPedalModel::DeleteBank(int64_t clientId, int64_t instanceId)
{
    std::lock_guard<std::recursive_mutex> guard{mutex};
    int64_t selectedBank = this->storage.GetBanks().selectedBank();
    int64_t newSelection = storage.DeleteBank(instanceId);

    int64_t newSelectedBank = this->storage.GetBanks().selectedBank();

    this->FireBanksChanged(clientId); // fire now.

    if (newSelectedBank != selectedBank)
    {
        this->OpenBank(clientId, newSelectedBank);
    }
    return newSelection;
}

int64_t PiPedalModel::DeletePresets(int64_t clientId, const std::vector<int64_t> &presetInstanceIds)
{
    std::lock_guard<std::recursive_mutex> guard{mutex};
    int64_t oldSelection = storage.GetCurrentPresetId();
    int64_t newSelection = storage.DeletePresets(presetInstanceIds);
    this->FirePresetsChanged(clientId); // fire BEFORE we load a new preset.
    if (oldSelection != newSelection)
    {
        this->LoadPreset(
            -1, // can't use cached version.
            newSelection);
    }
    return newSelection;
}
bool PiPedalModel::RenamePreset(int64_t clientId, int64_t instanceId, const std::string &name)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (storage.RenamePreset(instanceId, name))
    {
        this->FirePresetsChanged(clientId);
        if (storage.GetCurrentPresetId() == instanceId)
        {
            this->pedalboard.name(name);
            this->FirePedalboardChanged(-1);
        }
        return true;
    }
    else
    {
        throw PiPedalStateException("Rename failed.");
    }
}

GovernorSettings PiPedalModel::GetGovernorSettings()
{
    {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        GovernorSettings result;
        result.governor_ = storage.GetGovernorSettings();
        result.governors_ = pipedal::GetAvailableGovernors();
        return result;
    }
}
void PiPedalModel::SetGovernorSettings(const std::string &governor)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    adminClient.SetGovernorSettings(governor);

    this->storage.SetGovernorSettings(governor);

    std::vector<IPiPedalModelSubscriber::ptr> t{subscribers.begin(), subscribers.end()};
    for (auto &subscriber : t)
    {
        subscriber->OnGovernorSettingsChanged(governor);
    }
}

void PiPedalModel::SetWifiConfigSettings(const WifiConfigSettings &wifiConfigSettings)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);

#if NEW_WIFI_CONFIG
    if (this->storage.SetWifiConfigSettings(wifiConfigSettings))
    {
        this->UpdateDnsSd();
        if (this->hotspotManager)
        {
            this->hotspotManager->Reload();
        }
    }
#else
    this->storage.SetWifiConfigSettings(wifiConfigSettings);
    adminClient.SetWifiConfig(wifiConfigSettings);
#endif

    {
        WifiConfigSettings settingsWithNoSecrets = storage.GetWifiConfigSettings(); // (the passwordless version)

        std::vector<IPiPedalModelSubscriber::ptr> t{subscribers.begin(), subscribers.end()};
        for (auto &subscriber : t)
        {
            subscriber->OnWifiConfigSettingsChanged(settingsWithNoSecrets);
        }
    }
}

static std::string GetP2pdName()
{
    std::string name = "/etc/pipedal/config/pipedal_p2pd.conf";

    std::string result;

    if (ConfigUtil::GetConfigLine(name, "p2p_device_name", &result))
    {
        return result;
    }
    return "";
}

void PiPedalModel::UpdateDnsSd()
{
    if (!avahiService)
    {
        throw std::runtime_error("Not ready.");
    }
    ServiceConfiguration deviceIdFile;
    deviceIdFile.Load();
    WifiConfigSettings wifiSettings;
    wifiSettings.Load();
    std::string serviceName = wifiSettings.hotspotName_;
    if (serviceName == "")
    {
        serviceName = deviceIdFile.deviceName;
    }
    if (serviceName == "")
    {
        serviceName = "pipedal";
    }

    std::string hostName = GetHostName();
    if (serviceName != "" && deviceIdFile.uuid != "")
    {
        avahiService->Announce(webPort, serviceName, deviceIdFile.uuid, hostName, true);
    }
    else
    {
        // device_uuid file is written at install time. This warning is harmless if you're debugging.
        // Without it, we can't pulblish the website via dnsDS.
        // Run "pipedalconfig --install" to create the file.
        Lv2Log::warning("Cant read device_uuid file from service.conf file. dnsSD announcement skipped.");
    }
}
void PiPedalModel::SetWifiDirectConfigSettings(const WifiDirectConfigSettings &wifiDirectConfigSettings)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);

    adminClient.SetWifiDirectConfig(wifiDirectConfigSettings);

    this->storage.SetWifiDirectConfigSettings(wifiDirectConfigSettings);

    {
        WifiDirectConfigSettings tWifiDirectConfigSettings = storage.GetWifiDirectConfigSettings(); // (the passwordless version)

        std::vector<IPiPedalModelSubscriber::ptr> t{subscribers.begin(), subscribers.end()};
        for (auto &subscriber : t)
        {
            subscriber->OnWifiDirectConfigSettingsChanged(tWifiDirectConfigSettings);
        }

        // update NSD-SD announement.
        UpdateDnsSd();
    }
}

WifiConfigSettings PiPedalModel::GetWifiConfigSettings()
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    return this->storage.GetWifiConfigSettings();
}
WifiDirectConfigSettings PiPedalModel::GetWifiDirectConfigSettings()
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    return this->storage.GetWifiDirectConfigSettings();
}

void PiPedalModel::SetShowStatusMonitor(bool show)
{
    {
        std::lock_guard<std::recursive_mutex> lock(mutex); // copy atomically.
        storage.SetShowStatusMonitor(show);

        // Notify clients.
        std::vector<IPiPedalModelSubscriber::ptr> t{subscribers.begin(), subscribers.end()};
        for (auto &subscriber : t)
        {
            subscriber->OnShowStatusMonitorChanged(show);
        }
    }
}
bool PiPedalModel::GetShowStatusMonitor()
{
    std::lock_guard<std::recursive_mutex> lock(mutex); // copy atomically.
    return storage.GetShowStatusMonitor();
}

AirplaySettings PiPedalModel::GetAirplaySettings()
{
    std::lock_guard<std::recursive_mutex> lock(mutex); // copy atomically.
    return storage.GetAirplaySettings();
}

void PiPedalModel::PreviewAirplayVolume(float volume)
{
    audioHost->SetAirplayVolume(volume);
}

static std::string GetAirplayServiceName()
{
    ServiceConfiguration deviceIdFile;
    deviceIdFile.Load();
    WifiConfigSettings wifiSettings;
    wifiSettings.Load();
    // same naming rules as the dnsSD announcement.
    std::string serviceName = wifiSettings.hotspotName_;
    if (serviceName == "")
    {
        serviceName = deviceIdFile.deviceName;
    }
    if (serviceName == "")
    {
        serviceName = "PiPedal";
    }
    return serviceName;
}

void PiPedalModel::UpdateAirplayServiceConfiguration(const AirplaySettings &airplaySettings)
{
    AirplayServiceConfiguration serviceConfiguration;
    serviceConfiguration.enabled_ = airplaySettings.enabled_;
    serviceConfiguration.name_ = GetAirplayServiceName();
    uint32_t sampleRate = audioHost->GetSampleRate();
    serviceConfiguration.sampleRate_ = sampleRate == 0 ? 48000 : sampleRate;
    serviceConfiguration.fifoPath_ = AIRPLAY_FIFO_PATH;
    adminClient.SetAirplayConfiguration(serviceConfiguration);
}

void PiPedalModel::SetAirplaySettings(const AirplaySettings &airplaySettings)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);

    AirplaySettings oldSettings = storage.GetAirplaySettings();
    std::vector<IPiPedalModelSubscriber::ptr> t{subscribers.begin(), subscribers.end()};

    if (airplaySettings.enabled_ != oldSettings.enabled_)
    {
        try
        {
            UpdateAirplayServiceConfiguration(airplaySettings);
        }
        catch (const std::exception &e)
        {
            Lv2Log::error(SS("Can't configure the AirPlay service. " << e.what()));
            for (auto &subscriber : t)
            {
                subscriber->OnErrorMessage(e.what());
                subscriber->OnAirplaySettingsChanged(oldSettings); // revert optimistic UI state.
            }
            return;
        }
    }
    storage.SetAirplaySettings(airplaySettings);
    audioHost->SetAirplayVolume(airplaySettings.volume_);
    audioHost->SetAirplayOutputChannel(airplaySettings.outputChannel_);
    audioHost->SetAirplayStreamEnabled(airplaySettings.enabled_);

    for (auto &subscriber : t)
    {
        subscriber->OnAirplaySettingsChanged(airplaySettings);
    }
}

JackConfiguration PiPedalModel::GetJackConfiguration()
{
    std::lock_guard<std::recursive_mutex> lock(mutex); // copy atomically.
    return this->jackConfiguration;
}

void PiPedalModel::RestartAudio(bool useDummyAudioDriver)
{
    try
    {
        if (useDummyAudioDriver)
        {
            CancelAudioRetry();
        }
        if (this->audioHost->IsOpen())
        {

            this->audioHost->Close();
        }
        // restarting is a bit dodgy. It was impossible with Jack, but
        // now very plausible with the ALSA audio stack.

        // Still bugs wrt/ restarting the circular buffers for the audio thread.

        // do a complete reload.

        this->audioHost->SetPedalboard(nullptr);

        previousPedalboardLoaded = false;
        auto jackServerSettings = this->jackServerSettings;
        if (useDummyAudioDriver)
        {
            jackServerSettings.UseDummyAudioDevice();
        }

        auto jackConfiguration = this->jackConfiguration;
        jackConfiguration.AlsaInitialize(jackServerSettings);
        if (!jackConfiguration.isValid())
        {
            jackConfiguration.setErrorStatus("Error");
        }
        if (!useDummyAudioDriver)
        {
            this->jackConfiguration = jackConfiguration;
            FireJackConfigurationChanged(jackConfiguration);
        }

        if (!jackServerSettings.IsValid() || !jackConfiguration.isValid())
        {
            throw std::runtime_error("Audio configuration not valid.");
        }

        auto channelSelection = this->storage.GetChannelSelection();

        this->audioHost->Open(jackServerSettings, channelSelection); 

        this->pluginHost.OnConfigurationChanged(jackConfiguration, channelSelection);

        FireChannelRouterSettingsChanged(-1);
        LoadCurrentPedalboard();

        this->UpdateRealtimeVuSubscriptions();
        UpdateRealtimeMonitorPortSubscriptions();
    }
    catch (const std::exception &e)
    {
        this->audioHost->Close();
        if (useDummyAudioDriver)
        {
            Lv2Log::error(SS("Failed to start dummy audio driver. " << e.what()));
        }
        else
        {
            Lv2Log::error(SS("Failed to start audio. " << e.what()));
        }
        if (!useDummyAudioDriver)
        {
            RestartAudio(true); // use the dummy audio driver.
        }
    }
}

void PiPedalModel::OnAlsaSequencerDeviceAdded(int client, const std::string &clientName)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    auto alsaSequencerConfiguration = this->storage.GetAlsaSequencerConfiguration();
    std::string key = "seq:" + clientName;
    bool interested = false;
    for (const auto &port : alsaSequencerConfiguration.connections())
    {
        if (port.id().starts_with(key))
        {
            interested = true;
            break;
        }
    }
    if (interested)
    {
        Post(
            [this]
            {
                // reconfigure connections.
                std::lock_guard<std::recursive_mutex> lock(this->mutex);
                if (this->audioHost)
                {
                    this->audioHost->SetAlsaSequencerConfiguration(this->storage.GetAlsaSequencerConfiguration());
                }
            });
    }
}
void PiPedalModel::OnAlsaSequencerDeviceRemoved(int client)
{
    // no action  required.
}

void PiPedalModel::SetAlsaSequencerConfiguration(const AlsaSequencerConfiguration &alsaSequencerConfiguration)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);

    // reset midi connections even if the configuration hasn't changed.
    this->audioHost->SetAlsaSequencerConfiguration(alsaSequencerConfiguration);

    auto current = storage.GetAlsaSequencerConfiguration();
    if (alsaSequencerConfiguration != current)
    {
        this->storage.SetAlsaSequencerConfiguration(alsaSequencerConfiguration);
        // notify subscribers.
        std::vector<IPiPedalModelSubscriber::ptr> t{subscribers.begin(), subscribers.end()};
        for (auto &subscriber : t)
        {
            subscriber->OnAlsaSequencerConfigurationChanged(alsaSequencerConfiguration);
        }
    }
}
AlsaSequencerConfiguration PiPedalModel::GetAlsaSequencerConfiguration()
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    return this->storage.GetAlsaSequencerConfiguration();
}

std::vector<AlsaSequencerPortSelection> PiPedalModel::GetAlsaSequencerPorts()
{
    auto ports = AlsaSequencer::EnumeratePorts();
    std::vector<AlsaSequencerPortSelection> result;
    for (auto &port : ports)
    {
        result.push_back(AlsaSequencerPortSelection{
            port.id,
            port.name,
            port.displaySortOrder});
    }
    return result;
}

void PiPedalModel::FireChannelRouterSettingsChanged(int64_t clientId)
{
    std::lock_guard<std::recursive_mutex> guard{mutex};
    {
        // take a snapshot incase a client unsusbscribes in the notification handler (in which case the mutex won't protect us)
        ChannelRouterSettings::ptr channelRouterSettings = this->channelRouterSettings;

        std::vector<IPiPedalModelSubscriber::ptr> t{subscribers.begin(), subscribers.end()};
        for (auto &subscriber : t)
        {
            subscriber->OnChannelRouterSettingsChanged(clientId, *channelRouterSettings);
        }
    }
}

JackChannelSelection PiPedalModel::GetJackChannelSelection()
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    JackChannelSelection t = this->storage.GetJackChannelSelection(this->jackConfiguration);

    return t;
}

int64_t PiPedalModel::AddVuSubscription(int64_t instanceId)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    int64_t subscriptionId = ++nextSubscriptionId;
    activeVuSubscriptions.push_back(VuSubscription{subscriptionId, instanceId});

    UpdateRealtimeVuSubscriptions();

    return subscriptionId;
}
void PiPedalModel::RemoveVuSubscription(int64_t subscriptionHandle)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    for (auto i = activeVuSubscriptions.begin(); i != activeVuSubscriptions.end(); ++i)
    {
        if ((*i).subscriptionHandle == subscriptionHandle)
        {
            activeVuSubscriptions.erase(i);
            break;
        }
    }
    UpdateRealtimeVuSubscriptions();
}

void PiPedalModel::OnNotifyMidiValueChanged(int64_t instanceId, int portIndex, float value)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    PedalboardItem *item = this->pedalboard.GetItem(instanceId);
    if (item)
    {
        Lv2PluginInfo::ptr pPluginInfo;
        if (item->uri() == SPLIT_PEDALBOARD_ITEM_URI)
        {
            pPluginInfo = GetSplitterPluginInfo();
        }
        else
        {
            pPluginInfo = pluginHost.GetPluginInfo(item->uri());
        }
        if (pPluginInfo)
        {
            if (portIndex == -1)
            {
                this->pedalboard.SetItemEnabled(instanceId, value != 0);
                // take a snapshot incase a client unsusbscribes in the notification handler (in which case the mutex won't protect us)
                std::vector<IPiPedalModelSubscriber::ptr> t{subscribers.begin(), subscribers.end()};
                for (auto &subscriber : t)
                {
                    subscriber->OnItemEnabledChanged(-1, instanceId, value != 0);
                }

                this->SetPresetChanged(-1, true);
                return;
            }
            else
            {
                for (int i = 0; i < pPluginInfo->ports().size(); ++i)
                {
                    auto &port = pPluginInfo->ports()[i];
                    if (port->index() == portIndex)
                    {
                        std::string symbol = port->symbol();

                        this->pedalboard.SetControlValue(instanceId, symbol, value);
                        {

                            // take a snapshot incase a client unsusbscribes in the notification handler (in which case the mutex won't protect us)
                            std::vector<IPiPedalModelSubscriber::ptr> t{subscribers.begin(), subscribers.end()};
                            for (auto &subscriber : t)
                            {
                                subscriber->OnMidiValueChanged(instanceId, symbol, value);
                            }

                            this->SetPresetChanged(-1, true);
                            return;
                        }
                    }
                }
            }
        }
    }
}

void PiPedalModel::OnNotifyVusSubscription(const std::vector<VuUpdateX> &updates)
{
    std::vector<IPiPedalModelSubscriber::ptr> subscribers;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        subscribers = this->subscribers;
    }
    for (size_t i = 0; i < updates.size(); ++i)
    {
        // take a snapshot incase a client unsusbscribes in the notification handler (in which case the mutex won't protect us)
        for (auto &subscriber : subscribers)
        {
            subscriber->OnVuMeterUpdate(updates);
        }
    }
}

static bool isStartOrEndControl(int64_t controlId)
{
    switch (controlId)
    {
    case Pedalboard::START_CONTROL_ID:
    case Pedalboard::END_CONTROL_ID:
    case Pedalboard::AUX_START_CONTROL_ID:
    case Pedalboard::AUX_END_CONTROL_ID:
        return true;
    default:
        return false;
    }
}

void PiPedalModel::UpdateRealtimeVuSubscriptions()
{
    std::set<int64_t> addedInstances;

    for (int i = 0; i < activeVuSubscriptions.size(); ++i)
    {
        auto instanceId = activeVuSubscriptions[i].instanceid;
        if (pedalboard.HasItem(instanceId) || isStartOrEndControl(instanceId))
        {
            addedInstances.insert(activeVuSubscriptions[i].instanceid);
        }
    }
    if (audioHost)
    {
        std::vector<int64_t> instanceids(addedInstances.begin(), addedInstances.end());
        audioHost->SetVuSubscriptions(instanceids);
    }
}

void PiPedalModel::UpdateRealtimeMonitorPortSubscriptions()
{
    if (!audioHost)
    {
        return;
    }
    audioHost->SetMonitorPortSubscriptions(this->activeMonitorPortSubscriptions);
}

int64_t PiPedalModel::MonitorPort(int64_t instanceId, const std::string &key, float updateInterval, PortMonitorCallback onUpdate)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    int64_t subscriptionId = ++nextSubscriptionId;
    activeMonitorPortSubscriptions.push_back(
        MonitorPortSubscription{
            subscriptionId,
            instanceId,
            key,
            updateInterval,
            onUpdate});

    UpdateRealtimeMonitorPortSubscriptions();

    return subscriptionId;
}
void PiPedalModel::UnmonitorPort(int64_t subscriptionHandle)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);

    for (auto i = activeMonitorPortSubscriptions.begin(); i != activeMonitorPortSubscriptions.end(); ++i)
    {
        if ((*i).subscriptionHandle == subscriptionHandle)
        {
            activeMonitorPortSubscriptions.erase(i);
            UpdateRealtimeMonitorPortSubscriptions();
            break;
        }
    }
}

void PiPedalModel::OnNotifyMonitorPort(const MonitorPortUpdate &update)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);

    for (auto i = activeMonitorPortSubscriptions.begin(); i != activeMonitorPortSubscriptions.end(); ++i)
    {
        if ((*i).subscriptionHandle == update.subscriptionHandle)
        {

            // make the call ONLY if the subscription handle is still valid.
            (*update.callbackPtr)(update.subscriptionHandle, update.value);
            break;
        }
    }
}

void PiPedalModel::SendSetPatchProperty(
    int64_t clientId,
    int64_t instanceId,
    const std::string propertyUri,
    const json_variant &value,
    std::function<void()> onSuccess,
    std::function<void(const std::string &error)> onError)
{

    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (!audioHost)
    {
        onError("Audio not running.");
        return;
    }

    // save the property to the preset (currently used to reconstruct snapshots only)
    PedalboardItem *pedalboardItem = this->pedalboard.GetItem(instanceId);
    if (pedalboardItem)
    {
        std::shared_ptr<Lv2PluginInfo> pluginInfo = GetPluginInfo(pedalboardItem->uri_);
        auto pipedalUi = pluginInfo->piPedalUI();
        if (pipedalUi)
        {
            auto fileProperty = pipedalUi->GetFileProperty(propertyUri);
            if (fileProperty)
            {

                json_variant abstractPath = pluginHost.AbstractPath(value);
                std::string atomString = abstractPath.to_string();
                pedalboardItem->pathProperties_[propertyUri] = atomString;
            }
            this->SetPresetChanged(clientId, true);
        }
    }
    LV2_Atom *atomValue = atomConverter.ToAtom(value);

    std::function<void(RealtimePatchPropertyRequest *)> onRequestComplete{
        [this, onSuccess](RealtimePatchPropertyRequest *pParameter)
        {
            {
                std::lock_guard<std::recursive_mutex> lock(mutex);
                bool cancelled = true;
                for (auto i = this->outstandingParameterRequests.begin();
                     i != this->outstandingParameterRequests.end(); ++i)
                {
                    if ((*i) == pParameter)
                    {
                        cancelled = false;
                        this->outstandingParameterRequests.erase(i);
                        break;
                    }
                }
                if (!cancelled)
                {
                    if (pParameter->errorMessage != nullptr)
                    {
                        if (pParameter->onError)
                        {
                            pParameter->onError(pParameter->errorMessage);
                        }
                    }
                    else
                    {
                        if (onSuccess)
                        {
                            onSuccess();
                        }
                    }
                }
                delete pParameter;
            }
        }};

    LV2_URID urid = this->pluginHost.GetLv2Urid(propertyUri.c_str());
    size_t sampleTimeout = 0.5 * audioHost->GetSampleRate();
    RealtimePatchPropertyRequest *request = new RealtimePatchPropertyRequest(
        onRequestComplete,
        clientId, instanceId, urid, atomValue, nullptr, onError,
        sampleTimeout);

    outstandingParameterRequests.push_back(request);
    if (this->audioHost)
    {
        this->audioHost->sendRealtimeParameterRequest(request);
    }
}

void PiPedalModel::SendGetPatchProperty(
    int64_t clientId,
    int64_t instanceId,
    const std::string uri,
    std::function<void(const std::string &jsonResult)> onSuccess,
    std::function<void(const std::string &error)> onError)
{
    std::function<void(RealtimePatchPropertyRequest *)> onRequestComplete{
        [this](RealtimePatchPropertyRequest *pParameter)
        {
            {
                std::lock_guard<std::recursive_mutex> lock(mutex);
                bool cancelled = true;
                for (auto i = this->outstandingParameterRequests.begin();
                     i != this->outstandingParameterRequests.end(); ++i)
                {
                    if ((*i) == pParameter)
                    {
                        cancelled = false;
                        this->outstandingParameterRequests.erase(i);
                        break;
                    }
                }
                if (!cancelled)
                {
                    if (pParameter->errorMessage != nullptr)
                    {
                        if (pParameter->onError)
                        {
                            pParameter->onError(pParameter->errorMessage);
                        }
                    }
                    else if (pParameter->GetSize() == 0 && pParameter->requestType == RealtimePatchPropertyRequest::RequestType::PatchGet)
                    {
                        if (pParameter->onError)
                        {
                            // For plugins that don't respond (e.g. a buncha MOD plugins), use the value we last set on the plugin!
                            bool foundValue = false;
                            std::lock_guard<std::recursive_mutex> lock(mutex);
                            auto pedalboardItem = this->pedalboard.GetItem(pParameter->instanceId);
                            if (pedalboardItem)
                            {
                                if (pedalboardItem->pathProperties_.contains(pParameter->uri))
                                {
                                    pParameter->jsonResponse = pedalboardItem->pathProperties_[pParameter->uri];
                                    if (pParameter->onSuccess)
                                    {
                                        foundValue = true;
                                        pParameter->onSuccess(pParameter->jsonResponse);
                                    }
                                }
                            }
                            if (!foundValue)
                            {
                                pParameter->onError("No response.");
                            }
                        }
                    }
                    else
                    {
                        if (pParameter->onSuccess)
                        {
                            pParameter->onSuccess(pParameter->jsonResponse);
                        }
                    }
                }
                delete pParameter;
            }
        }};

    LV2_URID urid = this->pluginHost.GetLv2Urid(uri.c_str());

    std::lock_guard<std::recursive_mutex> lock(mutex);

    if (!this->audioHost)
    {
        onError("Audio stopped.");
    }
    size_t sampleTimeout = 0.3 * audioHost->GetSampleRate();
    RealtimePatchPropertyRequest *request = new RealtimePatchPropertyRequest(
        onRequestComplete,
        clientId, instanceId, urid, onSuccess, onError, sampleTimeout);
    request->uri = uri;

    outstandingParameterRequests.push_back(request);
    this->audioHost->sendRealtimeParameterRequest(request);
}

BankIndex PiPedalModel::GetBankIndex() const
{
    std::lock_guard<std::recursive_mutex> guard(const_cast<std::recursive_mutex &>(mutex));
    return storage.GetBanks();
}

void PiPedalModel::RenameBank(int64_t clientId, int64_t bankId, const std::string &newName)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    storage.RenameBank(bankId, newName);
    FireBanksChanged(clientId);
}

int64_t PiPedalModel::SaveBankAs(int64_t clientId, int64_t bankId, const std::string &newName)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    int64_t newId = storage.SaveBankAs(bankId, newName);
    FireBanksChanged(clientId);
    return newId;
}

void PiPedalModel::OpenBank(int64_t clientId, int64_t bankId)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);

    storage.LoadBank(bankId);
    FireBanksChanged(clientId);

    FirePresetsChanged(clientId);

    this->pedalboard = storage.GetCurrentPreset();

    UpdateDefaults(&this->pedalboard);
    this->hasPresetChanged = false;
    this->FirePedalboardChanged(clientId);
}

JackServerSettings PiPedalModel::GetJackServerSettings()
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    return this->jackServerSettings;
}

void PiPedalModel::SetOnboarding(bool value)
{
    std::unique_lock<std::recursive_mutex> guard(mutex);
    this->jackServerSettings.SetIsOnboarding(value);
    SetJackServerSettings(this->jackServerSettings);
}

void PiPedalModel::SetJackServerSettings(const JackServerSettings &jackServerSettings)
{
    std::unique_lock<std::recursive_mutex> guard(mutex);

#if JACK_HOST
    if (!adminClient.CanUseShutdownClient())
    {
        throw PiPedalException("Can't change server settings when running a debug server.");
    }
#endif

    this->jackServerSettings = jackServerSettings;
    try
    {
        this->jackServerSettings.FixUpDeviceNames();
    }
    catch (const std::exception &e)
    {
        Lv2Log::warning(SS("Unable to refresh audio device names. " << e.what()));
    }

    const bool codecZeroWasPresent = codecZeroPresent;
    RefreshCodecZeroPresence();
    if (gpioManager && codecZeroWasPresent != codecZeroPresent)
    {
        // Release any newly reserved direct GPIO before the audio device is
        // opened and its I2S pinctrl state is established.
        ConfigureGpioManager();
        UpdateGpioDashboard();
    }

    // take a snapshot incase a client unsusbscribes in the notification handler (in which case the mutex won't protect us)
    std::vector<IPiPedalModelSubscriber::ptr> t{subscribers.begin(), subscribers.end()};
    for (auto &subscriber : t)
    {
        subscriber->OnJackServerSettingsChanged(this->jackServerSettings);
    }

#if ALSA_HOST
    storage.SetJackServerSettings(this->jackServerSettings);

    FireJackConfigurationChanged(this->jackConfiguration);

    CancelAudioRetry();

    guard.unlock();
    RestartAudio();

#endif
#if JACK_HOST
    if (adminClient.CanUseShutdownClient())
    {
        // save the current (edited) preset now in case the service shutdown isn't clean.
        CurrentPreset currentPreset;
        currentPreset.modified_ = this->hasPresetChanged;
        currentPreset.preset_ = this->pedalboard;
        storage.SaveCurrentPreset(currentPreset);

        this->jackConfiguration.SetIsRestarting(true);
        FireJackConfigurationChanged(this->jackConfiguration);
        this->audioHost->UpdateServerConfiguration(
            jackServerSettings,
            [this](bool success, const std::string &errorMessage)
            {
                std::lock_guard<std::recursive_mutex> lock(mutex);
                if (!success)
                {
                    std::stringstream s;
                    s << "UpdateServerconfiguration failed: " << errorMessage;
                    Lv2Log::error(s.str().c_str());
                }
                // Update jack server status.
                if (!success)
                {
                    this->jackConfiguration.SetIsRestarting(false);
                    this->jackConfiguration.SetErrorStatus(errorMessage);
                    FireJackConfigurationChanged(this->jackConfiguration);
                }
                else
                {
                // we now do a complete restart of the services,
                // so just sit tight and wait for the restart.
#ifdef JUNK
                    this->jackConfiguration.SetErrorStatus("");
                    FireJackConfigurationChanged(this->jackConfiguration);

                    // restart the pedalboard on a new instance.
                    std::shared_ptr<Lv2Pedalboard> lv2Pedalboard{this->pluginHost.CreateLv2Pedalboard(this->pedalboard)};
                    this->lv2Pedalboard = lv2Pedalboard;

                    audioHost->SetPedalboard(lv2Pedalboard);
                    UpdateRealtimeVuSubscriptions();
                    UpdateRealtimeMonitorPortSubscriptions();
#endif
                }
            });
    }
#endif
}

std::shared_ptr<Lv2PluginInfo> PiPedalModel::GetPluginInfo(const std::string &uri)
{
    return pluginHost.GetPluginInfo(uri);
}

void PiPedalModel::UpdateDefaults(SnapshotValue &snapshotValue, const PedalboardItem *pedalboardItem_)
{
    std::shared_ptr<Lv2PluginInfo> pPlugin = pluginHost.GetPluginInfo(pedalboardItem_->uri());
    if (!pPlugin)
    {
        if (pedalboardItem_->uri() == SPLIT_PEDALBOARD_ITEM_URI)
        {
            pPlugin = GetSplitterPluginInfo();
        }
    }
    if (pPlugin)
    {
        // Fix incorrect bypass settings presint in Pipedal < 1.5.96
        for (size_t i = 0; i < pPlugin->ports().size(); ++i)
        {
            auto &port = pPlugin->ports()[i];
            if (port->is_bypass() && port->is_control_port() && port->is_input())
            {
                ControlValue *pValue = snapshotValue.GetControlValue(port->symbol());
                float value = snapshotValue.isEnabled_ ? 1.0f : 0.0f;

                if (pValue == nullptr)
                {
                    snapshotValue.controlValues_.push_back(
                        pipedal::ControlValue(port->symbol().c_str(), value));
                }
                else
                {
                    pValue->value(value);
                }
            }
        }
        //////// PLUGIN SPECIFIC UPGRADES //////////////////////
        if (pPlugin->uri() == "http://two-play.com/plugins/toob-nam")
        {
            ControlValue *pVersion = snapshotValue.GetControlValue("version");
            if (pVersion == nullptr)
            {
                ControlValue *pValue = snapshotValue.GetControlValue("inputCalibrationMode");
                if (pValue == nullptr)
                {
                    // calibration is OFF when upgradfing.
                    snapshotValue.SetControlValue("inputCalibrationMode", 0.0f);
                }
                // convert old gate threshold to new gate threshold.
                ControlValue *pGateValue = snapshotValue.GetControlValue("gate");
                if (pGateValue)
                {
                    float value = pGateValue->value();
                    // Is the gate disabled?
                    if (value <= -100.0f)
                    {
                        value = -120.0f; // "disabled in new range."
                    }
                    else
                    {
                        value = value * 0.5; // correct the bug in original implementation.
                    }
                    snapshotValue.SetControlValue("gate", value);
                }
                snapshotValue.SetControlValue("version", 0.0f);
            }
        }
        if (pPlugin->piPedalUI())
        {
            PiPedalUI::ptr piPedalUI = pPlugin->piPedalUI();
            std::set<std::string> validFileProperties;
            for (auto &fileProperty : piPedalUI->fileProperties())
            {
                validFileProperties.insert(fileProperty->patchProperty());
            }
            for (auto i = snapshotValue.pathProperties_.begin(); i != snapshotValue.pathProperties_.end(); /**/)
            {
                if (!validFileProperties.contains(i->first))
                {
                    i = snapshotValue.pathProperties_.erase(i);
                }
                else
                {
                    ++i;
                }
            }
        }
    }
}

void PiPedalModel::UpdateDefaults(PedalboardItem *pedalboardItem, std::unordered_map<int64_t, PedalboardItem *> &itemMap)
{
    itemMap[pedalboardItem->instanceId()] = pedalboardItem;

    std::shared_ptr<Lv2PluginInfo> pPlugin = pluginHost.GetPluginInfo(pedalboardItem->uri());
    if (!pPlugin)
    {
        pedalboardItem->instanceId();
        if (pedalboardItem->uri() == SPLIT_PEDALBOARD_ITEM_URI)
        {
            pPlugin = GetSplitterPluginInfo();
        }
    }
    if (pPlugin)
    {
        if (pPlugin->hasMidiInput())
        {
            if (!pedalboardItem->midiChannelBinding())
            {
                pedalboardItem->midiChannelBinding(MidiChannelBinding::DefaultForMissingValue());
            }
        }
        else
        {
            if (pedalboardItem->midiChannelBinding())
            {
                pedalboardItem->midiChannelBinding(std::optional<MidiChannelBinding>()); // clear it.
            }
        }
        //////// PLUGIN SPECIFIC UPGRADES //////////////////////
        if (pPlugin->uri() == "http://two-play.com/plugins/toob-nam")
        {
            ControlValue *pVersion = pedalboardItem->GetControlValue("version");
            if (pVersion == nullptr)
            {
                ControlValue *pValue = pedalboardItem->GetControlValue("inputCalibrationMode");
                if (pValue == nullptr)
                {
                    // calibration is OFF when upgradfing.
                    pedalboardItem->SetControlValue("inputCalibrationMode", 0.0f);
                }
                // convert old gate threshold to new gate threshold.
                ControlValue *pGateValue = pedalboardItem->GetControlValue("gate");
                if (pGateValue)
                {
                    float value = pGateValue->value();
                    // Is the gate disabled?
                    if (value <= -100.0f)
                    {
                        value = -120.0f; // "disabled in new range."
                    }
                    else
                    {
                        value = value * 0.5; // correct the bug in original implementation.
                    }
                    pedalboardItem->SetControlValue("gate", value);
                }
                pedalboardItem->SetControlValue("version", 0.0f);
            }
        }
        for (size_t i = 0; i < pPlugin->ports().size(); ++i)
        {
            auto port = pPlugin->ports()[i];

            if (port->is_control_port() && port->is_input())
            {
                if (port->is_bypass())
                {
                    // retroactively correct bug in version of PiPedal prior to 1.5.96.
                    ControlValue *pValue = pedalboardItem->GetControlValue(port->symbol());
                    float value = pedalboardItem->isEnabled() ? 1.0f : 0.0f;

                    if (pValue == nullptr)
                    {
                        pedalboardItem->controlValues().push_back(
                            pipedal::ControlValue(port->symbol().c_str(), value));
                    }
                    else
                    {
                        pValue->value(value);
                    }
                }
                else
                {
                    ControlValue *pValue = pedalboardItem->GetControlValue(port->symbol());
                    if (pValue == nullptr)
                    {
                        // Missing? Set it to default value.
                        pedalboardItem->controlValues().push_back(
                            pipedal::ControlValue(port->symbol().c_str(), port->default_value()));
                    }
                }
            }
        }
        if (pPlugin->piPedalUI())
        {
            PiPedalUI::ptr piPedalUI = pPlugin->piPedalUI();
            std::set<std::string> validFileProperties;
            for (auto &fileProperty : piPedalUI->fileProperties())
            {
                validFileProperties.insert(fileProperty->patchProperty());
                if (!pedalboardItem->pathProperties_.contains(fileProperty->patchProperty()))
                {
                    // make sure each pedalboard item has a complete list of path properties, even if it doesn't yet have values.
                    pedalboardItem->pathProperties_[fileProperty->patchProperty()] = "null";
                }
            }
            for (auto i = pedalboardItem->pathProperties_.begin(); i != pedalboardItem->pathProperties_.end(); /**/)
            {
                if (!validFileProperties.contains(i->first))
                {
                    i = pedalboardItem->pathProperties_.erase(i);
                }
                else
                {
                    ++i;
                }
            }
        }
    }
    else
    {
        // an old bug leaks lv2states.  Clean it up here.
        if (pedalboardItem->uri() == EMPTY_PEDALBOARD_ITEM_URI)
        {
            pedalboardItem->lv2State(Lv2PluginState());
        }
    }
    for (size_t i = 0; i < pedalboardItem->topChain().size(); ++i)
    {
        UpdateDefaults(&(pedalboardItem->topChain()[i]), itemMap);
    }
    for (size_t i = 0; i < pedalboardItem->bottomChain().size(); ++i)
    {
        UpdateDefaults(&(pedalboardItem->bottomChain()[i]), itemMap);
    }
}

void PiPedalModel::UpdateDefaults(Snapshot *snapshot, std::unordered_map<int64_t, PedalboardItem *> &itemMap)
{
    if (!snapshot)
        return;
    for (size_t i = 0; i < snapshot->values_.size(); ++i)
    {
        SnapshotValue &value = snapshot->values_[i];
        auto f = itemMap.find(value.instanceId_);
        if (f == itemMap.end())
        {
            // plugin is no longer present. Remove from the snapshot.
            snapshot->values_.erase(snapshot->values_.begin() + i);
            --i;
        }
        else
        {
            UpdateDefaults(value, f->second);
        }
    }
    for (auto &snapshotValue : snapshot->values_)
    {
    }
}

void PiPedalModel::UpdateDefaults(Pedalboard *pedalboard)
{
    // add missing values.
    std::unordered_map<int64_t, PedalboardItem *> itemMap;
    auto allPlugins = pedalboard->GetAllPlugins();
    for (size_t i = 0; i < allPlugins.size(); ++i)
    {
        UpdateDefaults(allPlugins[i], itemMap);
    }
    // set all missing values on snapshots to default values.
    for (auto snapshot : pedalboard->snapshots())
    {
        if (snapshot)
        {
            UpdateDefaults(snapshot.get(), itemMap);
        }
    }
}

PluginPresets PiPedalModel::GetPluginPresets(const std::string &pluginUri)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);

    return storage.GetPluginPresets(pluginUri);
}

PluginUiPresets PiPedalModel::GetPluginUiPresets(const std::string &pluginUri)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);

    return storage.GetPluginUiPresets(pluginUri);
}

void PiPedalModel::LoadPluginPreset(int64_t pluginInstanceId, uint64_t presetInstanceId)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);

    PedalboardItem *pedalboardItem = this->pedalboard.GetItem(pluginInstanceId);
    if (pedalboardItem != nullptr)
    {
        int32_t oldStateUpdateCount = pedalboardItem->stateUpdateCount();

        PluginPresetValues presetValues = storage.GetPluginPresetValues(pedalboardItem->uri(), presetInstanceId);
        // if the plugin has state, we have to rebuild the pedalboard, since setting state is not thread-safe.
        // Same goes if lilvPresetUri is not empty.

        // lilvPresetUri: use lilv to load the preset from the RDF model. Occurs when using a factory preset
        // that has state:state, because lilv doesn't allow us to read this data, but does load it.
        // This is a transient condition.

        for (auto &control : presetValues.controls)
        {
            this->pedalboard.SetControlValue(pluginInstanceId, control.key(), control.value());
        }

        if ((!presetValues.state.isValid_) && presetValues.lilvPresetUri.empty() && presetValues.pathProperties.empty())
        {
            // fast path for control changes only.
            audioHost->SetPluginPreset(pluginInstanceId, presetValues.controls);

            std::vector<IPiPedalModelSubscriber::ptr> t{subscribers.begin(), subscribers.end()};
            for (auto &subscriber : t)
            {
                subscriber->OnLoadPluginPreset(pluginInstanceId, presetValues.controls);
            }
        }
        else
        {
            pedalboardItem->lv2State(presetValues.state);
            pedalboardItem->lilvPresetUri(presetValues.lilvPresetUri);
            pedalboardItem->stateUpdateCount(oldStateUpdateCount + 1);
            pedalboardItem->pathProperties(presetValues.pathProperties);
            FirePedalboardChanged(-1); // does a complete reload of both client and audio server.
        }
        this->SetPresetChanged(-1, true);
    }
}

void PiPedalModel::DeleteAtomOutputListeners(int64_t clientId)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    for (size_t i = 0; i < atomOutputListeners.size(); ++i)
    {
        if (atomOutputListeners[i].clientId == clientId)
        {
            atomOutputListeners.erase(atomOutputListeners.begin() + i);
            --i;
        }
    }
    if (audioHost)
    {
        audioHost->SetListenForAtomOutput(atomOutputListeners.size() != 0);
    }
}

void PiPedalModel::DeleteMidiListeners(int64_t clientId)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    for (size_t i = 0; i < midiEventListeners.size(); ++i)
    {
        if (midiEventListeners[i].clientId == clientId)
        {
            midiEventListeners.erase(midiEventListeners.begin() + i);
            --i;
        }
    }
    if (audioHost)
    {
        audioHost->SetListenForMidiEvent(midiEventListeners.size() != 0);
    }
}

void PiPedalModel::OnPatchSetReply(uint64_t instanceId, LV2_URID patchSetProperty, const LV2_Atom *atomValue)
{
    std::vector<IPiPedalModelSubscriber::ptr> subscribers;
    std::vector<AtomOutputListener> atomOutputListeners;
    std::string propertyUri;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex);

        subscribers = this->subscribers;
        atomOutputListeners = this->atomOutputListeners;
        propertyUri = pluginHost.GetMapFeature().UridToString(patchSetProperty);

        {
            PedalboardItem *item = pedalboard.GetItem((int64_t)instanceId);
            if (item == nullptr)
                return;
            atom_object atomObject{atomValue};

            PedalboardItem::PropertyMap &properties = item->PatchProperties();
            if (properties.contains(propertyUri) && properties[propertyUri] == atomObject)
            {
                // do noting.
            }
            else
            {
                properties[propertyUri] = std::move(atomObject);
            }
        }
        if (audioHost)
        {
            audioHost->SetListenForAtomOutput(atomOutputListeners.size() != 0);
        }
    }
    OnNotifyMaybeLv2StateChanged(instanceId);

    bool hasAtomJson = false;
    std::string atomJson;

    for (int i = 0; i < atomOutputListeners.size(); ++i)
    {
        auto &listener = atomOutputListeners[i];
        if (listener.WantsProperty(instanceId, patchSetProperty))
        {
            auto subscriber = this->GetNotificationSubscriber(listener.clientId);
            if (subscriber)
            {
                if (!hasAtomJson)
                {
                    atomJson = this->audioHost->AtomToJson(atomValue);
                    hasAtomJson = true;
                }
                subscriber->OnNotifyPatchProperty(listener.clientHandle, instanceId, propertyUri, atomJson);
            }
            else
            {
                atomOutputListeners.erase(atomOutputListeners.begin() + i);
                --i;
            }
        }
    }
}

void PiPedalModel::OnNotifyPathPatchPropertyReceived(
    int64_t instanceId,
    LV2_URID pathPatchProperty,
    LV2_Atom *pathProperty)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);

    std::string pathPatchPropertyUri = this->pluginHost.Lv2UridToString(pathPatchProperty);
    std::string atomString = atomConverter.ToString(pathProperty);
    auto pedalboardItem = this->pedalboard.GetItem(instanceId);

    if (this->audioHost)
    {
        this->audioHost->OnNotifyPathPatchPropertyReceived(instanceId, pathPatchPropertyUri, atomString);
    }

    if (pedalboardItem == nullptr)
    {
        return;
    }

    auto i = pedalboardItem->pathProperties_.find(pathPatchPropertyUri);
    if (i != pedalboardItem->pathProperties_.end())
    {
        std::string abstractAtomString = storage.ToAbstractPathFromJson(atomString);
        pedalboardItem->pathProperties_[pathPatchPropertyUri] = abstractAtomString;

        std::vector<IPiPedalModelSubscriber::ptr> t{subscribers.begin(), subscribers.end()};
        for (auto &subscriber : t)
        {
            subscriber->OnNotifyPathPatchPropertyChanged(
                instanceId,
                pathPatchPropertyUri,
                abstractAtomString);
        }
    }
}

void PiPedalModel::OnNotifyMidiListen(uint8_t cc0, uint8_t cc1, uint8_t cc2)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    bool isNote = (cc0 & 0xF0) == 0x90; // Note On
    if (isNote && cc2 == 0)
    {
        return; // Note off. Oopsie.
    }
    bool isControl = (cc0 & 0xF0) == 0xB0; // Control Change
    if (!isNote && !isControl)
    {
        return; // Not a note on or control change.
    }

    for (int i = 0; i < midiEventListeners.size(); ++i)
    {
        auto &listener = midiEventListeners[i];
        auto subscriber = this->GetNotificationSubscriber(listener.clientId);
        if (subscriber)
        {
            subscriber->OnNotifyMidiListener(listener.clientHandle, cc0, cc1, cc2);
        }
        else
        {
            midiEventListeners.erase(midiEventListeners.begin() + i);
            --i;
        }
    }
    audioHost->SetListenForMidiEvent(midiEventListeners.size() != 0);
}

void PiPedalModel::ListenForMidiEvent(int64_t clientId, int64_t clientHandle)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    MidiListener listener{clientId, clientHandle};
    midiEventListeners.push_back(listener);
    audioHost->SetListenForMidiEvent(true);
}

void PiPedalModel::CancelListenForMidiEvent(int64_t clientId, int64_t clientHandle)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    for (size_t i = 0; i < midiEventListeners.size(); ++i)
    {
        const auto &listener = midiEventListeners[i];
        if (listener.clientId == clientId && listener.clientHandle == clientHandle)
        {
            midiEventListeners.erase(midiEventListeners.begin() + i);
            break;
        }
    }
    if (midiEventListeners.size() == 0)
    {
        audioHost->SetListenForMidiEvent(false);
    }
}

void PiPedalModel::MonitorPatchProperty(int64_t clientId, int64_t clientHandle, uint64_t instanceId, const std::string &propertyUri)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    LV2_URID propertyUrid = 0;
    if (propertyUri.length() != 0)
    {
        propertyUrid = pluginHost.GetMapFeature().GetUrid(propertyUri.c_str());
    }
    AtomOutputListener listener{clientId, clientHandle, instanceId, propertyUrid};
    atomOutputListeners.push_back(listener);
    audioHost->SetListenForAtomOutput(true);

    PedalboardItem *item = this->pedalboard.GetItem(instanceId);
    if (item)
    {
        auto &map = item->pathProperties();
        if (map.contains(propertyUri))
        {
            const auto &value = map.at(propertyUri);
            if (value != "null")
            {
                try
                {
                    std::string json = storage.FromAbstractPathJson(value);

                    for (auto &subscriber : this->subscribers)
                    {
                        if (subscriber->GetClientId() == clientId)
                        {
                            subscriber->OnNotifyPatchProperty(clientHandle, instanceId, propertyUri, json);
                        }
                    }
                }
                catch (const std::exception &ignored)
                {
                }
            }
        }
    }
}

void PiPedalModel::CancelMonitorPatchProperty(int64_t clientId, int64_t clientHandle)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    for (size_t i = 0; i < atomOutputListeners.size(); ++i)
    {
        const auto &listener = atomOutputListeners[i];
        if (listener.clientId == clientId && listener.clientHandle == clientHandle)
        {
            atomOutputListeners.erase(atomOutputListeners.begin() + i);
            break;
        }
    }
    if (midiEventListeners.size() == 0)
    {
        audioHost->SetListenForMidiEvent(false);
    }
}

std::vector<AlsaDeviceInfo> PiPedalModel::GetAlsaDevices()
{
    std::vector<AlsaDeviceInfo> result = this->alsaDevices.GetAlsaDevices();
#ifdef JUNK
    // Useful for debugging non-stereo device configurations
    result.push_back(MakeDummyDeviceInfo(1));
    result.push_back(MakeDummyDeviceInfo(2));
    result.push_back(MakeDummyDeviceInfo(8));
#endif
    return result;
}

const std::filesystem::path &PiPedalModel::GetWebRoot() const
{
    return webRoot;
}

std::map<std::string, bool> PiPedalModel::GetFavorites() const
{
    std::lock_guard<std::recursive_mutex> guard(const_cast<std::recursive_mutex &>(mutex));

    return storage.GetFavorites();
}
void PiPedalModel::SetFavorites(const std::map<std::string, bool> &favorites)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    storage.SetFavorites(favorites);

    // take a snapshot incase a client unsusbscribes in the notification handler (in which case the mutex won't protect us)
    std::vector<IPiPedalModelSubscriber::ptr> t{subscribers.begin(), subscribers.end()};
    for (auto &subscriber : t)
    {
        subscriber->OnFavoritesChanged(favorites);
    }
}
std::vector<MidiBinding> PiPedalModel::GetSystemMidiBidings()
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    return this->systemMidiBindings;
}
void PiPedalModel::SetSystemMidiBindings(std::vector<MidiBinding> &bindings)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    this->systemMidiBindings = bindings;
    storage.SetSystemMidiBindings(bindings);
    if (this->audioHost)
    {
        this->audioHost->SetSystemMidiBindings(bindings);
    }

    std::vector<IPiPedalModelSubscriber::ptr> t{subscribers.begin(), subscribers.end()};
    for (auto &subscriber : t)
    {
        subscriber->OnSystemMidiBindingsChanged(bindings);
    }
}

GpioSettings PiPedalModel::GetGpioSettings()
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    return gpioSettings;
}

void PiPedalModel::SetGpioSettings(const GpioSettings &settings)
{
    GpioManager::Validate(settings);
    std::lock_guard<std::recursive_mutex> lock(mutex);
    storage.SetGpioSettings(settings);
    gpioSettings = settings;
    gpioPendingPresetId = -1;
    if (gpioManager)
    {
        ConfigureGpioManager();
        UpdateGpioDashboard();
    }
    FireGpioSettingsChanged();
}

void PiPedalModel::RefreshCodecZeroPresence()
{
    codecZeroPresent = false;
    bool selectedCodecIdWasDiscovered = false;
    auto isCodecZeroId = [](std::string id)
    {
        std::transform(id.begin(), id.end(), id.begin(), [](unsigned char c)
                       { return static_cast<char>(std::tolower(c)); });
        return id == "hw:zero" || id == "hw:iqaudiocodec";
    };
    try
    {
        for (const auto &device : alsaDevices.GetAlsaDevices())
        {
            if (isCodecZeroId(device.id_))
                selectedCodecIdWasDiscovered = true;
            if (device.deviceProfile_ == ALSA_DEVICE_PROFILE_CODEC_ZERO_AUX)
            {
                codecZeroPresent = true;
                return;
            }
        }
    }
    catch (const std::exception &e)
    {
        Lv2Log::warning(SS("Unable to check for Codec Zero GPIO reservations. " << e.what()));
    }

    // Card discovery can be temporarily incomplete while another process owns
    // a PCM. Preserve protection for the stable card ids used by both hardware
    // revisions whenever either one is already selected.
    codecZeroPresent = !selectedCodecIdWasDiscovered &&
                       (isCodecZeroId(jackServerSettings.GetAlsaInputDevice()) ||
                        isCodecZeroId(jackServerSettings.GetAlsaOutputDevice()));
}

void PiPedalModel::ConfigureGpioManager()
{
    if (!gpioManager)
    {
        return;
    }

    GpioLineReservations reservations;
    if (codecZeroPresent)
    {
        std::vector<std::string> headerChips;
        GpioCapabilities capabilities = GpioManager::Discover();
        for (const auto &gpioChip : capabilities.chips_)
        {
            std::string identity = gpioChip.name_ + " " + gpioChip.label_;
            std::transform(identity.begin(), identity.end(), identity.begin(), [](unsigned char c)
                           { return static_cast<char>(std::tolower(c)); });
            if (identity.find("pinctrl-bcm") != std::string::npos ||
                identity.find("pinctrl-rp1") != std::string::npos)
            {
                headerChips.push_back(gpioChip.path_);
            }
        }
        if (headerChips.empty())
            headerChips.push_back("/dev/gpiochip0");

        for (const auto &chip : headerChips)
        {
            for (int32_t line = 18; line <= 21; ++line)
            {
                reservations.push_back({
                    chip,
                    line,
                    "GPIO " + std::to_string(line) +
                        " is reserved for the attached Raspberry Pi Codec Zero I2S audio interface."});
            }
        }
    }
    gpioManager->Configure(gpioSettings, reservations);
}

GpioCapabilities PiPedalModel::GetGpioCapabilities()
{
    return GpioManager::Discover();
}

std::vector<GpioInputStatus> PiPedalModel::GetGpioInputStatuses()
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (!gpioManager)
    {
        return {};
    }
    return gpioManager->GetStatuses();
}

void PiPedalModel::SetGpioBindings(int64_t clientId, const std::vector<GpioBinding> &bindings)
{
    if (bindings.size() > 256)
    {
        throw std::invalid_argument("A preset cannot contain more than 256 GPIO mappings.");
    }
    for (const auto &binding : bindings)
    {
        if (binding.inputId_.empty() ||
            binding.actionType_ < static_cast<int32_t>(GpioActionType::Control) ||
            binding.actionType_ > static_cast<int32_t>(GpioActionType::PreviousBank) ||
            binding.mode_ < static_cast<int32_t>(GpioBindingMode::Direct) ||
            binding.mode_ > static_cast<int32_t>(GpioBindingMode::Relative) ||
            binding.eventType_ < static_cast<int32_t>(GpioBindingEventType::Value) ||
            binding.eventType_ > static_cast<int32_t>(GpioBindingEventType::EncoderButton) ||
            // Not std::isfinite: release builds are compiled -ffast-math, which
            // is free to fold that check away and let a NaN reach the preset
            // file and then the OLED. See IsFiniteGpioValue.
            !IsFiniteGpioValue(binding.minValue_) || !IsFiniteGpioValue(binding.maxValue_) ||
            !IsFiniteGpioValue(binding.curve_) || binding.curve_ < 0.05f || binding.curve_ > 20.0f ||
            !IsFiniteGpioValue(binding.stepValue_) || binding.stepValue_ <= 0.0f || binding.stepValue_ > 1000000.0f)
        {
            throw std::invalid_argument("Invalid GPIO mapping.");
        }
    }

    {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        pedalboard.gpioBindings(bindings);
    }
    FirePedalboardChanged(clientId, false);
    SetPresetChanged(clientId, true, false);
}

void PiPedalModel::FireGpioSettingsChanged()
{
    std::vector<IPiPedalModelSubscriber::ptr> listeners{subscribers.begin(), subscribers.end()};
    for (auto &listener : listeners)
    {
        listener->OnGpioSettingsChanged(gpioSettings);
    }
}

void PiPedalModel::FireGpioInputStatusChanged(const GpioInputStatus &status)
{
    std::vector<IPiPedalModelSubscriber::ptr> listeners;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        listeners = subscribers;
    }
    for (auto &listener : listeners)
    {
        listener->OnGpioInputStatusChanged(status);
    }
}

void PiPedalModel::ShowGpioWorkflowMessage(
    const std::string &title,
    const std::string &label,
    const std::string &value)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (!gpioManager)
        return;
    GpioDisplayMessage message;
    message.title = title;
    message.label = label;
    message.value = value;
    gpioManager->ShowDisplayMessage(message);
}

// A port the web interface hides has no control the user can reach, so a
// hardware encoder must not offer it either.
static bool IsGpioVisibleParameter(const Lv2PortInfo &port)
{
    if (!port.is_control_port() || !port.is_input() || port.not_on_gui())
        return false;
    if (port.is_bypass() || port.symbol() == "bypass" || port.symbol() == "Bypass")
        return false;
    return port.name() != "bypass" && port.name() != "Bypass";
}

// The inverse of Lv2PortInfo::rangeToValue: a 0..1 position on the port's own
// scale, so that a detent moves a logarithmic control by a constant ratio.
static float GpioValueToRange(const Lv2PortInfo &port, float value)
{
    const float minimum = port.min_value();
    const float maximum = port.max_value();
    if (port.is_logarithmic() && minimum > 0.0f && maximum > 0.0f && maximum != minimum)
    {
        return std::log(std::max(value, minimum) / minimum) /
               std::log(maximum / minimum);
    }
    if (maximum == minimum)
        return 0.0f;
    return (value - minimum) / (maximum - minimum);
}

std::vector<std::pair<int64_t, std::string>> PiPedalModel::GetGpioEffects()
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    std::vector<std::pair<int64_t, std::string>> result;
    for (auto *item : pedalboard.GetAllPlugins())
    {
        if (item->isEmpty() || item->isSplit())
            continue;
        result.emplace_back(
            item->instanceId_,
            item->title_.empty() ? item->pluginName_ : item->title_);
    }
    return result;
}

void PiPedalModel::EnsureGpioSelectedEffect()
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    const auto effects = GetGpioEffects();
    if (effects.empty())
    {
        gpioSelectedEffectId = -1;
        return;
    }
    if (std::none_of(
            effects.begin(), effects.end(),
            [this](const auto &effect) { return effect.first == gpioSelectedEffectId; }))
    {
        const int64_t anchor = pedalboard.gpioScrollInstanceId();
        const auto anchored = std::find_if(
            effects.begin(), effects.end(),
            [anchor](const auto &effect) { return effect.first == anchor; });
        gpioSelectedEffectId = anchored != effects.end() ? anchored->first : effects.front().first;
    }
}

std::vector<GpioParameter> PiPedalModel::GetGpioParameters(int64_t effectId)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    std::vector<GpioParameter> result;
    for (auto *item : pedalboard.GetAllPlugins())
    {
        if ((effectId != -1 && item->instanceId_ != effectId) ||
            item->isEmpty() || item->isSplit())
            continue;
        auto plugin = GetPluginInfo(item->uri_);
        if (!plugin)
        {
            if (effectId != -1)
                break;
            continue;
        }
        for (const auto &port : plugin->ports())
        {
            // Match the web UI's actual control set: hidden/output/bypass
            // ports are rejected by IsGpioVisibleParameter, and a visible
            // control must also have a live value on this pedalboard item.
            if (port && IsGpioVisibleParameter(*port) &&
                item->GetControlValue(port->symbol()) != nullptr)
                result.push_back(GpioParameter{item->instanceId_, port->symbol()});
        }
        if (effectId != -1)
            break;
    }
    return result;
}

// The window always shows four adjacent parameters, so the last scroll
// position is the one whose fourth parameter is the last in the list.
static size_t GpioScrollPositions(size_t parameterCount)
{
    return parameterCount <= 4 ? 1 : parameterCount - 3;
}

size_t PiPedalModel::GetGpioScrollIndex(const std::vector<GpioParameter> &parameters)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (parameters.empty())
        return 0;
    const GpioParameter anchor{
        pedalboard.gpioScrollInstanceId(), pedalboard.gpioScrollSymbol()};
    auto found = std::find(parameters.begin(), parameters.end(), anchor);
    if (found == parameters.end())
        return 0;
    return std::min(
        static_cast<size_t>(std::distance(parameters.begin(), found)),
        GpioScrollPositions(parameters.size()) - 1);
}

void PiPedalModel::SetGpioScrollIndex(
    const std::vector<GpioParameter> &parameters, size_t index)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (index >= parameters.size())
        return;
    // Stored as the first shown parameter rather than an index, so that adding
    // or reordering effects moves the window with the parameter the player
    // chose. Deliberately does not mark the preset changed or notify clients:
    // this is a hardware view position, like the selected plugin.
    pedalboard.gpioScrollInstanceId(parameters[index].instanceId);
    pedalboard.gpioScrollSymbol(parameters[index].symbol);
}

// A parameter encoder handles its own turns but leaves its push button free.
// The ANO device is entirely reserved for navigation. Legacy preset/scroll
// role events can never fire after migration, so remove mappings that would be
// listed in the web interface but quietly do nothing.
void PiPedalModel::RemoveUnreachableGpioBindings()
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    auto roleForInput = [this](const std::string &inputId)
    {
        for (const auto &input : gpioSettings.inputs_)
        {
            if (input.id_ == inputId)
                return input.encoderRole();
        }
        return GpioEncoderRole::None;
    };

    const auto &bindings = pedalboard.gpioBindings();
    auto isUnreachable = [&](const GpioBinding &binding)
    {
        const auto input = std::find_if(
            gpioSettings.inputs_.begin(), gpioSettings.inputs_.end(),
            [&binding](const GpioInputConfiguration &candidate)
            { return candidate.id_ == binding.inputId_; });
        if (input != gpioSettings.inputs_.end() &&
            input->inputType() == GpioInputType::Navigation)
            return true;
        const GpioEncoderRole role = roleForInput(binding.inputId_);
        if (role == GpioEncoderRole::None)
            return false;
        return binding.eventType() == GpioBindingEventType::EncoderTurn ||
               role == GpioEncoderRole::PresetBrowser ||
               role == GpioEncoderRole::ParameterScroll;
    };
    if (std::none_of(bindings.begin(), bindings.end(), isUnreachable))
        return;

    std::vector<GpioBinding> kept;
    kept.reserve(bindings.size());
    for (const auto &binding : bindings)
    {
        if (!isUnreachable(binding))
            kept.push_back(binding);
    }
    pedalboard.gpioBindings(std::move(kept));
}

void PiPedalModel::CarryGpioScrollForward(Pedalboard &pedalboard)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (pedalboard.gpioScrollInstanceId() == -1 && pedalboard.gpioScrollSymbol().empty())
    {
        pedalboard.gpioScrollInstanceId(this->pedalboard.gpioScrollInstanceId());
        pedalboard.gpioScrollSymbol(this->pedalboard.gpioScrollSymbol());
    }
}

// One detent moves a parameter by its own natural step: between declared scale
// points, one unit for integers, one declared step where a port declares them,
// and otherwise a configurable fraction of the whole range.
void PiPedalModel::AdjustGpioParameter(const GpioParameter &parameter, int32_t delta)
{
    if (delta == 0)
        return;

    std::shared_ptr<Lv2PortInfo> port;
    float currentValue = 0.0f;
    int32_t stepsPerRange = 100;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        stepsPerRange = gpioSettings.encoderStepsPerRange_;
        auto *item = pedalboard.GetItem(parameter.instanceId);
        if (!item)
            return;
        auto plugin = GetPluginInfo(item->uri_);
        if (!plugin)
            return;
        for (const auto &candidate : plugin->ports())
        {
            if (candidate && candidate->symbol() == parameter.symbol)
            {
                port = candidate;
                break;
            }
        }
        if (!port)
            return;
        currentValue = port->default_value();
        if (auto *control = item->GetControlValue(parameter.symbol))
            currentValue = control->value();
    }
    // Not std::isfinite: release builds are -ffast-math. See IsFiniteGpioValue.
    if (!IsFiniteGpioValue(currentValue))
        currentValue = port->min_value();

    float targetValue;
    std::vector<float> scalePointValues;
    if (port->enumeration_property())
    {
        for (const auto &point : port->scale_points())
        {
            if (IsFiniteGpioValue(point.value()))
                scalePointValues.push_back(point.value());
        }
    }
    if (scalePointValues.size() >= 2)
    {
        std::sort(scalePointValues.begin(), scalePointValues.end());
        scalePointValues.erase(
            std::unique(scalePointValues.begin(), scalePointValues.end()),
            scalePointValues.end());
        auto nearest = std::min_element(
            scalePointValues.begin(), scalePointValues.end(),
            [currentValue](float left, float right)
            { return std::abs(left - currentValue) < std::abs(right - currentValue); });
        int64_t index = std::distance(scalePointValues.begin(), nearest) + delta;
        index = std::clamp<int64_t>(
            index, 0, static_cast<int64_t>(scalePointValues.size()) - 1);
        targetValue = scalePointValues[static_cast<size_t>(index)];
    }
    else
    {
        float steps;
        if (port->range_steps() >= 2)
            steps = static_cast<float>(port->range_steps() - 1);
        else if (port->toggled_property())
            steps = 1.0f;
        else if (port->integer_property())
            steps = std::max(1.0f, std::round(port->max_value() - port->min_value()));
        else
            steps = static_cast<float>(std::max(4, stepsPerRange));
        float range = GpioValueToRange(*port, currentValue) +
                      static_cast<float>(delta) / steps;
        targetValue = port->rangeToValue(std::clamp(range, 0.0f, 1.0f));
    }
    if (!IsFiniteGpioValue(targetValue))
        return;
    SetControl(-1, parameter.instanceId, parameter.symbol, targetValue);
}

void PiPedalModel::ShowGpioNavigationLayer()
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (!gpioManager)
        return;
    if (gpioNavigationLayer == GpioNavigationLayer::Parameters)
    {
        UpdateGpioDashboard(0, true);
        return;
    }

    GpioDisplayMenu menu;
    if (gpioNavigationLayer == GpioNavigationLayer::Settings)
    {
        static constexpr std::array<const char *, 4> passiveNames{
            "PARAMETERS", "WAVEFORM", "TUNER", "BLANK"};
        static constexpr std::array<const char *, 2> ledModeNames{
            "SPECTRUM", "DROPLETS"};
        const int32_t passive = std::clamp<int32_t>(
            gpioSettings.display_.passiveMode_, 0, 3);
        menu.title = "SETTINGS";
        menu.items = {
            "PASSIVE: " + std::string(passiveNames[static_cast<size_t>(passive)]),
            "LED MODE: " + std::string(ledModeNames[static_cast<size_t>(
                std::clamp<int32_t>(gpioSettings.ledMatrix_.mode_, 0, 1))]),
            "LED BRIGHT: " + std::to_string(gpioSettings.ledMatrix_.brightness_),
            "LED FLOOR: " + std::to_string(
                static_cast<int32_t>(std::lround(gpioSettings.ledMatrix_.floorDb_))) + "DB",
            "LED CAL: " + std::string(
                gpioSettings.ledMatrix_.calibrationMode_ ? "ON" : "OFF")};
        menu.selectedIndex = gpioSettingsMenuIndex;
    }
    else if (gpioNavigationLayer == GpioNavigationLayer::Presets)
    {
        PresetIndex index;
        storage.GetPresetIndex(&index);
        menu.title = "PRESETS";
        const int64_t selectedId = gpioPendingPresetId != -1
                                       ? gpioPendingPresetId
                                       : index.selectedInstanceId();
        for (size_t i = 0; i < index.presets().size(); ++i)
        {
            menu.items.push_back(index.presets()[i].name());
            if (index.presets()[i].instanceId() == selectedId)
                menu.selectedIndex = static_cast<int32_t>(i);
        }
    }
    else
    {
        EnsureGpioSelectedEffect();
        const auto effects = GetGpioEffects();
        menu.title = "EFFECTS";
        for (size_t i = 0; i < effects.size(); ++i)
        {
            menu.items.push_back(effects[i].second);
            if (effects[i].first == gpioSelectedEffectId)
                menu.selectedIndex = static_cast<int32_t>(i);
        }
    }
    gpioManager->ShowTemporaryMenu(menu);
}

void PiPedalModel::MoveGpioNavigationSelection(int32_t delta)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (delta == 0)
        return;
    const int32_t direction = delta > 0 ? 1 : -1;
    if (gpioNavigationLayer == GpioNavigationLayer::Settings)
    {
        constexpr int32_t count = 5;
        gpioSettingsMenuIndex = (gpioSettingsMenuIndex + direction + count) % count;
    }
    else if (gpioNavigationLayer == GpioNavigationLayer::Presets)
    {
        PresetIndex index;
        storage.GetPresetIndex(&index);
        if (!index.presets().empty())
        {
            const int64_t selectedId = gpioPendingPresetId != -1
                                           ? gpioPendingPresetId
                                           : index.selectedInstanceId();
            auto selected = std::find_if(
                index.presets().begin(), index.presets().end(),
                [selectedId](const PresetIndexEntry &entry)
                { return entry.instanceId() == selectedId; });
            int64_t position = selected == index.presets().end()
                                   ? 0
                                   : std::distance(index.presets().begin(), selected);
            position = (position + direction +
                        static_cast<int64_t>(index.presets().size())) %
                       static_cast<int64_t>(index.presets().size());
            gpioPendingPresetId = index.presets()[static_cast<size_t>(position)].instanceId();
        }
    }
    else if (gpioNavigationLayer == GpioNavigationLayer::Effects)
    {
        const auto effects = GetGpioEffects();
        if (!effects.empty())
        {
            EnsureGpioSelectedEffect();
            auto selected = std::find_if(
                effects.begin(), effects.end(),
                [this](const auto &effect) { return effect.first == gpioSelectedEffectId; });
            int64_t position = selected == effects.end()
                                   ? 0
                                   : std::distance(effects.begin(), selected);
            position = (position + direction + static_cast<int64_t>(effects.size())) %
                       static_cast<int64_t>(effects.size());
            gpioSelectedEffectId = effects[static_cast<size_t>(position)].first;
        }
    }
    else
    {
        auto parameters = GetGpioParameters();
        if (!parameters.empty())
        {
            const int64_t positions = static_cast<int64_t>(GpioScrollPositions(parameters.size()));
            int64_t index =
                (static_cast<int64_t>(GetGpioScrollIndex(parameters)) + direction) % positions;
            if (index < 0)
                index += positions;
            SetGpioScrollIndex(parameters, static_cast<size_t>(index));
        }
    }
    ShowGpioNavigationLayer();
}

void PiPedalModel::AcceptGpioNavigationSelection()
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (gpioNavigationLayer == GpioNavigationLayer::Settings)
    {
        bool reconfigure = false;
        if (gpioSettingsMenuIndex == 0)
        {
            int32_t next = (gpioSettings.display_.passiveMode_ + 1) % 4;
            if (next == static_cast<int32_t>(GpioDisplayMode::Waveform) &&
                !gpioSettings.display_.waveformEnabled_)
                next = static_cast<int32_t>(GpioDisplayMode::Tuner);
            gpioSettings.display_.passiveMode_ = next;
            if (gpioManager)
                gpioManager->SetDisplayMode(static_cast<GpioDisplayMode>(next));
        }
        else if (gpioSettingsMenuIndex == 1)
        {
            gpioSettings.ledMatrix_.mode_ =
                (gpioSettings.ledMatrix_.mode_ + 1) % 2;
            reconfigure = true;
        }
        else if (gpioSettingsMenuIndex == 2)
        {
            gpioSettings.ledMatrix_.brightness_ =
                (gpioSettings.ledMatrix_.brightness_ + 1) % 16;
            reconfigure = true;
        }
        else if (gpioSettingsMenuIndex == 3)
        {
            gpioSettings.ledMatrix_.floorDb_ += 6.0f;
            if (gpioSettings.ledMatrix_.floorDb_ > -18.0f)
                gpioSettings.ledMatrix_.floorDb_ = -60.0f;
            reconfigure = true;
        }
        else if (gpioSettingsMenuIndex == 4)
        {
            gpioSettings.ledMatrix_.calibrationMode_ =
                !gpioSettings.ledMatrix_.calibrationMode_;
            reconfigure = true;
        }
        storage.SetGpioSettings(gpioSettings);
        if (reconfigure && gpioManager)
        {
            ConfigureGpioManager();
            UpdateGpioDashboard();
        }
        FireGpioSettingsChanged();
        ShowGpioNavigationLayer();
        return;
    }

    if (gpioNavigationLayer == GpioNavigationLayer::Presets)
    {
        PresetIndex index;
        storage.GetPresetIndex(&index);
        const int64_t targetId = gpioPendingPresetId != -1
                                     ? gpioPendingPresetId
                                     : index.selectedInstanceId();
        if (targetId != -1)
            LoadPreset(-1, targetId);
        gpioPendingPresetId = -1;
        gpioSelectedEffectId = -1;
        ShowGpioNavigationLayer();
        return;
    }

    if (gpioNavigationLayer == GpioNavigationLayer::Effects)
    {
        AdvanceGpioNavigationLayer();
        return;
    }

    ShowGpioNavigationLayer();
}

void PiPedalModel::AdvanceGpioNavigationLayer()
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (gpioNavigationLayer == GpioNavigationLayer::Settings)
    {
        // Right moves inward but never applies the highlighted setting.
        gpioPendingPresetId = -1;
        gpioNavigationLayer = GpioNavigationLayer::Presets;
    }
    else if (gpioNavigationLayer == GpioNavigationLayer::Presets)
    {
        // Enter the effects belonging to the already loaded preset. The
        // highlighted preset remains only a browsing cursor until Select.
        gpioPendingPresetId = -1;
        gpioSelectedEffectId = -1;
        EnsureGpioSelectedEffect();
        gpioNavigationLayer = GpioNavigationLayer::Effects;
    }
    else if (gpioNavigationLayer == GpioNavigationLayer::Effects)
    {
        EnsureGpioSelectedEffect();
        const auto effectParameters = GetGpioParameters(gpioSelectedEffectId);
        const auto allParameters = GetGpioParameters();
        if (!effectParameters.empty())
        {
            auto first = std::find(
                allParameters.begin(), allParameters.end(), effectParameters.front());
            if (first != allParameters.end())
                SetGpioScrollIndex(
                    allParameters,
                    static_cast<size_t>(std::distance(allParameters.begin(), first)));
        }
        gpioNavigationLayer = GpioNavigationLayer::Parameters;
    }
    ShowGpioNavigationLayer();
}

void PiPedalModel::BackGpioNavigationLayer()
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (gpioNavigationLayer != GpioNavigationLayer::Settings)
    {
        gpioNavigationLayer = static_cast<GpioNavigationLayer>(
            static_cast<int32_t>(gpioNavigationLayer) - 1);
        if (gpioNavigationLayer == GpioNavigationLayer::Effects)
            EnsureGpioSelectedEffect();
    }
    ShowGpioNavigationLayer();
}

void PiPedalModel::HandleGpioNavigationEvent(const GpioInputEvent &event)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (event.initial)
        return;
    if (event.eventType == GpioInputEventType::EncoderTurn)
    {
        MoveGpioNavigationSelection(event.delta);
        return;
    }
    if (!event.risingEdge)
        return;
    switch (event.navigationButton)
    {
    case GpioNavigationButton::Up:
        MoveGpioNavigationSelection(-1);
        break;
    case GpioNavigationButton::Down:
        MoveGpioNavigationSelection(1);
        break;
    case GpioNavigationButton::Left:
        BackGpioNavigationLayer();
        break;
    case GpioNavigationButton::Right:
        AdvanceGpioNavigationLayer();
        break;
    case GpioNavigationButton::Select:
        AcceptGpioNavigationSelection();
        break;
    default:
        break;
    }
}

bool PiPedalModel::HandleGpioRoleEvent(
    const GpioInputEvent &event,
    GpioEncoderRole role)
{
    if (role == GpioEncoderRole::None)
        return false;

    // Initial button state is useful to the status UI, but role actions are
    // always driven by actual turns and rising button edges.
    if (event.initial)
        return true;

    // These roles exist only so settings written by the old workflow remain
    // parseable until EnsureStandardEncoderRoles migrates them.
    if (role == GpioEncoderRole::PresetBrowser ||
        role == GpioEncoderRole::ParameterScroll)
        return true;

    if (role >= GpioEncoderRole::Parameter1 && role <= GpioEncoderRole::Parameter4 &&
        event.eventType == GpioInputEventType::EncoderTurn)
    {
        if (event.delta == 0)
            return true;
        const int32_t slot = static_cast<int32_t>(role) -
                             static_cast<int32_t>(GpioEncoderRole::Parameter1) + 1;
        auto parameters = GetGpioParameters();
        const size_t index = GetGpioScrollIndex(parameters) + slot - 1;
        if (index >= parameters.size())
        {
            ShowGpioWorkflowMessage(
                "PARAMETER",
                parameters.empty() ? "NONE IN THIS EFFECT" : "NO PARAMETER IN SLOT",
                "");
            return true;
        }
        AdjustGpioParameter(parameters[index], event.delta);
        UpdateGpioDashboard(slot, true);
        return true;
    }

    // Parameter encoder buttons remain available to advanced mappings.
    return false;
}

void PiPedalModel::HandleGpioInputEvent(const GpioInputEvent &event)
{
    std::vector<GpioBinding> bindings;
    GpioEncoderRole inputRole = GpioEncoderRole::None;
    GpioInputType inputType = GpioInputType::Momentary;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        bindings = pedalboard.gpioBindings();
        for (const auto &input : gpioSettings.inputs_)
        {
            if (input.id_ == event.inputId)
            {
                inputRole = input.encoderRole();
                inputType = input.inputType();
                break;
            }
        }
    }

    if (inputType == GpioInputType::Navigation)
    {
        HandleGpioNavigationEvent(event);
        return;
    }

    if (HandleGpioRoleEvent(event, inputRole))
        return;

    for (const auto &binding : bindings)
    {
        if (binding.enabled_ && binding.inputId_ == event.inputId &&
            static_cast<GpioInputEventType>(binding.eventType_) == event.eventType)
        {
            ExecuteGpioBinding(binding, event);
        }
    }
}

void PiPedalModel::ExecuteGpioBinding(const GpioBinding &binding, const GpioInputEvent &event)
{
    const auto action = binding.actionType();
    const auto mode = binding.mode();

    // Commands are deliberately rising-edge-only. In particular, this prevents
    // a held switch or startup refresh from repeatedly loading presets.
    bool pressedEdge = event.risingEdge;
    bool isContinuousAction = action == GpioActionType::Control || action == GpioActionType::Bypass;

    if (!isContinuousAction)
    {
        if (!pressedEdge)
        {
            return;
        }
        switch (action)
        {
        case GpioActionType::LoadPreset:
            LoadPreset(-1, binding.targetId_);
            break;
        case GpioActionType::NextPreset:
            NextPreset();
            break;
        case GpioActionType::PreviousPreset:
            PreviousPreset();
            break;
        case GpioActionType::SelectSnapshot:
            SetSnapshot(binding.targetId_);
            break;
        case GpioActionType::NextSnapshot:
            NextSnapshot();
            break;
        case GpioActionType::PreviousSnapshot:
            PreviousSnapshot();
            break;
        case GpioActionType::NextBank:
            NextBank();
            break;
        case GpioActionType::PreviousBank:
            PreviousBank();
            break;
        default:
            break;
        }
        return;
    }

    if ((mode == GpioBindingMode::Toggle || mode == GpioBindingMode::Trigger) && !pressedEdge)
    {
        return;
    }

    if (action == GpioActionType::Bypass)
    {
        if (mode == GpioBindingMode::Relative)
        {
            if (event.delta == 0) return;
            bool enabled = event.delta > 0;
            if (binding.maxValue_ < binding.minValue_) enabled = !enabled;
            SetPedalboardItemEnable(-1, binding.instanceId_, enabled);
            ShowGpioBindingValue(binding, enabled ? 1.0f : 0.0f);
            return;
        }
        PedalboardItem *item = nullptr;
        {
            std::lock_guard<std::recursive_mutex> lock(mutex);
            item = pedalboard.GetItem(binding.instanceId_);
            if (!item)
            {
                return;
            }
            if (mode == GpioBindingMode::Toggle)
            {
                bool enabled = !item->isEnabled();
                SetPedalboardItemEnable(-1, binding.instanceId_, enabled);
                ShowGpioBindingValue(binding, enabled ? 1.0f : 0.0f);
                return;
            }
        }
        float normalized = std::clamp(event.value, 0.0f, 1.0f);
        float mapped = binding.minValue_ + (binding.maxValue_ - binding.minValue_) * normalized;
        float midpoint = (binding.minValue_ + binding.maxValue_) * 0.5f;
        bool enabled = mapped >= midpoint;
        SetPedalboardItemEnable(-1, binding.instanceId_, enabled);
        ShowGpioBindingValue(binding, enabled ? 1.0f : 0.0f);
        return;
    }

    float targetValue;
    if (mode == GpioBindingMode::Relative)
    {
        float currentValue = binding.minValue_;
        std::vector<float> scalePointValues;
        bool integerValue = false;
        {
            std::lock_guard<std::recursive_mutex> lock(mutex);
            if (binding.instanceId_ == Pedalboard::START_CONTROL_ID)
                currentValue = pedalboard.input_volume_db();
            else if (binding.instanceId_ == Pedalboard::END_CONTROL_ID)
                currentValue = pedalboard.output_volume_db();
            else if (auto *item = pedalboard.GetItem(binding.instanceId_))
            {
                if (auto *control = item->GetControlValue(binding.symbol_)) currentValue = control->value();
                auto plugin = GetPluginInfo(item->uri_);
                if (plugin)
                {
                    for (const auto &port : plugin->ports())
                    {
                        if (port->symbol() != binding.symbol_) continue;
                        integerValue = port->integer_property();
                        for (const auto &point : port->scale_points())
                        {
                            float low = std::min(binding.minValue_, binding.maxValue_);
                            float high = std::max(binding.minValue_, binding.maxValue_);
                            if (point.value() >= low && point.value() <= high)
                                scalePointValues.push_back(point.value());
                        }
                        break;
                    }
                }
            }
        }
        float direction = binding.maxValue_ >= binding.minValue_ ? 1.0f : -1.0f;
        if (!scalePointValues.empty())
        {
            std::sort(scalePointValues.begin(), scalePointValues.end());
            scalePointValues.erase(std::unique(scalePointValues.begin(), scalePointValues.end()), scalePointValues.end());
            auto nearest = std::min_element(
                scalePointValues.begin(), scalePointValues.end(),
                [currentValue](float left, float right)
                { return std::abs(left - currentValue) < std::abs(right - currentValue); });
            int64_t index = std::distance(scalePointValues.begin(), nearest);
            index += static_cast<int64_t>(event.delta) * (direction > 0 ? 1 : -1);
            index = std::clamp<int64_t>(index, 0, static_cast<int64_t>(scalePointValues.size()) - 1);
            targetValue = scalePointValues[static_cast<size_t>(index)];
        }
        else
        {
            targetValue = currentValue + static_cast<float>(event.delta) * binding.stepValue_ * direction;
            if (integerValue) targetValue = std::round(targetValue);
            targetValue = std::clamp(targetValue,
                                     std::min(binding.minValue_, binding.maxValue_),
                                     std::max(binding.minValue_, binding.maxValue_));
        }
    }
    else if (mode == GpioBindingMode::Direct)
    {
        float normalized = std::clamp(event.value, 0.0f, 1.0f);
        normalized = std::pow(normalized, binding.curve_);
        targetValue = binding.minValue_ + (binding.maxValue_ - binding.minValue_) * normalized;
    }
    else if (mode == GpioBindingMode::Trigger)
    {
        targetValue = binding.maxValue_;
    }
    else
    {
        float currentValue = binding.minValue_;
        {
            std::lock_guard<std::recursive_mutex> lock(mutex);
            if (binding.instanceId_ == Pedalboard::START_CONTROL_ID)
            {
                currentValue = pedalboard.input_volume_db();
            }
            else if (binding.instanceId_ == Pedalboard::END_CONTROL_ID)
            {
                currentValue = pedalboard.output_volume_db();
            }
            else if (auto *item = pedalboard.GetItem(binding.instanceId_))
            {
                if (auto *control = item->GetControlValue(binding.symbol_))
                {
                    currentValue = control->value();
                }
            }
        }
        targetValue = std::abs(currentValue - binding.minValue_) <= std::abs(currentValue - binding.maxValue_)
                          ? binding.maxValue_
                          : binding.minValue_;
    }

    if (binding.instanceId_ == Pedalboard::START_CONTROL_ID)
    {
        SetInputVolume(targetValue);
    }
    else if (binding.instanceId_ == Pedalboard::END_CONTROL_ID)
    {
        SetOutputVolume(targetValue);
    }
    else
    {
        // Changing a split's routing type rebuilds the pedalboard and causes a
        // GPIO refresh. Refuse a hand-authored recursive mapping; the UI does
        // not offer split routing controls as GPIO targets.
        {
            std::lock_guard<std::recursive_mutex> lock(mutex);
            auto *item = pedalboard.GetItem(binding.instanceId_);
            if (item && item->isSplit() && binding.symbol_ == SPLIT_SPLITTYPE_KEY)
            {
                return;
            }
        }
        SetControl(-1, binding.instanceId_, binding.symbol_, targetValue);
    }
    ShowGpioBindingValue(binding, targetValue);
}

// Overrides are supplied by advanced mappings, which carry their own range and
// step. A scrolled parameter has none, and uses the port's own scale.
GpioDisplayControl PiPedalModel::BuildGpioDisplayControl(
    int64_t instanceId,
    const std::string &symbol,
    std::optional<float> suppliedValue,
    std::optional<float> minimumOverride,
    std::optional<float> maximumOverride,
    std::optional<float> stepOverride)
{
    GpioDisplayControl result;
    result.assigned = true;
    result.label = symbol;
    float value = suppliedValue.value_or(0.0f);
    float portMinimum = 0.0f;
    float portMaximum = 1.0f;
    float portStep = 0.01f;
    std::vector<Lv2ScalePoint> displayScalePoints;
    Units displayUnits = Units::none;
    std::string displayCustomUnits;
    bool hasPortInfo = false;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        if (instanceId == Pedalboard::START_CONTROL_ID)
        {
            result.effectName = "INPUT";
            result.label = "LEVEL";
            portMinimum = -60.0f;
            portMaximum = 12.0f;
            portStep = 1.0f;
            if (!suppliedValue) value = pedalboard.input_volume_db();
        }
        else if (instanceId == Pedalboard::END_CONTROL_ID)
        {
            result.effectName = "OUTPUT";
            result.label = "LEVEL";
            portMinimum = -60.0f;
            portMaximum = 12.0f;
            portStep = 1.0f;
            if (!suppliedValue) value = pedalboard.output_volume_db();
        }
        else if (auto *item = pedalboard.GetItem(instanceId))
        {
            result.effectName = item->title_.empty() ? item->pluginName_ : item->title_;
            if (!suppliedValue)
                if (auto *control = item->GetControlValue(symbol)) value = control->value();
            auto plugin = GetPluginInfo(item->uri_);
            if (plugin)
            {
                for (const auto &port : plugin->ports())
                {
                    if (port && port->symbol() == symbol)
                    {
                        hasPortInfo = true;
                        displayScalePoints = port->scale_points();
                        displayUnits = port->units();
                        displayCustomUnits = port->custom_units();
                        result.label = port->name();
                        portMinimum = port->min_value();
                        portMaximum = port->max_value();
                        const float portRange = portMaximum - portMinimum;
                        portStep =
                            port->range_steps() >= 2
                                ? std::abs(portRange) / (port->range_steps() - 1)
                            : port->integer_property() || port->toggled_property() ||
                                    port->enumeration_property()
                                ? 1.0f
                                : std::abs(portRange) /
                                      std::max(4, gpioSettings.encoderStepsPerRange_);
                        break;
                    }
                }
            }
        }
    }

    const float minimum =
        ResolveGpioDisplayValue(minimumOverride.value_or(portMinimum), 0.0f, 0.0f);
    const float maximum = ResolveGpioDisplayValue(
        maximumOverride.value_or(portMaximum), minimum + 1.0f, minimum + 1.0f);
    float step = ResolveGpioDisplayValue(stepOverride.value_or(portStep), 0.01f, 0.01f);
    if (step <= 0.0f) step = 0.01f;
    value = ResolveGpioDisplayValue(suppliedValue, value, minimum);
    bool usedScalePoint = false;
    if (hasPortInfo)
    {
        for (const auto &point : displayScalePoints)
        {
            if (std::abs(point.value() - value) < 0.0001f)
            {
                result.value = point.label();
                usedScalePoint = true;
                break;
            }
        }
    }
    if (!usedScalePoint)
    {
        int precision = step >= 1.0f ? 0 : step >= 0.1f ? 1 :
                        step >= 0.01f ? 2 : 3;
        std::ostringstream stream;
        stream << std::fixed << std::setprecision(precision) << value;
        if (hasPortInfo)
        {
            std::string units = displayUnits == Units::custom
                                    ? displayCustomUnits : UnitsToString(displayUnits);
            if (!units.empty() && units != "none" && units != "unknown") stream << " " << units;
        }
        else if (instanceId == Pedalboard::START_CONTROL_ID ||
                 instanceId == Pedalboard::END_CONTROL_ID)
        {
            stream << " dB";
        }
        result.value = stream.str();
    }
    float range = ResolveGpioDisplayValue(
        maximum - minimum, 0.0f, 0.0f);
    if (range != 0.0f)
    {
        result.normalizedValue = std::clamp(
            (value - minimum) / range, 0.0f, 1.0f);
    }
    return result;
}

void PiPedalModel::UpdateGpioDashboard(int32_t activeSlot, bool temporary)
{
    if (!gpioManager)
        return;

    auto parameters = GetGpioParameters();
    GpioDisplayDashboard dashboard;
    std::array<GpioParameter, 4> shown;
    if (!parameters.empty())
    {
        const size_t index = GetGpioScrollIndex(parameters);
        dashboard.scrollIndex = static_cast<int32_t>(index);
        dashboard.scrollPositions =
            static_cast<int32_t>(GpioScrollPositions(parameters.size()));
        for (size_t slot = 0; slot < dashboard.controls.size(); ++slot)
        {
            if (index + slot >= parameters.size())
                break;
            shown[slot] = parameters[index + slot];
            dashboard.controls[slot] = BuildGpioDisplayControl(
                shown[slot].instanceId, shown[slot].symbol, std::nullopt);
        }
        dashboard.activeSlot =
            activeSlot >= 1 && activeSlot <= 4 &&
                    dashboard.controls[activeSlot - 1].assigned
                ? activeSlot
                : 0;
    }
    {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        gpioShownParameters = shown;
    }
    if (temporary)
        gpioManager->ShowTemporaryControlDashboard(dashboard);
    else
        gpioManager->ShowControlDashboard(dashboard);
}

void PiPedalModel::ShowGpioBindingValue(
    const GpioBinding &binding,
    std::optional<float> suppliedValue)
{
    if (!gpioManager) return;
    GpioDisplayMessage message;
    message.title = "PIPEDAL";
    message.label = binding.symbol_;

    if (binding.actionType() == GpioActionType::Bypass)
    {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        float value = suppliedValue.value_or(0.0f);
        if (auto *item = pedalboard.GetItem(binding.instanceId_))
        {
            message.title = item->title_.empty() ? item->pluginName_ : item->title_;
            if (!suppliedValue)
                value = item->isEnabled() ? 1.0f : 0.0f;
        }
        message.label = "EFFECT";
        message.value = value >= 0.5f ? "ON" : "BYPASSED";
        message.normalizedValue = value >= 0.5f ? 1.0f : 0.0f;
        message.hasNormalizedValue = true;
        gpioManager->ShowDisplayMessage(message);
        return;
    }

    GpioDisplayControl control = BuildGpioDisplayControl(
        binding.instanceId_, binding.symbol_, suppliedValue,
        binding.minValue_, binding.maxValue_, binding.stepValue_);
    if (!control.effectName.empty())
        message.title = control.effectName;
    message.label = control.label;
    message.value = control.value;
    message.normalizedValue = control.normalizedValue;
    message.hasNormalizedValue = true;
    gpioManager->ShowDisplayMessage(message);
}

PedalboardItem *PiPedalModel::GetPedalboardItemForFileProperty(const UiFileProperty &fileProperty)
{
    for (PedalboardItem *pedalboardItem : this->pedalboard.GetAllPlugins())
    {
        if (pedalboardItem->pathProperties_.contains(fileProperty.patchProperty()))
        {
            return pedalboardItem;
        }
    }
    return nullptr;
}
FileRequestResult PiPedalModel::GetFileList2(const std::string &relativePath_, const UiFileProperty &fileProperty)
{
    std::string relativePath = relativePath_;
    try
    {
        if (!storage.IsInUploadsDirectory(relativePath))
        {

            // if relativePath is in a resource directory of the plugin, then we have loaded a factory preset or are using a default property.
            // map the resource path to the corresponding file in the uploads directory.
            // :-(
            PedalboardItem *pedalboardItem = GetPedalboardItemForFileProperty(fileProperty);
            if (pedalboardItem)
            {
                auto pluginInfo = GetPluginInfo(pedalboardItem->uri());
                if (pluginInfo)
                {
                    std::filesystem::path resourcePath = fs::path(pluginInfo->bundle_path()) / fileProperty.resourceDirectory();
                    if (IsSubdirectory(relativePath, resourcePath))
                    {
                        fs::path t = MakeRelativePath(relativePath, resourcePath);
                        t = fileProperty.directory() / t;
                        if (fs::exists(t))
                        {
                            relativePath = t;
                        }
                    }
                }
            }
        }
        return this->storage.GetFileList2(relativePath, fileProperty);
    }
    catch (const std::exception &e)
    {
        Lv2Log::warning("GetFileList() failed:  (%s)", e.what());
        throw;
    }
}

std::string PiPedalModel::RenameFilePropertyFile(
    const std::string &oldRelativePath,
    const std::string &newRelativePath,
    const UiFileProperty &uiFileProperty)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    return storage.RenameFilePropertyFile(oldRelativePath, newRelativePath, uiFileProperty);
}

std::string PiPedalModel::CopyFilePropertyFile(
    const std::string &oldRelativePath,
    const std::string &newRelativePath,
    const UiFileProperty &uiFileProperty,
    bool overwrite)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    return storage.CopyFilePropertyFile(oldRelativePath, newRelativePath, uiFileProperty, overwrite);
}

void PiPedalModel::DeleteSampleFile(const std::filesystem::path &fileName)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    storage.DeleteSampleFile(fileName);
}

std::string PiPedalModel::CreateNewSampleDirectory(const std::string &relativePath, const UiFileProperty &uiFileProperty)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    return storage.CreateNewSampleDirectory(relativePath, uiFileProperty);
}
FilePropertyDirectoryTree::ptr PiPedalModel::GetFilePropertydirectoryTree(const UiFileProperty &uiFileProperty, const std::string &selectedPath)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    return storage.GetFilePropertydirectoryTree(uiFileProperty, selectedPath);
}

UiFileProperty::ptr PiPedalModel::FindLoadedPatchProperty(int64_t instanceId, const std::string &patchPropertyUri)
{

    auto pedalboardItems = pedalboard.GetAllPlugins();

    for (const auto &pedalboardItem : pedalboardItems)
    {
        if (pedalboardItem->instanceId() == instanceId)
        {
            Lv2PluginInfo::ptr pluginInfo = GetPluginInfo(pedalboardItem->uri());
            if (pluginInfo && pluginInfo->piPedalUI())
            {
                for (const auto &fileProperty : pluginInfo->piPedalUI()->fileProperties())
                    if (fileProperty->patchProperty() == patchPropertyUri)
                    {
                        return fileProperty;
                    }
            }
        }
    }
    return nullptr;
    throw std::runtime_error("Permission denied. Plugin not currently loaded.");
}

std::string PiPedalModel::UploadUserFile(const std::string &directory, int64_t instanceId, const std::string &patchProperty, const std::string &filename, std::istream &stream, size_t contentLength)
{
    UiFileProperty::ptr fileProperty = FindLoadedPatchProperty(instanceId, patchProperty);
    if (!fileProperty)
    {
        Lv2Log::error(SS("Upload fle: Permission denied. No currently-loaded plugin provides that patch property: " << patchProperty));
        throw std::runtime_error("Permission denied.");
    }
    return storage.UploadUserFile(directory, fileProperty, filename, stream, contentLength);
}

uint64_t PiPedalModel::CreateNewPreset()
{
    std::lock_guard<std::recursive_mutex> lock(mutex);

    return storage.CreateNewPreset();
}

void PiPedalModel::CheckForResourceInitialization(Pedalboard &pedalboard)
{
    for (auto item : pedalboard.GetAllPlugins())
    {
        if (!item->isSplit())
        {
            pluginHost.CheckForResourceInitialization(item->uri(), storage.GetPluginUploadDirectory());
        }
    }
}
Pedalboard &PiPedalModel::GetPedalboard()
{
    return this->pedalboard;
}
std::shared_ptr<Lv2Pedalboard> PiPedalModel::GetLv2Pedalboard()
{
    // test only.
    Lv2PedalboardErrorList errorMessages;
    std::shared_ptr<Lv2Pedalboard> lv2Pedalboard{this->pluginHost.CreateLv2Pedalboard(this->pedalboard, errorMessages)};
    if (errorMessages.size() != 0)
    {
        throw std::runtime_error(errorMessages[0].message);
    }
    return lv2Pedalboard;
}

void PiPedalModel::SetSelectedPedalboardPlugin(uint64_t clientId, uint64_t pedalboardId)
{
    // Thinking on this:
    // 1) do NOT mark the pedalboard as changed. This shouldn't set a change flag.
    // 2) do NOT broadcast the change. Whoever set it last controls what happens when the plugin is reloaded. Meh.
    // 3) Clients must be able to save a non-changed pedalboard.
    pedalboard.selectedPlugin(pedalboardId);
}

bool PiPedalModel::LoadCurrentPedalboard()
{
    CrashGuardLock crashGuardLock;
    if (previousPedalboardLoaded && pedalboard.IsStructureIdentical(previousPedalboard))
    {
        // then we can send a snapshot update instead!
        Snapshot snapshot = pedalboard.MakeSnapshotFromCurrentSettings(previousPedalboard);
        audioHost->LoadSnapshot(snapshot, pluginHost);
        this->previousPedalboard = this->pedalboard;
        return true;
    }

    Lv2PedalboardErrorList errorMessages;
    std::shared_ptr<Lv2Pedalboard> lv2Pedalboard{this->pluginHost.CreateLv2Pedalboard(this->pedalboard, errorMessages)};
    this->lv2Pedalboard = lv2Pedalboard;

    // apply the error messages to the lv2Pedalboard.
    // return true if the error messages have changed
    CheckForResourceInitialization(this->pedalboard);
    audioHost->SetPedalboard(lv2Pedalboard);
    previousPedalboard = this->pedalboard;
    previousPedalboardLoaded = true;
    return true;
}

void PiPedalModel::OnNotifyLv2RealtimeError(int64_t instanceId, const std::string &error)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);

    // Notify clients.
    size_t n = subscribers.size();
    std::vector<IPiPedalModelSubscriber::ptr> t{subscribers.begin(), subscribers.end()};
    for (auto &subscriber : t)
    {
        subscriber->OnErrorMessage(error);
    }
}
std::filesystem::path PiPedalModel::GetPluginUploadDirectory() const
{
    return storage.GetPluginUploadDirectory();
}

void PiPedalModel::OnLv2PluginsChanged()
{
    Lv2Log::info("Lv2 plugins have changed. Reloading plugins.");
    std::lock_guard<std::recursive_mutex> lock(mutex);
    {
        // Notify clients.
        std::vector<IPiPedalModelSubscriber::ptr> t{subscribers.begin(), subscribers.end()};
        for (auto &subscriber : t)
        {
            subscriber->OnLv2PluginsChanging();
        }
    }
    std::thread(
        [this]()
        {
            // wait for the message to propagate. It would be better to use some kind of flush()
            // operation, but it's not clear how to do that with asyncio.
            std::this_thread::sleep_for(std::chrono::milliseconds(2000));
            restartListener();
        })
        .detach();
}
void PiPedalModel::SetRestartListener(std::function<void(void)> &&listener)
{
    this->restartListener = std::move(listener);
}

void PiPedalModel::OnUpdateStatusChanged(const UpdateStatus &updateStatus)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);

    if (this->currentUpdateStatus != updateStatus)
    {
        this->currentUpdateStatus = updateStatus;
        FireUpdateStatusChanged(this->currentUpdateStatus);
    }
}
void PiPedalModel::FireUpdateStatusChanged(const UpdateStatus &updateStatus)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);

    std::vector<IPiPedalModelSubscriber::ptr> t{subscribers.begin(), subscribers.end()};
    for (auto &subscriber : t)
    {
        subscriber->OnUpdateStatusChanged(updateStatus);
    }
}
UpdateStatus PiPedalModel::GetUpdateStatus()
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    return updater->GetCurrentStatus();
}

void PiPedalModel::UpdateNow(const std::string &updateUrl)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    std::filesystem::path fileName, signatureName;
    updater->DownloadUpdate(updateUrl, &fileName, &signatureName);

    adminClient.InstallUpdate(fileName);
}
void PiPedalModel::ForceUpdateCheck()
{
    updater->ForceUpdateCheck();
}
void PiPedalModel::SetUpdatePolicy(UpdatePolicyT updatePolicy)
{
    updater->SetUpdatePolicy(updatePolicy);
}

static bool HasAlsaDevice(const std::vector<AlsaDeviceInfo> devices, const std::string &deviceId)
{
    for (auto &device : devices)
    {
        if (device.id_ == deviceId)
            return true;
    }
    return false;
}

void PiPedalModel::StartHotspotMonitoring()
{
    this->avahiService = std::make_unique<AvahiService>();

    SetThreadName("avahi"); // hack to name the avahi service thread.
    UpdateDnsSd();          // now that the server is running, publish a  DNS-SD announcement.
    SetThreadName("main");

    this->hotspotManager->Open();
}

void PiPedalModel::WaitForAudioDeviceToComeOnline()
{
    auto serverSettings = this->GetJackServerSettings();
    // Wait for selected audio device to be initialized.
    // It may take some time for ALSA to publish all available devices when rebooting.

    if (serverSettings.IsValid())
    {
        // wait up to 15 seconds for the midi device to come online.
        auto devices = GetAlsaDevices();
        bool found = false;
        if (HasAlsaDevice(devices, serverSettings.GetAlsaInputDevice()))
        {
            Lv2Log::info(SS("Found ALSA device " << serverSettings.GetAlsaInputDevice() << "."));
        }
        else
        {
            Lv2Log::info(SS("Waiting for ALSA device " << serverSettings.GetAlsaInputDevice() << "."));
            for (int i = 0; i < 5; ++i)
            {
                sleep(2);
                devices = GetAlsaDevices();
                if (HasAlsaDevice(devices, serverSettings.GetAlsaInputDevice()))
                {
                    found = true;
                    break;
                }
            }
            if (found)
            {
                Lv2Log::info(SS("Found ALSA device " << serverSettings.GetAlsaInputDevice() << "."));
            }
            else
            {
                Lv2Log::info(SS("ALSA device " << serverSettings.GetAlsaInputDevice() << " not found."));
            }
        }
    }
    else
    {
        Lv2Log::info("No ALSA device selected.");
    }

    // pre-cache device info before we let audio services run.
    GetAlsaDevices();
}

PiPedalModel::PostHandle PiPedalModel::Post(PostCallback &&fn)
{
    // I know. odd place to forward this to, but it's a very serviceable dispatcher implementation.
    // Why? because it's there, and PiPedalModel has no thread of its own to do dispatching.
    if (!hotspotManager)
    {
        throw std::runtime_error("Too early. It's not ready yet.");
    }
    return hotspotManager->Post(std::move(fn));
}
PiPedalModel::PostHandle PiPedalModel::PostDelayed(const clock::duration &delay, PostCallback &&fn)
{
    if (!hotspotManager)
    {
        throw std::runtime_error("Too early. It's not ready yet.");
    }
    return hotspotManager->PostDelayed(delay, std::move(fn));
}
bool PiPedalModel::CancelPost(PostHandle handle)
{
    if (!hotspotManager)
    {
        throw std::runtime_error("Too early. It's not ready yet.");
    }
    return hotspotManager->CancelPost(handle);
}

void PiPedalModel::CancelNetworkChangingTimer()
{
    if (networkChangingDelayHandle)
    {
        CancelPost(networkChangingDelayHandle);
        networkChangingDelayHandle = 0;
    }
}

std::vector<std::string> PiPedalModel::GetKnownWifiNetworks()
{
    if (!this->hotspotManager)
    {
        return std::vector<std::string>();
    }
    return this->hotspotManager->GetKnownWifiNetworks();
}

void PiPedalModel::OnNetworkChanging(bool ethernetConnected, bool hotspotConnected)
{
    CancelNetworkChangingTimer();
    this->networkChangingDelayHandle =
        PostDelayed(std::chrono::seconds(10), // takes a while for network configuration to be fully applied.
                    [this, ethernetConnected, hotspotConnected]()
                    {
                        this->networkChangingDelayHandle = 0;
                        OnNetworkChanged(ethernetConnected, hotspotConnected);
                    });

    // take a snapshot incase a client unsusbscribes in the notification handler (in which case the mutex won't protect us)
    std::vector<IPiPedalModelSubscriber::ptr> t{subscribers.begin(), subscribers.end()};
    for (auto &subscriber : t)
    {
        subscriber->OnNetworkChanging(hotspotConnected);
    }
}
void PiPedalModel::OnNetworkChanged(bool ethernetConnected, bool hotspotConnected)
{
    FireNetworkChanged();
}

void PiPedalModel::OnNotifyMidiRealtimeEvent(RealtimeMidiEventType eventType)
{
    try
    {
        switch (eventType)
        {
        case RealtimeMidiEventType::Shutdown:
        {
            this->RequestShutdown(false);
        }
        break;
        case RealtimeMidiEventType::Reboot:
        {
            this->RequestShutdown(true);
        }
        break;
        case RealtimeMidiEventType::StartHotspot:
        {
            WifiConfigSettings settings = storage.GetWifiConfigSettings();
            if (!settings.hasSavedPassword_)
            {
                throw std::runtime_error("Can't start Wi-Fi hotspot because no password has been configured.");
            }
            settings.autoStartMode_ = (uint16_t)HotspotAutoStartMode::Always;
            this->SetWifiConfigSettings(settings);
        }
        break;
        case RealtimeMidiEventType::StopHotspot:
        {
            WifiConfigSettings settings = storage.GetWifiConfigSettings();
            settings.autoStartMode_ = (uint16_t)HotspotAutoStartMode::Never;
            this->SetWifiConfigSettings(settings);
        }
        break;

        default:
            break;
        }
    }
    catch (const std::exception &e)
    {
        Lv2Log::error(SS("Failed to process realtime MIDI event. " << e.what()));
    }
}

void PiPedalModel::RequestShutdown(bool restart)
{
    if (GetAdminClient().CanUseAdminClient())
    {
        GetAdminClient().RequestShutdown(restart);
    }
    else
    {
        // ONLY works when interactively logged in.
        std::stringstream s;
        s << "/usr/sbin/shutdown ";
        if (restart)
        {
            s << "-r";
        }
        else
        {
            s << "-P";
        }
        s << " now";

        if (sysExec(s.str().c_str()) != EXIT_SUCCESS)
        {
            Lv2Log::error("shutdown failed.");
            if (restart)
            {
                throw new PiPedalStateException("Restart request failed.");
            }
            else
            {
                throw new PiPedalStateException("Shutdown request failed.");
            }
        }
    }
}

void PiPedalModel::SetHasWifi(bool hasWifi)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (this->hasWifi != hasWifi)
    {
        this->hasWifi = hasWifi;

        std::vector<IPiPedalModelSubscriber::ptr> t{subscribers.begin(), subscribers.end()};

        for (auto &subscriber : t)
        {
            subscriber->OnHasWifiChanged(hasWifi);
        }
    }
}
bool PiPedalModel::GetHasWifi()
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    return hasWifi;
}

std::map<std::string, std::string> PiPedalModel::GetWifiRegulatoryDomains()
{
    std::map<std::string, std::string> result;
    try
    {
        auto &regDb = RegDb::GetInstance();
        result = regDb.getRegulatoryDomains(storage.GetConfigRoot() / "iso_codes.json");
    }
    catch (const std::exception &e)
    {
        Lv2Log::warning(SS("Unable to query Wifi Regulatory domains. " << e.what()));
    }
    return result;
}

void PiPedalModel::CancelAudioRetry()
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (audioRetryPostHandle)
    {
        // don't think this can ever happen, but if it did, it would be bad.
        this->CancelPost(audioRetryPostHandle);
        audioRetryPostHandle = 0;
    }
}

void PiPedalModel::OnAlsaDriverTerminatedAbnormally()
{
    // notification from the realtime thread, via the audiohost that the
    // ALSA stream has broken. We want to restart.

    // get off the service thread as promptly as possible
    this->Post([&]()
               {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        if (closed) return;

        auto now = clock::now();
        clock::duration timeSinceLastRetry = now-this->lastRestartTime;
        this->lastRestartTime = now;
        if (timeSinceLastRetry > std::chrono::duration_cast<clock::duration>(std::chrono::milliseconds(1000))) {
            audioRestartRetries = 0;
        }
        CancelAudioRetry();

        if (audioRestartRetries == 0)
        {
            this->audioRetryPostHandle = this->Post(
                // No lock to avoid deadlocks!
                [this]() {
                    Lv2Log::info("Restarting audio.");
                    this->RestartAudio();
                });
            ++audioRestartRetries;
        } else if (audioRestartRetries < 3) 
        {
            this->audioRetryPostHandle = this->PostDelayed(
                std::chrono::milliseconds(100 * audioRestartRetries),
                [this]() {
                    if (closed) {
                        return;
                    }
                    Lv2Log::info(SS("Restarting audio. (retry " << audioRestartRetries << ")"));

                    RestartAudio();
                });
            ++audioRestartRetries;
        } else if (audioRestartRetries == 3)  // one attempt to start the dummy driver.
        {
            {
                this->audioRetryPostHandle = this->Post(
                    // No lock to avoid deadlocks!
                    [this]() {
                        Lv2Log::info(SS("Switching to dummy driver."));
                        RestartAudio(true); // switch to the dummy driver.
                    });
            } 
            ++audioRestartRetries;
        } else {
            Lv2Log::error(SS("Unable to reastart audio."));

        } });
}

bool PiPedalModel::IsInUploadsDirectory(const std::string &path)
{
    return storage.IsInUploadsDirectory(path);
}

void PiPedalModel::MoveAudioFile(
    const std::string &directory,
    int32_t fromPosition,
    int32_t toPosition)
{
    if (directory.empty())
    {
        throw std::runtime_error("Directory is empty.");
    }
    AudioDirectoryInfo::Ptr dir = AudioDirectoryInfo::Create(directory);
    dir->MoveAudioFile(directory, fromPosition, toPosition);
}
void PiPedalModel::SetPedalboardItemTitle(int64_t instanceId, const std::string &title, const std::string &colorKey)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (!this->pedalboard.SetItemTitle(instanceId, title, colorKey))
    {
        return;
    }
    // no need to reload the pedalboard, but we do need to notify subscribers.
    this->SetPresetChanged(-1, true);
    this->FirePedalboardChanged(-1, false);
}

std::vector<PresetIndexEntry> PiPedalModel::RequestBankPresets(int64_t bankInstanceId)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);

    return storage.RequestBankPresets(bankInstanceId);
}

int64_t PiPedalModel::ImportPresetsFromBank(int64_t bankInstanceId, const std::vector<int64_t> &presets)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    uint64_t lastAdded = storage.ImportPresetsFromBank(bankInstanceId, presets);

    FirePresetsChanged(-1);
    return lastAdded;
}
int64_t PiPedalModel::CopyPresetsToBank(int64_t bankInstanceId, const std::vector<int64_t> &presets)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    uint64_t lastAdded = storage.CopyPresetsToBank(bankInstanceId, presets);
    return lastAdded;
}

ChannelRouterSettings::ptr PiPedalModel::GetChannelRouterSettings()
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    return this->channelRouterSettings;
}

void PiPedalModel::SetChannelRouterSettings(int64_t clientId, ChannelRouterSettings::ptr &settings)
{
    {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        this->channelRouterSettings = settings;
        this->storage.SetChannelRouterSettings(settings);
        this->pluginHost.OnConfigurationChanged(jackConfiguration, *settings);
        CancelAudioRetry();
    }
    RestartAudio(); // no lock to avoid mutex deadlock when reader thread is sending notifications..

    this->FireChannelRouterSettingsChanged(clientId);
}

std::string PiPedalModel::Tone3000ThumbnailDirectory()
{
    return "/var/pipedal/audio_uploads/tone3000_thumbnails";
}
std::string PiPedalModel::OldTone3000ThumbnailDirectory()
{
    return "/var/pipedal/tone3000_thumbnails";
}

void PiPedalModel::EnableUpdater(bool enable)
{
    this->updaterEnabled = enable;
}

static bool IsSafeMediaPath(const fs::path path)
{
    if (!HtmlHelper::IsSafeFileName(path))
    {
        return false;
    }
    if (!(path.string().starts_with("/var/pipedal/audio_uploads")))
    {
        return false;
    }
    return true;
}

void PiPedalModel::WriteTone3000Readme(const std::filesystem::path &filePath, const tone3000::Tone &tone, const std::string &thumbnailUrl)
{
    if (!IsSafeMediaPath(filePath))
    {
        throw std::runtime_error("Invalid media path.");
    }
    tone3000::WriteTone3000Readme(filePath, tone, thumbnailUrl);
}
