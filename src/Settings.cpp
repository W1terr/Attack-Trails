#include "Settings.h"

#include <nlohmann/json.hpp>

namespace Settings
{
	namespace
	{
		using json = nlohmann::json;

		constexpr int kVersion = 7;

		constexpr std::array<const char*, kElements> kKeys{ "plain", "fire", "frost", "shock", "chaos", "health", "magicka", "stamina", "soulTrap",
			"paralysis", "fear", "turnUndead", "banish", "calm", "frenzy", "bound", "ghost", "other" };

		Values                     values;
		Values                     saved;
		std::mutex                 lock;
		std::atomic<std::uint32_t> revision{ 1 };

		// next to the plugin; under Mod Organizer 2 the file lands in its overwrite folder (no mod ships it, so updating
		// the mod never replaces it)
		std::filesystem::path Path() { return "Data/SKSE/Plugins/AttackTrails.json"; }

		// the test builds were called Power Attack Trails: their settings are taken over once
		std::filesystem::path OldPath() { return "Data/SKSE/Plugins/PowerAttackTrails.json"; }

		bool writeAnyway{ false };  // the next Save writes even if nothing changed (settings taken over from the old file)

		template <class T>
		void Read(const json& a_obj, const char* a_key, T& a_value)
		{
			if (const auto it = a_obj.find(a_key); it != a_obj.end()) {
				try {
					a_value = it->get<T>();
				} catch (const std::exception& e) {
					logs::warn("settings: '{}' ignored ({})", a_key, e.what());
				}
			}
		}

		constexpr std::array<const char*, kWeaponFilters> kFilterKeys{ "all", "unique", "listed" };

		std::uint32_t ParseId(const json& a_value)
		{
			if (a_value.is_number_unsigned()) {
				return a_value.get<std::uint32_t>();
			}
			if (a_value.is_string()) {
				return static_cast<std::uint32_t>(std::stoul(a_value.get<std::string>(), nullptr, 16));
			}
			throw std::invalid_argument("not a form id");
		}

		void ReadWeapons(const json& a_root, Values& a_values)
		{
			if (const auto it = a_root.find("weaponFilter"); it != a_root.end() && it->is_string()) {
				const auto found = std::ranges::find(kFilterKeys, it->get<std::string>());
				a_values.weaponFilter = found != kFilterKeys.end() ? static_cast<int>(found - kFilterKeys.begin()) : kAllWeapons;
			}
			const auto it = a_root.find("weapons");
			if (it == a_root.end() || !it->is_array()) {
				return;  // older file: the suggested list
			}
			a_values.weapons.clear();
			for (const auto& entry : *it) {
				try {
					WeaponRule rule;
					rule.plugin = entry.at("plugin").get<std::string>();
					rule.id = ParseId(entry.at("id")) & 0xFFFFFF;
					Read(entry, "name", rule.name);
					Read(entry, "trail", rule.trail);
					Read(entry, "ownColor", rule.ownColor);
					Read(entry, "color", rule.color);
					a_values.weapons.push_back(std::move(rule));
				} catch (const std::exception& e) {
					logs::warn("settings: a weapon of the list ignored ({})", e.what());
				}
			}
		}

		json WriteWeapons(const Values& a_values)
		{
			json list = json::array();
			for (const auto& rule : a_values.weapons) {
				list.push_back({ { "plugin", rule.plugin }, { "id", std::format("0x{:06X}", rule.id) }, { "name", rule.name }, { "trail", rule.trail },
					{ "ownColor", rule.ownColor }, { "color", rule.color } });
			}
			return list;
		}

		void ReadEnchantments(const json& a_root, Values& a_values)
		{
			const auto it = a_root.find("enchantments");
			if (it == a_root.end() || !it->is_array()) {
				return;
			}
			a_values.enchantments.clear();
			for (const auto& entry : *it) {
				try {
					EnchantmentRule rule;
					rule.plugin = entry.at("plugin").get<std::string>();
					rule.id = ParseId(entry.at("id")) & 0xFFFFFF;
					Read(entry, "name", rule.name);
					Read(entry, "trail", rule.trail);
					Read(entry, "color", rule.color);
					a_values.enchantments.push_back(std::move(rule));
				} catch (const std::exception& e) {
					logs::warn("settings: an enchantment of the list ignored ({})", e.what());
				}
			}
		}

		json WriteEnchantments(const Values& a_values)
		{
			json list = json::array();
			for (const auto& rule : a_values.enchantments) {
				list.push_back({ { "plugin", rule.plugin }, { "id", std::format("0x{:06X}", rule.id) }, { "name", rule.name }, { "trail", rule.trail },
					{ "color", rule.color } });
			}
			return list;
		}

