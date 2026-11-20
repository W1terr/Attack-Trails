#pragma once

#include "Settings.h"

// The trails. Every watched actor's weapons are sampled once a frame (where they are against the actor's skeleton
// root). A strike (traced back from its hit event, or Precision's hit windows) lays glowing particles and a thin line
// along the path of the part that strikes: copies of meshes\AttackTrails\Particles.nif and Line.nif, hung under
// the skeleton root, their bones placed in world space every frame. Arrows shot from a bow held drawn long enough lay
// the same along their flight. Colors come from the weapon list, the enchantment list, the color the enchantment
// glows with on items, or the enchantment's kind (bound, ghost, fire...).
namespace Trails
{
	void Init();   // data loaded: loads the model
	void Reset();  // a game loads: every trail and watched actor dropped

	// once a frame, after the skeletons' update (the camera's), with the game time passed since the last call
	void Frame(float a_delta);

	// The color a magic effect's enchantment glows with on items (its enchant shader's), if it has a clear one: false
	// for shaders whose color comes from their textures (fire, shock) or that are white or grey.
	bool GlowColor(const RE::EffectSetting* a_effect, std::array<float, 3>& a_color);

	// the kind of one magic effect (fire, frost, shock, drains, archetypes, else other)
	Settings::Element ElementOfEffect(const RE::EffectSetting* a_effect);

	// what the player holds, for the Weapons and Colors pages
	struct HandStatus
	{
		bool                 weapon{ false };  // a melee weapon or a bow drawn in this hand (or a fist raised)
		bool                 fist{ false };    // the bare (or gauntleted) fist: name = the gauntlets', if any
		std::string          name;
		Settings::Element    element{ Settings::kPlain };
		bool                 unique{ false };   // counts as a unique weapon
		bool                 listed{ false };   // in the weapon list
		bool                 trailOn{ false };  // the settings let it leave trails
		std::array<float, 3> color{ 1.0f, 1.0f, 1.0f };
		std::string          plugin;            // where the weapon comes from (its first file)
		std::uint32_t        id{ 0 };           // its id in that plugin

		// the enchantment's magic effect that colors it (the listed one, else the costliest), if any
		std::string   effectName;
		std::string   effectPlugin;
		std::uint32_t effectId{ 0 };
	};

	struct Status
	{
		std::array<HandStatus, 2> hands{};  // right, left
	};
	Status GetStatus();
}
