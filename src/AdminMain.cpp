// Copyright (c) Robin E.R. Davies
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

// shim file to allow shutdown to be called from a service.
// (Gets suid root permissions during install, since otherwise
// non-interactive services are not allowed to execute)

#include "pch.h"
#include <sys/stat.h>
#include <grp.h>
#include "ss.hpp"
#include "CommandLineParser.hpp"
#include "CpuGovernor.hpp"
#include <iostream>
#include <cstdint>
#include <iostream>
#include <memory>
#include <thread>
#include <stdlib.h>
#include "JackServerSettings.hpp"
#include <cstdlib>
#include "Lv2Log.hpp"
#include "Lv2SystemdLogger.hpp"
#include "WifiConfigSettings.hpp"
#include "SetWifiConfig.hpp"
#include "AirplaySettings.hpp"
#include "SysExec.hpp"
#include <fstream>
#include <filesystem>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include "UnixSocket.hpp"
#include <signal.h>

using namespace std;
using namespace pipedal;

const char ADMIN_SOCKET_NAME[] = "/run/pipedal/pipedal_admin";

class GovernorMonitorThread
{
public:
    ~GovernorMonitorThread()
    {
        Stop();
    }
    void Start(std::string governor)
    {
        std::unique_lock<std::mutex> lock(mutex);
        if (!HasCpuGovernor())
        {
            return;
        }
        if (!pThread)
        {
            this->governor = governor;
            cancelled = false;
            wake = false;
            pThread = std::make_unique<std::thread>([this]() { this->ServiceProc(); });
        }
        else
        {
            this->governor = governor;
            wake = true;

            lock.unlock();
            cv.notify_one();
        }
    }
    void Stop()
    {
        if (!HasCpuGovernor())
        {
            return;
        }
        std::unique_lock<std::mutex> lock(mutex);
        if (pThread)
        {
            wake = true;
            cancelled = true;
            lock.unlock();

            cv.notify_one();
            pThread->join();
            pThread.reset();
        }
    }
    void SetGovernor(const std::string &governor)
    {
        {
            std::unique_lock<std::mutex> lock(mutex);
            wake = true;
            this->governor = governor;
        }
        cv.notify_one();
    }

private:
    bool wake = false;
    bool cancelled = false;
    std::string savedGovernor;
    void ServiceProc()
    {
        if (!HasCpuGovernor())
        {
            return;
        }
        savedGovernor = pipedal::GetCpuGovernor();
        pipedal::SetCpuGovernor(this->governor);
        while (true)
        {
            bool cancelled;
            std::string governor;
            {
                std::unique_lock<std::mutex> lock(mutex);
                auto timeToWaitFor = std::chrono::system_clock::now() + 5000ms;
                cv.wait_until(
                    lock,
                    timeToWaitFor,
                    [this]() { return wake; });
                wake = false;
                governor = this->governor;
                cancelled = this->cancelled;
            }
            if (cancelled)
            {
                break;
            }
            std::string activeGovernor = pipedal::GetCpuGovernor();
            if (activeGovernor != governor)
            {
                // somebody set it so they must have wanted it.
                // save the value so that we can restore it when done.
                savedGovernor = activeGovernor;

                // but insist on using ours!!
                pipedal::SetCpuGovernor(governor);
            }
        }
        pipedal::SetCpuGovernor(savedGovernor);
    }
    std::unique_ptr<std::thread> pThread;
    std::string governor;
    std::mutex mutex;
    bool canceled;
    std::condition_variable cv;
};

GovernorMonitorThread governorMonitorThread;

void StopGovernorMonitorThread()
{
    governorMonitorThread.Stop();
}
void StartGovernorMonitorThread(const std::string &governor)
{
    governorMonitorThread.Start(governor);
}

static bool startsWith(const std::string &s, const char *text)
{
    if (s.length() < strlen(text))
        return false;
    const char *sp = s.c_str();
    while (*text)
    {
        if (*text++ != *sp++)
            return false;
    }
    return true;
}
static std::vector<std::string> tokenize(std::string value)
{
    std::vector<std::string> result;
    std::stringstream s(value);
    std::string item;
    while (std::getline(s, item, ' '))
    {
        if (item.length() != 0)
            result.push_back(item);
    }
    return result;
}

