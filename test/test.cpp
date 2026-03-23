#include <iostream>
#include <chrono>
#include "ECS.h"
#include "catch_amalgamated.hpp"
#include <random>

using namespace ECS;


//preferably read all of the comments to understand what this even is
//catch_amalgamated.hpp and catch_amalgamated.cpp implements UNIT TESTS
//more info about unit tests on github page:
//https://github.com/catchorg/Catch2

//for "practical" ecs usage examples see:
//https://github.com/jekabsv/ECS

// ECS (Entity Component System) is an architectural design common in game development.
// The idea is to separate data from behaviour, which leads to better cache performance.
// any ECS has three main building blocks:
// Entity - just a unique ID, handle for the actual entity data.
// Component - plain data attached to an entity (e.g. Pos, Vel). No logic.
// System - logic that runs over all entities that have a specific set of components.
//
// Example: a "move" system queries every entity that has both Pos and Vel,
// and updates positions each frame. Entities that only have Pos are simply ignored.

#ifdef _DEBUG
//var skaisti rakstīt testus šeit, palaist testus var kompilējot un palaižot programmu debug konfigurācijā

TEST_CASE("Entity creation and deletion", "[entity]") {
	World world;
	Entity e = world.Create();
	Entity e2 = world.Create();

	REQUIRE(e != e2);
	REQUIRE(world.Alive(e));

	world.Destroy(e);

	REQUIRE(!world.Alive(e));

	Entity e3 = world.Create();

	REQUIRE(EntityId(e3) == EntityId(e));
	REQUIRE(EntityGeneration(e3) != EntityGeneration(e));
}

TEST_CASE("Query performance", "[benchmark]") {
	World world;
	std::vector<Entity> v(10e5);
	struct Pos { float x, y; };
	struct Vel { float vx, vy; };
	for (auto& e : v){
		e = world.Create();
		world.Add<Pos, Vel>(e, Pos{ 1,2 }, Vel{ 1,2 });
	}

	std::vector<Entity> wrong_entities1(10e4);
	for (auto& e : wrong_entities1) {
		e = world.Create();
		world.Add<Pos>(e, Pos{ 1,2 });
	}

	BENCHMARK("querry performance") {
		world.Query<Pos, Vel>(
			[](Entity e, Pos& pos, Vel& vel)
			{
				pos.x += vel.vx;
				pos.y += vel.vy;
			});
	};

}

TEST_CASE("System performance", "[benchmark]") {
	World world;
	std::vector<Entity> v(10e5);
	struct Pos { float x, y; };
	struct Vel { float vx, vy; };
	for (auto& e : v) {
		e = world.Create();
		world.Add<Pos, Vel>(e, Pos{ 1,2 }, Vel{ 1,2 });
	}

	std::vector<Entity> wrong_entities1(10e4);
	for (auto& e : wrong_entities1) {
		e = world.Create();
		world.Add<Pos>(e, Pos{ 1,2 });
	}


	world.RegisterSystem<Pos, Vel>("move",
		[](ArchetypeContext ctx, float dt, SharedDataRef _data)
		{
			auto pos = ctx.Slice<Pos>();
			auto vel = ctx.Slice<Vel>();

			for (int i = 0; i < pos.size(); i++)
			{
				pos[i].x += vel[i].vx;
				pos[i].y += vel[i].vy;
			}
		}, SystemGroup::Update);

	BENCHMARK("system performance")
	{
		world.Run(SystemGroup::Update, 0.016f);
	};

}

#else


//random
std::mt19937 rng(std::random_device{}());
std::uniform_int_distribution<int> dist(1, 1000);

//windows cmd initialization dont worry bout this
#if __has_include("windows.h")
#include "windows.h"
std::vector<CHAR_INFO> screenBuffer;
int cols;
int rows;
HANDLE hOut;
HANDLE hBuffer;

void InitBuffer(int cols, int rows) {
	hOut = GetStdHandle(STD_OUTPUT_HANDLE);
	hBuffer = CreateConsoleScreenBuffer(GENERIC_READ | GENERIC_WRITE, 0, NULL, CONSOLE_TEXTMODE_BUFFER, NULL);
	COORD size = { (SHORT)cols, (SHORT)rows };
	SetConsoleScreenBufferSize(hBuffer, size);
	screenBuffer.resize(cols * rows);
}

void ClearBuffer(int cols, int rows) {
	for (auto& c : screenBuffer) {
		c.Char.AsciiChar = ' ';
		c.Attributes = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
	}
}

void DrawChar(int x, int y, char ch) {
	if (x < 0 || x >= cols || y < 0 || y >= rows) return;
	screenBuffer[y * cols + x].Char.AsciiChar = ch;
	screenBuffer[y * cols + x].Attributes = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
}

void Present(int cols, int rows) {
	COORD bufSize = { (SHORT)cols, (SHORT)rows };
	COORD bufOrigin = { 0, 0 };
	SMALL_RECT region = { 0, 0, (SHORT)(cols - 1), (SHORT)(rows - 1) };
	WriteConsoleOutput(hBuffer, screenBuffer.data(), bufSize, bufOrigin, &region);
	SetConsoleActiveScreenBuffer(hBuffer);
}
#else
#endif


