/*
This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or
distribute this software, either in source code form or as a compiled
binary, for any purpose, commercial or non-commercial, and by any
means.

In jurisdictions that recognize copyright laws, the author or authors
of this software dedicate any and all copyright interest in the
software to the public domain. We make this dedication for the benefit
of the public at large and to the detriment of our heirs and
successors. We intend this dedication to be an overt act of
relinquishment in perpetuity of all present and future rights to this
software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

For more information, please refer to <http://unlicense.org/>
*/

#include <Windows.h>

#include <algorithm>
#include <cassert>
#include <iostream>
#include <vector>
#include <type_traits>


struct Memory
{
	HANDLE const hProcess;

	Memory(HANDLE const hProcess)
		: hProcess(hProcess)
	{ }

	~Memory()
	{
		if (hProcess) {
			CloseHandle(hProcess);
		}
	}

	template <typename T>
	inline T Read(DWORD address) const
	{
		static_assert(std::is_pod<T>::value, "T must be plain old data.");

		T data = {0};
		SIZE_T numRead = -1;
		auto success = ReadProcessMemory(hProcess, reinterpret_cast<LPCVOID>(address), 
			&data, sizeof(T), &numRead);

		assert(success && numRead == sizeof(T));
		
		return data;
	}

	template <typename T>
	inline T& Read(DWORD address, T& data) const
	{
		static_assert(std::is_pod<T>::value, "T must be plain old data.");

		SIZE_T numRead = -1;
		auto success = ReadProcessMemory(hProcess, reinterpret_cast<LPCVOID>(address), 
			&data, sizeof(T), &numRead);

		assert(success && numRead == sizeof(T));

		return data;
	}

	void ReadBytes(DWORD address, void* buffer, SIZE_T length) const
	{
		SIZE_T numRead = -1;
		auto success = ReadProcessMemory(hProcess, reinterpret_cast<LPCVOID>(address),
			buffer, length, &numRead);

		assert(success && numRead == length);
	}

	template <typename T>
	inline void Write(DWORD address, T data) const
	{
		static_assert(std::is_pod<T>::value, "T must be plain old data.");

		SIZE_T numWritten = -1;
		auto success = WriteProcessMemory(hProcess, reinterpret_cast<LPVOID>(address),
			&data, sizeof(T), &numWritten);

		assert(success && numWritten == sizeof(T));
	}
};


struct SuperHexagonApi
{
#pragma pack(push, 1)
	struct Wall
	{
		DWORD slot;
		DWORD distance;
		BYTE enabled;
		BYTE fill1[3];
		DWORD unk2;
		DWORD unk3;
	};
#pragma pack(pop)

	static_assert(sizeof(Wall) == 20, "Wall struct must be 0x14 bytes total.");

	// ASLR is off in Super Hexagon.
	struct Offsets
	{
		enum : DWORD
		{
			BasePointer = 0x694B00,

			NumSlots = 0x1BC,
			NumWalls = 0x2930,
			FirstWall = 0x220,
			PlayerAngle = 0x2958,
			PlayerAngle2 = 0x2954,
			MouseDownLeft = 0x42858,
			MouseDownRight = 0x4285A,
			MouseDown = 0x42C45,
			WorldAngle = 0x1AC
		};
	};

	DWORD appBase;
	Memory const& memory;
	std::vector<Wall> walls;

	SuperHexagonApi(Memory const& memory)
		: memory(memory)
	{
		appBase = memory.Read<DWORD>(Offsets::BasePointer);
		assert(appBase != 0);
	}

	DWORD GetNumSlots() const
	{
		return memory.Read<DWORD>(appBase + Offsets::NumSlots);
	}

	DWORD GetNumWalls() const
	{
		return memory.Read<DWORD>(appBase + Offsets::NumWalls);
	}

	void UpdateWalls()
	{
		walls.clear();
		auto const numWalls = GetNumWalls();
		walls.resize(numWalls);
		memory.ReadBytes(appBase + Offsets::FirstWall, walls.data(), sizeof(Wall) * numWalls);
	}

	DWORD GetPlayerAngle() const
	{
		return memory.Read<DWORD>(appBase + Offsets::PlayerAngle);
	}

	void SetPlayerSlot(DWORD slot) const
	{
		// Move into the center of a given slot number.
		DWORD const angle = 360 / GetNumSlots() * (slot % GetNumSlots()) + (180 / GetNumSlots());
		memory.Write(appBase + Offsets::PlayerAngle, angle);
		memory.Write(appBase + Offsets::PlayerAngle2, angle);
	}

	DWORD GetPlayerSlot() const
	{
		float const angle = static_cast<float>(GetPlayerAngle());
		return static_cast<DWORD>(angle / 360.0f * GetNumSlots());
	}

	void StartMovingLeft() const
	{
		memory.Write<BYTE>(appBase + Offsets::MouseDownLeft, 1);
		memory.Write<BYTE>(appBase + Offsets::MouseDown, 1);
	}

	void StartMovingRight() const
	{
		memory.Write<BYTE>(appBase + Offsets::MouseDownRight, 1);
		memory.Write<BYTE>(appBase + Offsets::MouseDown, 1);
	}

	void ReleaseMouse() const
	{
		memory.Write<BYTE>(appBase + Offsets::MouseDownLeft, 0);
		memory.Write<BYTE>(appBase + Offsets::MouseDownRight, 0);
		memory.Write<BYTE>(appBase + Offsets::MouseDown, 0);
	}

	DWORD GetWorldAngle() const
	{
		return memory.Read<DWORD>(appBase + Offsets::WorldAngle);
	}

	void SetWorldAngle(DWORD angle) const
	{
		memory.Write<DWORD>(appBase + Offsets::WorldAngle, angle);
	}
};

int main(int argc, char** argv, char** env)
{
	auto hWnd = FindWindow(nullptr, L"Super Hexagon");
	assert(hWnd);

	DWORD processId = -1;
	GetWindowThreadProcessId(hWnd, &processId);
	assert(processId > 0);

	auto const hProcess = OpenProcess(
		PROCESS_VM_READ |     // For ReadProcessMemory
		PROCESS_VM_WRITE |    // For WriteProcessMemory
		PROCESS_VM_OPERATION, // For WriteProcessMemory
		FALSE, processId);

	assert(hProcess);

	Memory const memory(hProcess);
	SuperHexagonApi api(memory);

	for (;;) {
		api.UpdateWalls();
		if (!api.walls.empty()) {
			auto const numSlots = api.GetNumSlots();
			std::vector<DWORD> minDistances(numSlots, -1);

			std::for_each(api.walls.begin(), api.walls.end(), [&] (SuperHexagonApi::Wall const& a) {
				if (a.distance > 0 && a.enabled) {
					minDistances[a.slot % numSlots] = min(minDistances[a.slot % numSlots], a.distance);
				}
			});

			auto const maxElement = std::max_element(minDistances.begin(), minDistances.end());
			DWORD const targetSlot = static_cast<DWORD>(std::distance(minDistances.begin(), maxElement));
			std::cout << "Moving to slot [" << targetSlot << "]; world angle is: " << api.GetWorldAngle() << ".\n";

			// TODO: Move properly instead of teleporting around; requires some more wall processing logic.
			api.SetPlayerSlot(targetSlot);
		}

		Sleep(10);
		system("cls"); // Oh the humanity.
	}

	return 0;
}
