#include "Hooks.h"

#include "Trails.h"

namespace Hooks
{
	namespace
	{
		// The trails are placed once a frame after the skeletons got this frame's pose: from the camera's update
		// (it follows the animated body, so everything has moved by then). The player's update (once a frame, before
		// the animations) collects the frame's game time and stands in when no hooked camera state ran last frame
		// (kill cams, VATS, furniture).
		float pendingDelta{ 0.0f };
		bool  pending{ false };

		void RunFrame()
		{
			const float delta = pendingDelta;
			pendingDelta = 0.0f;
			pending = false;
			Trails::Frame(delta);
		}

		struct PlayerUpdate
		{
			static void thunk(RE::PlayerCharacter* a_this, float a_delta)
			{
				func(a_this, a_delta);
				if (pending) {
					RunFrame();
				}
				pendingDelta += a_delta;
				pending = true;
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		template <std::size_t N>
		struct CameraUpdate
		{
			static void thunk(RE::TESCameraState* a_this, RE::BSTSmartPointer<RE::TESCameraState>& a_next)
			{
				func(a_this, a_next);
				if (pending) {
					RunFrame();
				}
			}
			static inline REL::Relocation<decltype(thunk)> func;

			static void Install(REL::VariantID a_vtable)
			{
				REL::Relocation<std::uintptr_t> vtable{ a_vtable };
				func = vtable.write_vfunc(0x3, thunk);
			}
		};
	}

	void Install()
	{
		REL::Relocation<std::uintptr_t> player{ RE::VTABLE_PlayerCharacter[0] };
		PlayerUpdate::func = player.write_vfunc(REL::Module::IsVR() ? 0xAF : 0xAD, PlayerUpdate::thunk);
		CameraUpdate<0>::Install(RE::VTABLE_ThirdPersonState[0]);
		CameraUpdate<1>::Install(RE::VTABLE_FirstPersonState[0]);
		CameraUpdate<2>::Install(RE::VTABLE_HorseCameraState[0]);
		CameraUpdate<3>::Install(RE::VTABLE_BleedoutCameraState[0]);
		CameraUpdate<4>::Install(RE::VTABLE_DragonCameraState[0]);
		logs::info("hooked the player update and the camera states ({})", REL::Module::IsVR() ? "Skyrim VR" : REL::Module::IsSE() ? "Skyrim SE" : "Skyrim AE");
	}
}