static void delayedRestartProc()
{
    sleep(1); // give a chance for websocket messages to propagate.
    Lv2Log::error("Delayed Restart");
    std::string pipedalConfigPath = std::filesystem::path(getSelfExePath()).parent_path() / "pipedalconfig";

    std::stringstream s;
    s << pipedalConfigPath.c_str() << " --restart --excludeShutdownService";
    if (::system(s.str().c_str()) != EXIT_SUCCESS)
    {
        Lv2Log::error("Delayed Restart failed.");
    }
}

bool setJackConfiguration(JackServerSettings serverSettings)
{
    bool success = true;

#if JACK_HOST

    serverSettings.WriteDaemonConfig();

    silentSysExec("/usr/bin/systemctl unmask jack");
    silentSysExec("/usr/bin/systemctl enable jack");
    std::thread delayedRestartThread(delayedRestartProc);
    delayedRestartThread.detach();
#else
    // otherwise, 
    // the config was written before invoking admin main and we don't need to to restart the service.
#endif

    return true;
}

void InstallUpdate(const std::filesystem::path path)
{
    std::string cmd = SS("/usr/bin/systemd-run --unit=pidal_update /usr/sbin/pipedal_update " <<path.string());
    int rc = system(cmd.c_str());
}

static const char SHAIRPORT_BIN[] = "/usr/bin/shairport-sync";
static const char AIRPLAY_ALSA_CONF_PATH[] = "/etc/alsa/conf.d/99-pipedal-airplay.conf";
static const char AIRPLAY_SHAIRPORT_CONF_PATH[] = "/etc/pipedal/shairport-sync.conf";
static const char AIRPLAY_SERVICE_PATH[] = "/etc/systemd/system/pipedal-airplay.service";

static std::string sanitizeAirplayName(const std::string &name)
{
    std::string result;
    for (char c : name)
    {
        if (c == '"' || c == '\\' || (unsigned char)c < 0x20)
        {
            continue;
        }
        result += c;
    }
    if (result.empty())
    {
        result = "PiPedal";
    }
    return result;
}

static void writeFileOrThrow(const std::filesystem::path &path, const std::string &content)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream f(path);
    if (!f.is_open())
    {
        throw std::runtime_error(SS("Can't write " << path));
    }
    f << content;
}

bool setAirplayConfiguration(const AirplayServiceConfiguration &configuration)
{
    // remove the ALSA device configuration written by earlier versions; the
    // pipe backend writes straight to the FIFO and needs no ALSA plumbing.
    std::error_code ignoredError;
    std::filesystem::remove(AIRPLAY_ALSA_CONF_PATH, ignoredError);

    if (!configuration.enabled_)
    {
        silentSysExec("/usr/bin/systemctl disable --now pipedal-airplay.service");
        return true;
    }
    if (!std::filesystem::exists(SHAIRPORT_BIN))
    {
        throw std::runtime_error(
            "shairport-sync is not installed. Run 'sudo apt install shairport-sync', then try again.");
    }

    // The pipe backend writes raw 44100Hz S16_LE stereo to the FIFO; pipedald
    // resamples to the device rate if necessary. (The alsa backend can't be
    // pointed at a FIFO-backed virtual device: its sync engine needs real
    // device timing.) NOTE: compatible with shairport-sync 3.3.x (Debian
    // bookworm); 3.3.x exits on unrecognized config options, so the backend is
    // selected with -o pipe instead of a config setting.
    writeFileOrThrow(AIRPLAY_SHAIRPORT_CONF_PATH, SS(
        "// Written by pipedaladmind. Do not edit; changes will be overwritten.\n"
        "general = {\n"
        "  name = \"" << sanitizeAirplayName(configuration.name_) << "\";\n"
        "};\n"
        "pipe = {\n"
        "  name = \"" << configuration.fifoPath_ << "\";\n"
        "};\n"
        // allow_session_interruption lets a returning sender take over a session
        // that was never torn down. Without it, a Mac whose previous session
        // ended abruptly (PiPedal switched off, network dropped) is refused for
        // session_timeout seconds and reports the receiver as unavailable.
        "sessioncontrol = {\n"
        "  allow_session_interruption = \"yes\";\n"
        "  session_timeout = 10;\n"
        "};\n"));

    writeFileOrThrow(AIRPLAY_SERVICE_PATH, SS(
        "# Written by pipedaladmind. Do not edit; changes will be overwritten.\n"
        "[Unit]\n"
        "Description=PiPedal AirPlay receiver (shairport-sync)\n"
        "Wants=avahi-daemon.service\n"
        "After=network-online.target avahi-daemon.service pipedald.service\n"
        "\n"
        "[Service]\n"
        "Type=simple\n"
        "User=pipedal_d\n"
        "Group=pipedal_d\n"
        // if the FIFO doesn't exist yet, the ALSA file plugin would create a regular
        // file and grow it without bound; make sure the FIFO exists first.
        "ExecStartPre=/bin/sh -c 'test -p " << configuration.fifoPath_ << " || mkfifo -m 660 " << configuration.fifoPath_ << "'\n"
        "ExecStart=" << SHAIRPORT_BIN << " -o pipe -c " << AIRPLAY_SHAIRPORT_CONF_PATH << "\n"
        "Restart=on-failure\n"
        "RestartSec=5\n"
        "LimitRTPRIO=10\n"
        // SIGTERM lets shairport-sync withdraw its Avahi record before it exits,
        // so senders stop seeing a receiver that is no longer there.
        "KillSignal=SIGTERM\n"
        "TimeoutStopSec=10\n"
        "\n"
        "[Install]\n"
        "WantedBy=multi-user.target\n"));

    silentSysExec("/usr/bin/systemctl daemon-reload");
    // the stock shairport-sync service would advertise a second (broken) AirPlay
    // endpoint pointing at the ALSA device that PiPedal owns.
    silentSysExec("/usr/bin/systemctl disable --now shairport-sync.service");
    // Deliberately not enabled: pipedald turns AirPlay off at startup, so a unit
    // that systemd started at boot would only advertise a receiver for the few
    // seconds before pipedald shut it down again.
    silentSysExec("/usr/bin/systemctl disable pipedal-airplay.service");
    int rc = sysExec("/usr/bin/systemctl restart pipedal-airplay.service");
    if (rc != EXIT_SUCCESS)
    {
        throw std::runtime_error("Can't start the pipedal-airplay service.");
    }
    return true;
}

