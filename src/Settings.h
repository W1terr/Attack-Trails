#pragma once

namespace Settings
{
	// what a weapon is enchanted with, which picks the trail's color
	enum Element : std::size_t
	{
		kPlain,  // no enchantment
		kFire,
		kFrost,
		kShock,
		kChaos,       // fire, frost and shock together (Dragonborn's chaos damage)
		kHealth,      // drains or absorbs health
		kMagicka,     // drains or absorbs magicka
		kStamina,     // drains or absorbs stamina
		kSoulTrap,
		kParalysis,
		kFear,
		kTurnUndead,
		kBanish,
		kCalm,
		kFrenzy,
		kBound,       // bound weapons (conjured), whatever else
		kGhost,       // ghosts' weapons and the Ghostblade
		kOther,       // enchanted with something else
		kElements
	};
	inline constexpr std::array<const char*, kElements> kElementNames{ "No enchantment", "Fire", "Frost", "Shock", "Chaos", "Health", "Magicka",
		"Stamina", "Soul trap", "Paralysis", "Fear", "Turn undead", "Banish", "Calm", "Frenzy", "Bound weapons", "Ghost weapons",
		"Other enchantments" };

	struct ElementSettings
	{
		bool                 enabled{ true };
		std::array<float, 3> color{ 1.0f, 1.0f, 1.0f };

		bool operator==(const ElementSettings&) const = default;
	};

	inline const std::array<ElementSettings, kElements> kDefaultElements{ {
		{ true, { 1.0f, 1.0f, 1.0f } },     // white
		{ true, { 1.0f, 0.22f, 0.08f } },   // red
		{ true, { 0.55f, 0.86f, 1.0f } },   // light blue
		{ true, { 0.16f, 0.3f, 1.0f } },    // dark blue
		{ true, { 1.0f, 0.35f, 0.75f } },   // magenta
		{ true, { 0.9f, 0.04f, 0.06f } },   // red
		{ true, { 0.2f, 0.5f, 1.0f } },     // blue
		{ true, { 0.25f, 1.0f, 0.3f } },    // green
		{ true, { 0.6f, 0.25f, 1.0f } },    // violet
		{ true, { 0.85f, 1.0f, 0.35f } },   // yellow green
		{ true, { 0.7f, 0.1f, 0.45f } },    // dark purple red
		{ true, { 1.0f, 0.93f, 0.65f } },   // pale gold
		{ true, { 0.2f, 0.95f, 0.85f } },   // teal
		{ true, { 1.0f, 0.7f, 0.85f } },    // soft pink
		{ true, { 1.0f, 0.5f, 0.0f } },     // orange
		{ true, { 0.65f, 0.25f, 1.0f } },   // purple
		{ true, { 0.55f, 0.9f, 1.0f } },    // light blue
		{ true, { 1.0f, 1.0f, 1.0f } },     // white, like no enchantment
	} };

	// which weapons leave trails (the weapon list below always has the last word)
	enum WeaponFilter : int
	{
		kAllWeapons,
		kUniqueWeapons,  // daedric artifacts and weapons made with their own enchantment
		kListedWeapons,  // only the weapons in the list
		kWeaponFilters
	};
	inline constexpr std::array<const char*, kWeaponFilters> kWeaponFilterNames{ "All weapons", "Unique weapons only", "Only the weapons in the list" };

	// one weapon of the list, by its plugin and form id (the id without the plugin's load order index)
	struct WeaponRule
	{
		std::string          plugin;
		std::uint32_t        id{ 0 };
		std::string          name;              // as it was added (shown while its plugin isn't loaded)
		bool                 trail{ true };     // on: it always leaves trails; off: never
		bool                 ownColor{ true };  // its own color instead of its enchantment's
		std::array<float, 3> color{ 1.0f, 1.0f, 1.0f };

		bool operator==(const WeaponRule&) const = default;
	};

