#include "ECS.h"
#include "catch_amalgamated.hpp"

#ifdef _DEBUG

using namespace ECS;

// Shared data type used by integration tests
struct SharedData {
    int ScreenW, ScreenH;
};

// Simple test components
struct Pos {
    float x, y;
};

struct Vel {
    float vx, vy;
};


TEST_CASE("Integration: removing one component keeps the others", "[integration]") {
    World world;
    Entity e = world.Create();

    world.Add<Pos, Vel>(e, Pos{ 10.0f, 20.0f }, Vel{ 1.0f, 2.0f });

    REQUIRE(world.Has<Pos>(e));
    REQUIRE(world.Has<Vel>(e));

    world.Remove<Vel>(e);

    REQUIRE(world.Alive(e));
    REQUIRE(world.Has<Pos>(e));
    REQUIRE_FALSE(world.Has<Vel>(e));

    auto& pos = world.Get<Pos>(e);
    REQUIRE(pos.x == Catch::Approx(10.0f));
    REQUIRE(pos.y == Catch::Approx(20.0f));
}


TEST_CASE("Integration: query processes only matching entities", "[integration]") {
    World world;

    Entity e1 = world.Create();
    Entity e2 = world.Create();
    Entity e3 = world.Create();

    world.Add<Pos, Vel>(e1, Pos{ 0, 0 }, Vel{ 1, 1 });
    world.Add<Pos>(e2, Pos{ 5, 5 });
    world.Add<Pos, Vel>(e3, Pos{ 10, 10 }, Vel{ 2, 2 });

    int processed = 0;

    world.Query<Pos, Vel>([&](Entity, Pos& pos, Vel& vel) {
        processed++;
        pos.x += vel.vx;
        pos.y += vel.vy;
        });

    REQUIRE(processed == 2);

    REQUIRE(world.Get<Pos>(e1).x == Catch::Approx(1.0f));
    REQUIRE(world.Get<Pos>(e1).y == Catch::Approx(1.0f));

    REQUIRE(world.Get<Pos>(e2).x == Catch::Approx(5.0f));
    REQUIRE(world.Get<Pos>(e2).y == Catch::Approx(5.0f));

    REQUIRE(world.Get<Pos>(e3).x == Catch::Approx(12.0f));
    REQUIRE(world.Get<Pos>(e3).y == Catch::Approx(12.0f));
}

TEST_CASE("Integration: update system changes entity data", "[integration]") {
    World world;

    Entity e = world.Create();
    world.Add<Pos, Vel>(e, Pos{ 0, 0 }, Vel{ 3, 4 });

    world.RegisterSystem<Pos, Vel>(
        "move",
        [](ArchetypeContext ctx, float dt, SharedDataRef) {
            auto pos = ctx.Slice<Pos>();
            auto vel = ctx.Slice<Vel>();

            for (int i = 0; i < pos.size(); i++) {
                pos[i].x += vel[i].vx * dt;
                pos[i].y += vel[i].vy * dt;
            }
        },
        SystemGroup::Update
    ).Read<Pos>().Write<Pos>().Read<Vel>();

    world.Run(SystemGroup::Update, 2.0f);

    auto& pos = world.Get<Pos>(e);
    REQUIRE(pos.x == Catch::Approx(6.0f));
    REQUIRE(pos.y == Catch::Approx(8.0f));
}

TEST_CASE("Integration: disabled system does not affect world state", "[integration]") {
    World world;

    Entity e = world.Create();
    world.Add<Pos>(e, Pos{ 7.0f, 9.0f });

    world.RegisterSystem<Pos>(
        "reset_pos",
        [](ArchetypeContext ctx, float, SharedDataRef) {
            auto pos = ctx.Slice<Pos>();
            for (int i = 0; i < pos.size(); i++) {
                pos[i].x = 0.0f;
                pos[i].y = 0.0f;
            }
        },
        SystemGroup::Update
    ).Read<Pos>().Write<Pos>();

    world.DisableSystem("reset_pos");
    world.Run(SystemGroup::Update, 1.0f);

    auto& pos = world.Get<Pos>(e);
    REQUIRE(pos.x == Catch::Approx(7.0f));
    REQUIRE(pos.y == Catch::Approx(9.0f));
}

TEST_CASE("Integration: system receives shared data from world", "[integration]") {
    World world;
    world.Tie(std::make_shared<SharedData>(SharedData{ 10, 10 }));

    Entity e = world.Create();
    world.Add<Pos, Vel>(e, Pos{ 9.5f, 5.0f }, Vel{ 1.0f, 0.0f });

    world.RegisterSystem<Pos, Vel>(
        "bounce",
        [](ArchetypeContext ctx, float dt, SharedDataRef data) {
            auto pos = ctx.Slice<Pos>();
            auto vel = ctx.Slice<Vel>();

            for (int i = 0; i < pos.size(); i++) {
                pos[i].x += vel[i].vx * dt;

                if (pos[i].x >= data->ScreenW) {
                    pos[i].x -= vel[i].vx * dt;
                    vel[i].vx *= -1;
                }
            }
        },
        SystemGroup::Update
    ).Read<Pos>().Write<Pos>().Read<Vel>().Write<Vel>();

    world.Run(SystemGroup::Update, 1.0f);

    REQUIRE(world.Get<Pos>(e).x == Catch::Approx(9.5f));
    REQUIRE(world.Get<Vel>(e).vx == Catch::Approx(-1.0f));
}


TEST_CASE("Integration: destroying one entity keeps other entities valid", "[integration]") {
    World world;

    Entity e1 = world.Create();
    Entity e2 = world.Create();
    Entity e3 = world.Create();

    world.Add<Pos>(e1, Pos{ 1, 1 });
    world.Add<Pos>(e2, Pos{ 2, 2 });
    world.Add<Pos>(e3, Pos{ 3, 3 });

    world.Destroy(e2);

    REQUIRE_FALSE(world.Alive(e2));
    REQUIRE(world.Alive(e1));
    REQUIRE(world.Alive(e3));

    REQUIRE(world.Get<Pos>(e1).x == Catch::Approx(1.0f));
    REQUIRE(world.Get<Pos>(e3).x == Catch::Approx(3.0f));
}

#endif