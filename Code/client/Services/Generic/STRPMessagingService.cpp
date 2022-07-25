
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

std::string g_remoteIdByLocalId = "{}";
std::mutex g_remoteIdByLocalIdMutex;

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
                if (req.headers.find("Strp-Token") != req.headers.end())
                {
                    auto ip = req.headers.find("Together-Server-IP")->second;
                    auto token = req.headers.find("Strp-Token")->second;
                    g_tasks.push_back([ip, token](auto& world) {
                        STRPMessagingService::ProcessConnect(world, ip.c_str(), token.c_str());
                    });
                }
            }
        });

        server.Get("/spdlogInfo", [](const httplib::Request& req, httplib::Response& res) {
            if (req.headers.find("Message") != req.headers.end())
            {
                auto message = req.headers.find("Message")->second;
                spdlog::info("{}", message);
            }
        });

        // does not return right answer after first request.
        // needs some requests to warp up.
        server.Get("/getIdsMap", [](const httplib::Request& req, httplib::Response& res) {
            {
                std::lock_guard<std::mutex> lock(g_tasksMutex);
                g_tasks.push_back([](auto& world) {
                    // Request update ids map
                    STRPMessagingService::UpdateIdsMap(world);
                });
            }
            std::lock_guard<std::mutex> lock(g_remoteIdByLocalIdMutex);
            res.body = g_remoteIdByLocalId;
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

void STRPMessagingService::ProcessConnect(World& aWorld, const char* aIpAddress, const char* aStrpToken) noexcept
{
    spdlog::info("STRPMessagingService::ProcessConnect IP={} STRPTOKEN={}", aIpAddress, aStrpToken);

    const int port = 10578;

    std::string endpoint = aIpAddress + std::string(":") + std::to_string(port);

    World::Get().GetTransport().SetSTRPToken(aStrpToken);

    World::Get().GetRunner().Queue([endpoint] { World::Get().GetTransport().Connect(endpoint); });
}

void STRPMessagingService::UpdateIdsMap(World& aWorld) noexcept
{
    std::string jsonBody = "{";

    for (auto [entity, player, formId] : aWorld.view<PlayerComponent, FormIdComponent>().each())
    {
        jsonBody += "\"" + std::to_string(formId.Id) + "\": " + std::to_string(player.Id) + ",";
    };

    // finish json
    if (jsonBody.size() > 0 && jsonBody.back() == ',')
    {
        jsonBody.back() = '}';
    }

    // fixup json if empty
    if (jsonBody == "{")
    {
        jsonBody += "}";
    }

    std::lock_guard l(g_remoteIdByLocalIdMutex);
    g_remoteIdByLocalId = std::move(jsonBody);
}