//Shared data that will be available to each entity
struct SharedData{ int ScreenW, ScreenH; };

//Some POD (plain old data), mutable and trivially copyable Components
struct Pos { float x, y; };
struct Vel { float vx, vy; };



int main()
{
	//Just setting up cmd, dont worry bout this
#if __has_include("windows.h")
	CONSOLE_CURSOR_INFO ci = { 1, FALSE };
	SetConsoleCursorInfo(GetStdHandle(STD_OUTPUT_HANDLE), &ci);
	keybd_event(VK_MENU, 0, 0, 0);
	keybd_event(VK_RETURN, 0, 0, 0);
	keybd_event(VK_RETURN, 0, KEYEVENTF_KEYUP, 0);
	keybd_event(VK_MENU, 0, KEYEVENTF_KEYUP, 0);
	Sleep(500);
	CONSOLE_SCREEN_BUFFER_INFO csbi = {};
	GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
	cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
	rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
	InitBuffer(cols, rows);
#endif

	//now to ECS


	//ECS world initialization
	World world;

	//Shared Data setup
	SharedData data{ cols , rows };
	world.Tie(std::make_shared<SharedData>(data));

	unsigned int N = 50; //number of test entities
	//on large N initialisation will take longer but after initialisation framerate should be fine
	//on very large N (millions) initialisation might be a bit too long
	
	//entity creation
	std::vector<Entity> entities(N);

	for (int i = 0; i < N; i++)
		entities[i] = world.Create();
	
	//adding components to entity
	for (int i = 0; i < N; i++) {
		//assigning some random velocitites
		float x = dist(rng) / 100.0f;
		float y = dist(rng) / 100.0f;
		world.Add<Pos, Vel>(entities[i], Pos{0, 0}, Vel{x, y});
	}

	//Systems are functions saved and run later on global update/render, each active system runs once for each archetype 
	//and exposes each archetypes context as ArchetypeContext ctx
 
	//Systems assigned to Update all run on world.Run(SystemGroup::Update, dt) each frame
	//adding systems to Update
	world.RegisterSystem<Pos, Vel>("move",
		[](ECS::ArchetypeContext ctx, float dt, SharedDataRef _data)
		{
			auto pos = ctx.Slice<Pos>();
			auto vel = ctx.Slice<Vel>();
			for (int i = 0; i < pos.size(); i++)
			{
				pos[i].x += vel[i].vx * dt;
				pos[i].y += vel[i].vy * dt;

				if (pos[i].x >= _data->ScreenW || pos[i].x <= 0) {
					pos[i].x -= vel[i].vx * dt;
					vel[i].vx *= -1;
				}
				if (pos[i].y >= _data->ScreenH || pos[i].y <= 0) {
					pos[i].y -= vel[i].vy * dt;
					vel[i].vy *= -1;
				}
			}


		}, 
		SystemGroup::Update).Read<Pos>().Write<Pos>().Read<Vel>();

	world.RegisterSystem<Pos>("NoMovement",
		[](ECS::ArchetypeContext ctx, float dt, SharedDataRef _data)
		{
			auto pos = ctx.Slice<Pos>();
			for (auto [x, y] : pos)
			{
				//set position to 0;
				//if this system is active then pixels would not move
				x = 0;
				y = 0;
			}


		},
		SystemGroup::Update).Read<Pos>().Write<Pos>().Read<Vel>();

	//Disable system
	world.DisableSystem("NoMovement");

	//Systems assigned to Update all run on world.Run(SystemGroup::Render, dt) each frame
	//adding system to Render
	world.RegisterSystem<Pos>("draw",
		[](ECS::ArchetypeContext ctx, float dt, SharedDataRef _data)
		{
			auto pos = ctx.Slice<Pos>();
			for (const auto [x, y] : pos)
				DrawChar(x, y, '#');
			
		}, 
		SystemGroup::Render).Read<Pos>();


	//Querries are similar to systems but are exectued immeadiately and not for each archetype but for each entity
	//Note that Systems are way faster then Querries if used properly as seen by the performance benchmark in the test cases provided
	world.Query<Vel>([](Entity e, Vel& v)
		{
			v.vx += 5;
			v.vy += 5;
		});


	//Game Loop
	int TARGET_FRAMERATE = 60.0f;
	float TARGET_FRAME_TIME = 1.0f / TARGET_FRAMERATE;
	auto prev = std::chrono::high_resolution_clock::now();
	while (true)
	{
		//chrono Delta time
		auto now = std::chrono::high_resolution_clock::now();
		float dt = std::chrono::duration<float>(now - prev).count();
		prev = now;


		//ECS update
		//Updating all Systems assigned to Update
		world.Run(SystemGroup::Update, dt);
		ClearBuffer(cols, rows);
		//Updating all Systems assigned to Render
		world.Run(SystemGroup::Render, dt);
		Present(cols, rows);


		//chrono Delta time
		auto elapsed = std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - prev).count();
		float sleepTime = TARGET_FRAME_TIME - elapsed;
		if (sleepTime > 0)
			Sleep(sleepTime * 1000);
	}
}

#endif