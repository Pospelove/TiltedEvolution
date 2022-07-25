#pragma once

struct UpdateEvent;
struct World;

struct STRPMessagingService
{
    STRPMessagingService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~STRPMessagingService() noexcept = default;

    TP_NOCOPYMOVE(STRPMessagingService);

    void OnUpdate(const UpdateEvent&) noexcept;

    static void ProcessConnect(World& aWorld, const char* aIpAddress, const char* aStrpToken) noexcept;
    static void UpdateIdsMap(World& aWorld) noexcept;

    World& m_world;
};
