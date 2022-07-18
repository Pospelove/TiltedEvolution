
#include <Services/STRPMessagingService.h>
#include <TiltedOnlinePCH.h>

#include <Events/UpdateEvent.h>

#include <functional>
#include <mutex>
#include <vector>

#include <thread>

#include <tlhelp32.h>

enum class STRPMessageType : uint32_t
{
    Connect,
    Maximum
};

namespace
{
std::vector<std::function<void(World&)>> g_tasks;
std::mutex g_tasksMutex;

std::vector<DWORD> FindProcesses(std::wstring pName)
{
    std::vector<DWORD> pids;

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

    PROCESSENTRY32W entry;
    entry.dwSize = sizeof entry;

    if (!Process32FirstW(snap, &entry))
    {
        return {};
    }

    do
    {
        if (std::wstring(entry.szExeFile) == pName)
        {
            pids.emplace_back(entry.th32ProcessID);
        }
    } while (Process32NextW(snap, &entry));

    return pids;
}

// STRPMessagingService.cpp on ST side, DevAPI.cpp on SkyMP side
int FindMessagingPort()
{
    int port = 10000;
    port += FindProcesses(L"SkyrimSE.exe").size();
    port += FindProcesses(L"SkyrimTogether.exe").size();
    return port;
}

} // namespace

STRPMessagingService::STRPMessagingService(World& aWorld, entt::dispatcher& aDispatcher) noexcept : m_world(aWorld)
{
    aDispatcher.sink<UpdateEvent>().connect<&STRPMessagingService::OnUpdate>(this);
    spdlog::info("STRPMessagingService initialized");

    std::thread([] {
        // launch api server on localhost
        httplib::Server server;

        server.Get("/connect", [](const httplib::Request& req, httplib::Response& res) {
            std::lock_guard<std::mutex> lock(g_tasksMutex);
            if (req.headers.find("Together-Server-IP") != req.headers.end())
            {
                auto ip = req.headers.find("Together-Server-IP")->second;
                g_tasks.push_back([ip](auto& world) { STRPMessagingService::ProcessConnect(world, ip.c_str()); });
            }
        });

        int port = FindMessagingPort();

        spdlog::info("STRPMessagingService Listening {}", port);

        server.listen("localhost", port);
    }).detach();
}

void STRPMessagingService::OnUpdate(const UpdateEvent& aEvent) noexcept
{
    decltype(g_tasks) tasks;
    {
        std::lock_guard<std::mutex> lock(g_tasksMutex);
        tasks = std::move(g_tasks);
        g_tasks.clear();
    }

    for (auto& task : tasks)
    {
        task(m_world);
    }
}

void STRPMessagingService::ProcessConnect(World& aWorld, const char* ip) noexcept
{
    spdlog::info("STRPMessagingService::ProcessConnect {}", ip);

    const int port = 10578;

    std::string endpoint = ip + std::string(":") + std::to_string(port);

    World::Get().GetRunner().Queue([endpoint] { World::Get().GetTransport().Connect(endpoint); });
}
