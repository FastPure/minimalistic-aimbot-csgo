/*

copyright : FastPure.

*/


#include <Windows.h>
#include <iostream>
#include <thread>
#include <Psapi.h>
#include <TlHelp32.h>

namespace ctx
{
	HANDLE process_handle = NULL;

	uintptr_t client = NULL, engine = NULL;

	struct vector
	{
		float x, y, z;

		vector& operator+(vector arg)
		{
			x += arg.x;
			y += arg.y;
			z += arg.z;
			return *this;
		}

		vector& operator-(vector arg)
		{
			x -= arg.x;
			y -= arg.y;
			z -= arg.z;
			return *this;
		}
	};

	namespace mem
	{
		template <class t>
		t read(uintptr_t address)
		{
			t read;
			ReadProcessMemory(process_handle, (LPVOID)address, &read, sizeof(read), NULL);
			return read;
		};

		template <class t>
		void write(uintptr_t address, t info)
		{
			WriteProcessMemory(process_handle, (LPVOID)address, &info, sizeof(info), NULL);
		};
	}

	namespace ent
	{
		namespace client_state
		{
			uintptr_t clientstate_ptr()
			{
				return mem::read<uintptr_t>(engine + 0x589D9C);
			};

			bool in_game()
			{
				auto current_state = mem::read<uintptr_t>(clientstate_ptr() + 0x108);

				if (current_state == 6)
					return true;

				return false;
			};

			vector get_view_angle()
			{
				return mem::read<vector>(clientstate_ptr() + 0x4D88);
			};

			vector set_view_angle(vector set)
			{
				mem::write<vector>(clientstate_ptr() + 0x4D88, set);
				return {};
			};
		}
		
		class prop
		{
		public:
			uintptr_t team()
			{
				return mem::read<uintptr_t>(uintptr_t(this) + 0xF4);
			};

			uintptr_t health()
			{
				return mem::read<uintptr_t>(uintptr_t(this) + 0x100);
			};

			bool dormant()
			{
				return mem::read<bool>(uintptr_t(this) + 0xED);
			}

			vector view_offset()
			{
				return mem::read<vector>(uintptr_t(this) + 0x108);
			};

			vector origin()
			{
				return mem::read<vector>(uintptr_t(this) + 0x138);
			}

			vector bone_pos(size_t id)
			{
				uintptr_t bone_matrix = mem::read<uintptr_t>(uintptr_t(this) + 0x26A8);
				
				struct bone_struct
				{
					float x;
					char _pad4[0xC];
					float y;
					char _pad14[0xC];
					float z;
				};

				bone_struct bone = mem::read<bone_struct>(bone_matrix + 0x30 * id + 0xC);

				vector pos = {};

				pos.x = bone.x;
				pos.y = bone.y;
				pos.z = bone.z;

				return pos;
			}
		};

		prop* local_ptr()
		{
			return mem::read<prop*>(client + 0xD30B84);
		};

		prop* entity_ptr(size_t current)
		{
			return mem::read<prop*>(client + 0x4D44A04 + (current * 0x10));
		};
	}
}

int main()
{		
	SetConsoleTitle("minimalistic");

	auto get_all = []()->bool
	{
		PROCESSENTRY32 process_entry;

		process_entry.dwSize = sizeof(PROCESSENTRY32);

		HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);

		if (snapshot == INVALID_HANDLE_VALUE)
		{
			std::cout << "[!] failed to get snapshot of process." << std::endl;
			return false;
		}

		while (Process32Next(snapshot, &process_entry))
		{
			if (!std::strcmp(process_entry.szExeFile, "csgo.exe"))
			{
				ctx::process_handle = OpenProcess(PROCESS_ALL_ACCESS, FALSE, process_entry.th32ProcessID);

				MODULEENTRY32 module_entry;

				module_entry.dwSize = sizeof(MODULEENTRY32);

				HANDLE process_module = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, process_entry.th32ProcessID);

				while (Module32Next(process_module, &module_entry))
				{
					if (!std::strcmp(module_entry.szModule, "client_panorama.dll"))
						ctx::client = (uintptr_t)module_entry.modBaseAddr;

					if (!std::strcmp(module_entry.szModule, "engine.dll"))
						ctx::engine = (uintptr_t)module_entry.modBaseAddr;
				}

				if (ctx::client == NULL || ctx::engine == NULL)
					return false;

				std::cout << "[>] found csgo || pid -> " << process_entry.th32ProcessID << std::endl
					<< "[>] client panorama -> " << ctx::client << " || engine -> " << ctx::engine << std::endl;

				return true;
			}
		}

		std::cout << "[!] csgo not found - trying again..." << std::endl;

		return false;
	};

	while (!get_all())
	{
		std::this_thread::sleep_for(std::chrono::seconds(5));
		std::system("cls");
	}

	while (!GetAsyncKeyState(VK_END))
	{
		if (ctx::ent::client_state::in_game())
		{
			ctx::ent::prop* loc = ctx::ent::local_ptr();

			auto calc_angle = [loc](ctx::vector target)
			{
				ctx::vector aim_at{};

				ctx::vector pos = loc->origin() + loc->view_offset();

				ctx::vector delta = { target.x - pos.x, target.y - pos.y, target.z - pos.z };
				float length = sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);

				double pitch = -asin(delta.z / length) * (180 / 3.14159265358);
				double yaw = atan2(delta.y, delta.x) * (180 / 3.14159265358);

				if (pitch >= -89 && pitch <= 89 && yaw >= -180 && yaw <= 180)
				{
					aim_at.x = static_cast<float>(pitch);
					aim_at.y = static_cast<float>(yaw);
				}

				return aim_at;
			};

			auto best_tagert = [loc]()->ctx::ent::prop*
			{
				size_t index = -1;
				float max_dist = 10 * 10000;

				for (size_t i = 1; i < 32; i++)
				{
					ctx::ent::prop* ent = ctx::ent::entity_ptr(i);

					if (!ent || ent->health() == 0 || ent->team() == loc->team() || ent->dormant() )
						continue;

					ctx::vector me = loc->origin(),
						opponent = ent->origin();

					ctx::vector delta = { opponent.x - me.x, opponent.y - me.y, opponent.z - me.z };
					float length = sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);

					if (length < max_dist)
					{
						max_dist = length;
						index = i;
					}
				}

				if (index == -1)
					return nullptr;

				return ctx::ent::entity_ptr(index);
			};
			
			ctx::ent::prop * target = best_tagert();

			if (target)
			{
				ctx::vector delta = calc_angle(target->bone_pos(8)) - ctx::ent::client_state::get_view_angle();

				auto clamp=[](ctx::vector& vector)->ctx::vector
				{
					auto normalize = [](float vectorValue)
					{
						if (!isfinite(vectorValue)) 
						{
							vectorValue = 0.0f; 
						}
						return remainder(vectorValue, 360.0f);
					};

					vector.x = max(-89.0f, min(89.0f, normalize(vector.x)));
					vector.y = normalize(vector.y);
					vector.z = 0.f;
					return vector;
				};

				clamp(delta);
				
				float fov = sqrtf(powf(delta.x, 2.f) + powf(delta.y, 2.f));
				
				if (fov <= 15)
					ctx::ent::client_state::set_view_angle(calc_angle(target->bone_pos(8)));
			}
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	std::quick_exit(0);
}