		void Clamp(Values& a_values)
		{
			a_values.weaponFilter = std::clamp(a_values.weaponFilter, 0, kWeaponFilters - 1);
			for (auto& rule : a_values.weapons) {
				for (auto& c : rule.color) {
					c = std::clamp(c, 0.0f, 1.0f);
				}
			}
			for (auto& rule : a_values.enchantments) {
				for (auto& c : rule.color) {
					c = std::clamp(c, 0.0f, 1.0f);
				}
			}
			a_values.npcRange = std::clamp(a_values.npcRange, 256.0f, 8192.0f);
			a_values.npcMax = std::clamp(a_values.npcMax, 0, 20);
			a_values.opacity = std::clamp(a_values.opacity, 0.0f, 1.0f);
			a_values.brightness = std::clamp(a_values.brightness, 0.0f, 6.0f);
			a_values.intensity = std::clamp(a_values.intensity, 0.0f, 3.0f);
			a_values.sparkAmount = std::clamp(a_values.sparkAmount, 0.0f, 4.0f);
			a_values.sparkSize = std::clamp(a_values.sparkSize, 0.2f, 4.0f);
			a_values.sparkLife = std::clamp(a_values.sparkLife, 0.2f, 4.0f);
			a_values.sparkSpread = std::clamp(a_values.sparkSpread, 0.0f, 4.0f);
			a_values.flyingSparks = std::clamp(a_values.flyingSparks, 0.0f, 4.0f);
			a_values.arrowHold = std::clamp(a_values.arrowHold, 0.0f, 10.0f);
			a_values.vrSwingSpeed = std::clamp(a_values.vrSwingSpeed, 200.0f, 4000.0f);
			for (auto& e : a_values.elements) {
				for (auto& c : e.color) {
					c = std::clamp(c, 0.0f, 1.0f);
				}
			}
		}
	}

	void Load()
	{
		std::scoped_lock guard(lock);
		auto             path = Path();
		std::ifstream    file(path);
		if (!file && std::filesystem::exists(OldPath())) {
			path = OldPath();
			file.open(path);
			writeAnyway = true;
		}
		if (!file) {
			logs::info("no settings file at {}, using defaults", path.string());
			saved = values;
			return;
		}
		try {
			const auto root = json::parse(file);
			Read(root, "enabled", values.enabled);
			Read(root, "normalAttacks", values.normalAttacks);
			Read(root, "fists", values.fists);
			Read(root, "npcs", values.npcs);
			Read(root, "npcRange", values.npcRange);
			Read(root, "npcMax", values.npcMax);
			Read(root, "opacity", values.opacity);
			Read(root, "brightness", values.brightness);
			Read(root, "intensity", values.intensity);
			Read(root, "sparks", values.sparks);
			Read(root, "sparkAmount", values.sparkAmount);
			Read(root, "sparkSize", values.sparkSize);
			Read(root, "sparkLife", values.sparkLife);
			Read(root, "sparkSpread", values.sparkSpread);
			Read(root, "flyingSparks", values.flyingSparks);
			Read(root, "line", values.line);
			Read(root, "arrows", values.arrows);
			Read(root, "arrowHold", values.arrowHold);
			Read(root, "vrSwings", values.vrSwings);
			Read(root, "vrSwingSpeed", values.vrSwingSpeed);
			Read(root, "debugLog", values.debugLog);
			Read(root, "language", values.language);
			ReadWeapons(root, values);
			ReadEnchantments(root, values);
			Read(root, "glowColors", values.glowColors);
			if (const auto it = root.find("colors"); it != root.end() && it->is_object()) {
				for (std::size_t e = 0; e < kElements; ++e) {
					if (const auto entry = it->find(kKeys[e]); entry != it->end() && entry->is_object()) {
						Read(*entry, "enabled", values.elements[e].enabled);
						Read(*entry, "color", values.elements[e].color);
					}
				}
			}
			Clamp(values);
			logs::info("settings loaded from {}", path.string());
		} catch (const std::exception& e) {
			logs::error("settings file {} is broken ({}), using defaults", path.string(), e.what());
			values = {};
		}
		saved = values;
		if (writeAnyway) {
			Save();  // under the new name from now on
		}
	}

	void Save()
	{
		if (values == saved && !writeAnyway) {
			return;
		}
		writeAnyway = false;
		json colors = json::object();
		for (std::size_t e = 0; e < kElements; ++e) {
			colors[kKeys[e]] = { { "enabled", values.elements[e].enabled }, { "color", values.elements[e].color } };
		}
		const json root{
			{ "version", kVersion },
			{ "enabled", values.enabled },
			{ "normalAttacks", values.normalAttacks },
			{ "fists", values.fists },
			{ "npcs", values.npcs },
			{ "npcRange", values.npcRange },
			{ "npcMax", values.npcMax },
			{ "opacity", values.opacity },
			{ "brightness", values.brightness },
			{ "intensity", values.intensity },
			{ "sparks", values.sparks },
			{ "sparkAmount", values.sparkAmount },
			{ "sparkSize", values.sparkSize },
			{ "sparkLife", values.sparkLife },
			{ "sparkSpread", values.sparkSpread },
			{ "flyingSparks", values.flyingSparks },
			{ "line", values.line },
			{ "arrows", values.arrows },
			{ "arrowHold", values.arrowHold },
			{ "vrSwings", values.vrSwings },
			{ "vrSwingSpeed", values.vrSwingSpeed },
			{ "colors", colors },
			{ "glowColors", values.glowColors },
			{ "enchantments", WriteEnchantments(values) },
			{ "weaponFilter", kFilterKeys[static_cast<std::size_t>(values.weaponFilter)] },
			{ "weapons", WriteWeapons(values) },
			{ "debugLog", values.debugLog },
			{ "language", values.language },
		};
		const auto      path = Path();
		std::error_code error;
		std::filesystem::create_directories(path.parent_path(), error);
		std::ofstream file(path, std::ios::trunc);
		if (!file) {
			logs::error("can't write {}", path.string());
			return;
		}
		file << root.dump(2);
		saved = values;
		logs::info("settings saved");
	}

	Values&     Get() { return values; }
	std::mutex& Lock() { return lock; }

	std::uint32_t Revision() { return revision.load(); }
	void          Touch() { ++revision; }
}
