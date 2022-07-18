
#include <TiltedOnlinePCH.h>

#include <Services/STRPMessagingService.h>

#include <Events/UpdateEvent.h>

#include <vector>
#include <functional>
#include <mutex>

// Keep in sync with AAAStrpApi.cpp
enum class STRPMessageType : uint32_t
{
  Connect
};

namespace {
  std::vector<std::function<void(World&)>> g_tasks;
  std::mutex g_tasksMutex;
}

STRPMessagingService::STRPMessagingService(World& aWorld, entt::dispatcher& aDispatcher) noexcept : m_world(aWorld)
{
  aDispatcher.sink<UpdateEvent>().connect<&STRPMessagingService::OnUpdate>(this);
  spdlog::info("STRPMessagingService initialized");
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

void STRPMessagingService::ProcessConnect(World& aWorld, const char *ip) noexcept 
{
    spdlog::info("STRPMessagingService::ProcessConnect {}", ip);

    const int port = 10578;

    std::string endpoint = ip + std::string(":") + std::to_string(port);

    World::Get().GetRunner().Queue([endpoint] { 
        World::Get().GetTransport().Connect(endpoint);
    });
}

extern "C" {
    __declspec(dllexport) void STRPMessagingService_Send(STRPMessageType msgType, const char *message) noexcept
    {
      try {
          std::lock_guard<std::mutex> lock(g_tasksMutex);
          spdlog::info("STRPMessagingService_Send: received task {} {}", (int)msgType, message);
          g_tasks.push_back([msgType, message](World& aWorld) {
              switch (msgType)
              {
              case STRPMessageType::Connect:
                  STRPMessagingService::ProcessConnect(aWorld, message);
                  break;
              }
          });
      }
      catch(std::exception& e) {
          spdlog::error("STRPMessagingService_Send: {}", e.what());
      }
    }
}