class AdminServer
{
private:
    std::stringstream message;

    size_t writePosition = 0;
    std::string response;

    char buffer[4096];

private:
    bool HandleCommand()
    {
        std::string sender;
        size_t length = socket.ReceiveFrom(buffer, sizeof(buffer), &sender);
        if (length > 0 && buffer[length - 1] == '\n')
            --length;
        buffer[length] = 0;

        std::string text{buffer};
        int result = -1;
        std::string command;
        std::string args;
        auto pos = text.find_first_of(' ');
        if (pos == std::string::npos)
        {
            command = text;
        }
        else
        {
            command = text.substr(0, pos);
            args = text.substr(pos + 1);
        }
        if (command == "__quit")
        {
            return false;
        }
        try
        {
            if (command == "InstallUpdate")
            {
                std::filesystem::path path = args;
                InstallUpdate(path);
            } else if (command == "UnmonitorGovernor")
            {
                StopGovernorMonitorThread();
                result = 0;
            }
            else if (command == "MonitorGovernor")
            {
                std::stringstream ss(args);
                std::string governor;
                try
                {
                    json_reader reader(ss);
                    reader.read(&governor);
                }
                catch (const std::exception &e)
                {
                    throw PiPedalArgumentException("Invalid arguments.");
                }
                StartGovernorMonitorThread(governor);
                result = 0;
            }
            else if (command == "GovernorSettings")
            {
                std::stringstream ss(args);
                std::string governor;
                try
                {
                    json_reader reader(ss);
                    reader.read(&governor);
                }
                catch (const std::exception &e)
                {
                    throw PiPedalArgumentException("Invalid arguments.");
                }
                if (HasCpuGovernor())
                {
                    governorMonitorThread.SetGovernor(governor);
                }
                result = 0;
            }
            else if (command == "WifiConfigSettings")
            {
                std::stringstream ss(args);
                WifiConfigSettings settings;
                try
                {
                    json_reader reader(ss);
                    reader.read(&settings);
                }
                catch (const std::exception &e)
                {
                    throw PiPedalArgumentException("Invalid arguments.");
                }
                pipedal::SetWifiConfig(settings);
                result = 0;
            }
            else if (command == "WifiDirectConfigSettings")
            {
                std::stringstream ss(args);
                WifiDirectConfigSettings settings;
                try
                {
                    json_reader reader(ss);
                    reader.read(&settings);
                }
                catch (const std::exception &e)
                {
                    throw PiPedalArgumentException("Invalid arguments.");
                }
                pipedal::SetWifiDirectConfig(settings);
                result = 0;
            }
            else if (command == "shutdown")
            {
                result = sysExec("/usr/sbin/shutdown -P now");
            }
            else if (command == "restart")
            {
                result = sysExec("/usr/sbin/shutdown -r now");
            }
            else if (command == "setJackConfiguration")
            {

                std::stringstream input(args);
                JackServerSettings serverSettings;
                json_reader reader(input);

                reader.read(&serverSettings);

                if (input.fail())
                {
                    result = -1;
                }
                else
                {
                    result = setJackConfiguration(serverSettings) ? 0 : -1;
                }
            }
            else if (command == "setAirplayConfiguration")
            {
                std::stringstream input(args);
                AirplayServiceConfiguration airplayConfiguration;
                try
                {
                    json_reader reader(input);
                    reader.read(&airplayConfiguration);
                }
                catch (const std::exception &e)
                {
                    throw PiPedalArgumentException("Invalid arguments.");
                }
                result = setAirplayConfiguration(airplayConfiguration) ? 0 : -1;
            }
            else
            {
                throw std::invalid_argument(SS("Unknown command:" << command));
            }
        }
        catch (const std::exception &e)
        {

            std::string reply = SS("-2 " << e.what() << "\n");

            try
            {
                socket.SendTo(reply.c_str(), reply.length(), sender);
            }
            catch (const std::exception /*ignored*/)
            {
            }
            return true;
        }

        std::string reply;
        reply = SS(result << "\n");
        try
        {
            socket.SendTo(reply.c_str(), reply.length(), sender);
        }
        catch (const std::exception /*ignored*/)
        {
        }

        return true;
    }

private:
    UnixSocket socket;
    UnixSocket cancelSocket;