	// the list a fresh install starts with (and "Add the suggested colors")
	inline const std::vector<WeaponRule> kSuggestedWeapons{
		{ "Skyrim.esm", 0x04E4EE, "Dawnbreaker", true, true, { 1.0f, 0.78f, 0.22f } },          // golden yellow
		{ "Skyrim.esm", 0x04A38F, "Ebony Blade", true, true, { 0.5f, 0.12f, 0.95f } },          // dark violet
		{ "Skyrim.esm", 0x0EA29C, "Ebony Blade (real)", true, true, { 0.5f, 0.12f, 0.95f } },
		{ "Skyrim.esm", 0x0240D2, "Mehrunes' Razor", true, true, { 0.85f, 0.05f, 0.1f } },      // blood red
		{ "Skyrim.esm", 0x0233E3, "Mace of Molag Bal", true, true, { 0.75f, 0.2f, 0.55f } },    // purple pink
		{ "Skyrim.esm", 0x02ACD2, "Volendrung", true, true, { 1.0f, 0.55f, 0.15f } },           // amber
		{ "Dawnguard.esm", 0x0067CF, "Harkon's Sword", true, true, { 0.7f, 0.0f, 0.08f } },     // dark blood
		{ "Dragonborn.esm", 0x01AEA4, "Bloodskal Blade", true, true, { 1.0f, 0.12f, 0.12f } },  // red
	};

	// one enchantment of the color list: a magic effect (by its plugin and form id), so every enchantment made with it
	// counts, the game's, other mods' and the ones you make yourself
	struct EnchantmentRule
	{
		std::string          plugin;
		std::uint32_t        id{ 0 };
		std::string          name;           // as it was added (shown while its plugin isn't loaded)
		bool                 trail{ true };  // off: items enchanted with it leave no trail
		std::array<float, 3> color{ 1.0f, 1.0f, 1.0f };

		bool operator==(const EnchantmentRule&) const = default;
	};

	// Precision (by Ershin) draws trails of its own on weapon swings: which mod draws them, for two kinds of weapons
	enum PrecisionChoice : int
	{
		kBothTrails,       // both mods' trails
		kOwnTrails,        // only these trails: Precision's are hidden
		kPrecisionTrails,  // only Precision's: these are left out
		kPrecisionChoices
	};
	inline constexpr std::array<const char*, kPrecisionChoices> kPrecisionChoiceNames{ "Both", "Attack Trails only", "Precision only" };

	struct Values
	{
		bool enabled{ true };
		bool normalAttacks{ false };  // ordinary (not power) attacks get trails too
		bool fists{ true };           // punches too, colored by the gauntlets' enchantment
		bool npcs{ true };            // NPCs' and creatures' power attacks near you
		float npcRange{ 2656.0f };
		int   npcMax{ 8 };

		// the look
		float opacity{ 0.64f };
		float brightness{ 2.3f };    // glow: above 1 blooms
		float intensity{ 1.97f };    // everything at once

		// the particles the blade leaves along its path
		bool  sparks{ true };
		float sparkAmount{ 2.2f };   // how many along the path
		float sparkSize{ 0.4f };
		float sparkLife{ 0.55f };    // how long they stay (about a second at 1)
		float sparkSpread{ 0.0f };   // how far they drift and fly off
		float flyingSparks{ 1.35f }; // sparks flying off the edge
		bool  line{ true };          // one thin line along each swing's edge

		std::array<ElementSettings, kElements> elements{ kDefaultElements };
		bool                                   glowColors{ true };  // the color the enchantment glows with on items, when it has a clear one
		std::vector<EnchantmentRule>           enchantments;        // enchantments with colors of their own (they win over the above)

		// which weapons
		int                     weaponFilter{ kAllWeapons };
		std::vector<WeaponRule> weapons{ kSuggestedWeapons };

		// arrows shot from a bow held drawn long enough
		bool  arrows{ true };
		float arrowHold{ 1.0f };  // seconds the bow is held fully drawn before the shot

		// Precision's trails: for weapons without an enchantment, and for enchanted ones (bound and ghost weapons and
		// listed weapons with a color of their own count as enchanted)
		int precisionPlain{ kBothTrails };
		int precisionEnchanted{ kBothTrails };

		// VR: your swings are your own motion (no attack animations): a fast enough swing leaves a trail
		bool  vrSwings{ true };
		float vrSwingSpeed{ 1000.0f };  // units/s of the weapon's tip against your body (fists: 60% of it)

		bool        debugLog{ false };
		std::string language{ "auto" };

		bool operator==(const Values&) const = default;
	};

	void Load();
	void Save();  // writes the file if anything changed since the last load/save

	Values&     Get();
	std::mutex& Lock();  // around reads from other threads and changes from the menu

	// bumped on every change, so the other modules can notice
	std::uint32_t Revision();
	void          Touch();
}
