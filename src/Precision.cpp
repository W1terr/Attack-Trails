#include "Precision.h"

#include <Windows.h>
#include <detours.h>
#include <intrin.h>

#pragma intrinsic(_ReturnAddress)

namespace Precision
{
	namespace
	{
		std::uintptr_t    moduleBegin{ 0 };
		std::uintptr_t    moduleEnd{ 0 };
		bool              hooked{ false };
		std::atomic<bool> hideAtOnce{ false };

		std::mutex           spawnedLock;
		std::vector<Spawned> spawned;

		bool FromPrecision(const void* a_address)
		{
			const auto at = reinterpret_cast<std::uintptr_t>(a_address);
			return at >= moduleBegin && at < moduleEnd;
		}

		// the node whose world transform these are, if they are one (Precision passes node->world.rotate and
		// node->world.translate straight from the node)
		RE::NiAVObject* NodeOf(const RE::NiMatrix3& a_rotate, const RE::NiPoint3& a_position)
		{
			constexpr std::size_t kWorld = 0x7C;                         // NiAVObject::world, every runtime
			constexpr std::size_t kTranslate = sizeof(RE::NiMatrix3);    // NiTransform: rotate, then translate
			const auto            rotate = reinterpret_cast<std::uintptr_t>(&a_rotate);
			const auto            position = reinterpret_cast<std::uintptr_t>(&a_position);
			if (position != rotate + kTranslate || rotate < kWorld) {
				return nullptr;
			}
			const auto node = reinterpret_cast<RE::NiAVObject*>(rotate - kWorld);
			// only a node (the WEAPON / SHIELD nodes are plain NiNodes): its vtable says so
			static const auto nodeVtable = RE::VTABLE_NiNode[0].address();
			return *reinterpret_cast<const std::uintptr_t*>(node) == nodeVtable ? node : nullptr;
		}

		// BSTempEffectParticle::Spawn(cell, lifetime, model, rotation matrix, position, scale, flags, target)
		using Spawn_t = RE::BSTempEffectParticle*(RE::TESObjectCELL*, float, const char*, const RE::NiMatrix3&, const RE::NiPoint3&, float, std::uint32_t,
			RE::NiAVObject*);
		Spawn_t* spawn{ nullptr };

		RE::BSTempEffectParticle* Spawn(RE::TESObjectCELL* a_cell, float a_lifetime, const char* a_model, const RE::NiMatrix3& a_rotate,
			const RE::NiPoint3& a_position, float a_scale, std::uint32_t a_flags, RE::NiAVObject* a_target)
		{
			const auto caller = _ReturnAddress();
			const auto result = spawn(a_cell, a_lifetime, a_model, a_rotate, a_position, a_scale, a_flags, a_target);
			if (!result || !FromPrecision(caller)) {
				return result;
			}
			const auto node = NodeOf(a_rotate, a_position);
			if (!node) {
				return result;
			}
			Spawned s{ RE::NiPointer<RE::BSTempEffectParticle>(result), RE::NiPointer<RE::NiAVObject>(node), false };
			if (hideAtOnce.load(std::memory_order_relaxed) && result->particleObject) {
				result->particleObject->SetAppCulled(true);  // not in the scene yet
				s.hiddenAtOnce = true;
			}
			std::scoped_lock guard(spawnedLock);
			spawned.push_back(std::move(s));
			return result;
		}
	}

	void Install()
	{
		const auto module = GetModuleHandleW(L"Precision.dll");
		if (!module) {
			logs::info("Precision is not loaded");
			return;
		}
		const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
		const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<const std::byte*>(module) + dos->e_lfanew);
		moduleBegin = reinterpret_cast<std::uintptr_t>(module);
		moduleEnd = moduleBegin + nt->OptionalHeader.SizeOfImage;

		const REL::RelocationID id(29219, 30072);
		spawn = reinterpret_cast<Spawn_t*>(id.address());
		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		auto error = DetourAttach(reinterpret_cast<PVOID*>(&spawn), reinterpret_cast<PVOID>(&Spawn));
		if (error != NO_ERROR) {
			DetourTransactionAbort();
			logs::error("Precision found, but its trails can't be told apart (DetourAttach error {})", error);
			return;
		}
		error = DetourTransactionCommit();
		if (error != NO_ERROR) {
			logs::error("Precision found, but its trails can't be told apart (DetourTransactionCommit error {})", error);
			return;
		}
		hooked = true;
		logs::info("Precision found: its trails are watched (spawn hooked at {:X})", id.address() - REL::Module::get().base());
	}

	bool Found() { return hooked; }

	std::vector<Spawned> Take()
	{
		std::scoped_lock guard(spawnedLock);
		return std::exchange(spawned, {});
	}

	void SetHideAtOnce(bool a_hide) { hideAtOnce.store(a_hide, std::memory_order_relaxed); }
}