    std::unique_ptr<std::thread> serviceThread;
    void ServiceProc()
    {
        try
        {
            while (true)
            {
                if (!HandleCommand())
                {
                    break;
                }
            }
        }
        catch (const std::exception &)
        {
        }
    }

public:
    void Start()
    {
        socket.Bind(ADMIN_SOCKET_NAME, "pipedal_d");
        cancelSocket.Connect(ADMIN_SOCKET_NAME, "pipedal_d");

        serviceThread = std::make_unique<std::thread>([this] {
            this->ServiceProc();
        });
    }

    void Stop()
    {

        if (serviceThread)
        {
            std::string msg{"__quit\n"};
            try
            {
                cancelSocket.Send(msg.c_str(), msg.length());
            }
            catch (const std::exception &ignored)
            {
            };

            serviceThread->join();
            serviceThread = nullptr;
        }
    }
};

static bool signaled = false;

static void sigHandler(int signal)
{
    signaled = true;
}

struct sigaction saInt, oldActionInt;
struct sigaction saTerm, oldActionTerm;

static void setSignalHandlers()
{
    memset(&saInt, 0, sizeof(saInt));
    saInt.sa_handler = sigHandler;
    sigaction(SIGINT, &saInt, &oldActionInt);

    memset(&saTerm, 0, sizeof(saTerm));
    saTerm.sa_handler = sigHandler;
    sigaction(SIGTERM, &saTerm, &oldActionTerm);
}

int main(int argc, char **argv)
{
    Lv2Log::set_logger(MakeLv2SystemdLogger());
    CommandLineParser parser;

    uint16_t port = 0;

    parser.AddOption("-port", &port); // ignored legacy option.

    try
    {
        parser.Parse(argc, (const char **)argv);
        if (parser.Arguments().size() != 0)
        {
            throw PiPedalException("Invalid argument.");
        }
    }
    catch (const std::exception &e)
    {
        cout << "Error: " << e.what() << endl;
        return EXIT_FAILURE;
    }

    mkdir("/run/pipedal", S_IRWXU | S_IRWXG);

    struct group *group_;
    group_ = getgrnam("pipedal_d");
    if (group_ == nullptr)
    {
        throw UnixSocketException("Group not found.");
    }

    std::ignore = chown("/run/pipedal", -1, group_->gr_gid);

    setSignalHandlers();

    AdminServer server;
    server.Start();

    while (!signaled)
    {
        usleep(300000);
    }
    server.Stop();

    governorMonitorThread.Stop();
    return EXIT_SUCCESS;
}
