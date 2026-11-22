// SKSE Menu Framework 3 API (https://github.com/QTR-Modding/SKSE-Menu-Framework-3-API, LGPL-2.1).
// It pulls in windows.h, so only this file includes it.
#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
#pragma warning(push, 0)
#include "SKSEMenuFramework.h"
#pragma warning(pop)
#undef GetObject
#undef PlaySound

#include "Menu.h"

#include "Lang.h"
#include "Settings.h"
#include "Trails.h"

namespace Menu
{
	namespace
	{
		using namespace ImGuiMCP;
		using Lang::F;
		using Lang::T;
		using Settings::kElements;

		constexpr auto kSection = "Attack Trails";

		const ImVec4 kDim{ 0.6f, 0.6f, 0.6f, 1.0f };
		const ImVec4 kGood{ 0.55f, 0.9f, 0.5f, 1.0f };
		const ImVec4 kWarn{ 1.0f, 0.45f, 0.35f, 1.0f };

		// changes are written right away (at most twice a second while a slider moves) and when the menu closes
		bool                                  dirty{ false };
		std::chrono::steady_clock::time_point lastSave{};

		// translated label with a fixed ImGui id (it must not change with the language)
		std::string Id(const char* a_english, const char* a_suffix = "")
		{
			return std::format("{}###{}{}", T(a_english), a_english, a_suffix);
		}

		void Hint(const char* a_english)
		{
			SameLine();
			TextDisabled("(?)");
			if (IsItemHovered()) {
				SetTooltip("%s", T(a_english));
			}
		}

		void Heading(const char* a_english)
		{
			SeparatorText(T(a_english));
		}

		bool Changed(bool a_changed)
		{
			if (a_changed) {
				Settings::Touch();
				dirty = true;
			}
			return a_changed;
		}

		void SaveIfDue(bool a_now = false)
		{
			const auto clock = std::chrono::steady_clock::now();
			if (dirty && (a_now || clock - lastSave > 500ms)) {
				Settings::Save();
				dirty = false;
				lastSave = clock;
			}
		}

		void __stdcall RenderSettings();
		void __stdcall RenderColors();
		void __stdcall RenderWeapons();

		struct Page
		{
			const char*                              english;
			SKSEMenuFramework::Model::RenderFunction render;
			std::string                              shown;  // the name it was added or last renamed with
		};
		std::array pages{
			Page{ "Settings", RenderSettings, {} },
			Page{ "Colors", RenderColors, {} },
			Page{ "Weapons", RenderWeapons, {} },
		};

		// exported by SKSE Menu Framework 3.8+, not in its header: renames a page at runtime
		using RenameSectionFunction = bool (*)(const char* a_path, const char* a_newName);

		RenameSectionFunction RenameSection()
		{
			static const auto func = SKSEMenuFramework::Model::Internal::GetFunction<RenameSectionFunction>("RenameSection");
			return func;
		}

		void RenamePages()
		{
			if (!RenameSection()) {
				return;
			}
			for (auto& page : pages) {
				const std::string wanted = T(page.english);
				if (!page.shown.empty() && wanted != page.shown && RenameSection()(std::format("{}/{}", kSection, page.shown).c_str(), wanted.c_str())) {
					page.shown = wanted;
				}
			}
		}

		// the language list at the top of the Settings page
		void LanguagePicker(Settings::Values& a_s)
		{
			const char* shown = a_s.language == "auto" ? T("Same as the game") : Lang::Current().name;
			bool        changed = false;
			SetNextItemWidth(260.0f);
			if (BeginCombo(Id("Language").c_str(), shown)) {
				if (Selectable(Id("Same as the game").c_str(), a_s.language == "auto")) {
					a_s.language = "auto";
					changed = true;
				}
				for (const auto& language : Lang::All()) {
					if (Selectable(language.name, a_s.language == language.id)) {
						a_s.language = language.id;
						changed = true;
					}
				}
				EndCombo();
			}
			if (changed) {
				Changed(true);
				Lang::Set(a_s.language);
				RenamePages();
			}
			if (!RenameSection() && changed) {
				TextWrapped("%s", T("The names of the pages on the left change after you restart the game."));
			}
			// without the right glyphs the framework draws "?" for every letter, so this is also shown in English
			if (const auto glyphs = Lang::Current().glyphs) {
				constexpr auto kGlyphHint = "If you see ? instead of letters: turn on the {} font in SKSE Menu Framework's settings (Fonts).";
				TextWrapped("%s", F(kGlyphHint, glyphs).c_str());
				TextDisabled("%s", std::vformat(kGlyphHint, std::make_format_args(glyphs)).c_str());
			}
		}

		void __stdcall RenderSettings()
		{
			std::scoped_lock guard(Settings::Lock());
			auto&            s = Settings::Get();

			LanguagePicker(s);
			Spacing();

			// three switches of their own: weapons, fists, arrows (Arrows below)
			Changed(Checkbox(Id("Weapon trails").c_str(), &s.enabled));
			Hint("Trails behind your weapon when you power attack, colored by its enchantment. Fists and arrows have switches of their own.");
			Changed(Checkbox(Id("Fists").c_str(), &s.fists));
			Hint("Punches leave trails, from the knuckles, colored by the enchantment of the gauntlets you wear (white without one). On or off whatever the weapon trails are.");
			const bool melee = s.enabled || s.fists;
			if (!melee) {
				BeginDisabled();
			}
			Changed(Checkbox(Id("Normal attacks too").c_str(), &s.normalAttacks));
			Hint("Off: only power attacks leave trails. On: every attack does.");
			if (!melee) {
				EndDisabled();
			}
			Changed(Checkbox(Id("NPCs and creatures").c_str(), &s.npcs));
			Hint("The power attacks and arrows of NPCs and creatures near you leave trails too.");
			if (!s.npcs) {
				BeginDisabled();
			}
			Changed(SliderFloat(Id("Within").c_str(), &s.npcRange, 512.0f, 8192.0f, "%.0f"));
			Changed(SliderInt(Id("At most").c_str(), &s.npcMax, 1, 20));
			Hint("The nearest this many NPCs with their weapons out are watched.");
			if (!s.npcs) {
				EndDisabled();
			}

			Heading("Look");
			Changed(SliderFloat(Id("Opacity").c_str(), &s.opacity, 0.0f, 1.0f, "%.2f"));
			Changed(SliderFloat(Id("Brightness").c_str(), &s.brightness, 0.0f, 4.0f, "%.2f"));
			Hint("Above 1 the trail glows (with bloom it shines).");
			Changed(SliderFloat(Id("Overall intensity").c_str(), &s.intensity, 0.0f, 2.0f, "%.2fx"));
			Hint("The whole effect at once.");

			Heading("Particles");
			Changed(Checkbox(Id("Particle trail").c_str(), &s.sparks));
			Hint("The weapon leaves glowing dashes and dots along its path, from the part that strikes: a sword's outer blade, an axe's, mace's or hammer's head, packed along its leading edge. Every swing leaves its own trail: they stay where the weapon passed, drift a little and burn out one by one, so the arc breaks up as it fades.");
			if (!s.sparks) {
				BeginDisabled();
			}
			Changed(SliderFloat(Id("Amount").c_str(), &s.sparkAmount, 0.1f, 3.0f, "%.2fx"));
			Hint("How many particles the swing leaves. More: denser, more solid lines.");
			Changed(SliderFloat(Id("Particle size").c_str(), &s.sparkSize, 0.3f, 3.0f, "%.2fx"));
			Changed(SliderFloat(Id("Duration").c_str(), &s.sparkLife, 0.3f, 3.0f, "%.2fx"));
			Hint("How long the particles stay before they burn out (about a second at 1x).");
			Changed(SliderFloat(Id("Spread").c_str(), &s.sparkSpread, 0.0f, 3.0f, "%.2fx"));
			Hint("How far the particles drift outward and the sparks fly. 0: everything stays on the swing's path.");
			Changed(SliderFloat(Id("Flying sparks").c_str(), &s.flyingSparks, 0.0f, 3.0f, "%.2fx"));
			Hint("Sparks thrown off the edge: they fly off with the blade, slow down, fall and burn out quickly.");
			if (!s.sparks) {
				EndDisabled();
			}
			Changed(Checkbox(Id("Thin line").c_str(), &s.line));
			Hint("One thin glowing line along the path of the weapon's edge, under the particles. It fades with them.");

			Heading("Arrows");
			Changed(Checkbox(Id("Arrow trails").c_str(), &s.arrows));
			Hint("An arrow shot from a bow held fully drawn long enough leaves a trail behind it, in the bow's color (its enchantment's, or its own from the Weapons page). It ends where the arrow hits.");
			if (!s.arrows) {
				BeginDisabled();
			}
			Changed(SliderFloat(Id("Hold the bow drawn for").c_str(), &s.arrowHold, 0.0f, 5.0f, "%.1f s"));
			Hint("How long the bow has to stay fully drawn before the shot for its arrow to leave a trail. 0: every arrow.");
			if (!s.arrows) {
				EndDisabled();
			}

			if (REL::Module::IsVR()) {
				Heading("VR");
				Changed(Checkbox(Id("Every fast swing").c_str(), &s.vrSwings));
				Hint("In VR you swing the weapon yourself and no attack animation plays: every swing fast enough leaves a trail, each hand on its own. Off: only what the game itself counts as a power attack.");
				if (!s.vrSwings) {
					BeginDisabled();
				}
				Changed(SliderFloat(Id("Swing speed").c_str(), &s.vrSwingSpeed, 200.0f, 4000.0f, "%.0f"));
				Hint("How fast the weapon's tip has to move (against your body) for a trail. Lower: gentler swings leave trails too. Fists need 60% of it.");
				if (!s.vrSwings) {
					EndDisabled();
				}
			}

			Spacing();
			Changed(Checkbox(Id("Debug log").c_str(), &s.debugLog));
			Hint("Writes every attack, power attack, bow shot and its trail to AttackTrails.log.");
			SaveIfDue();
		}

		// ---- the Weapons page

		bool HasTrails(const RE::TESObjectWEAP* a_weapon)
		{
			switch (a_weapon->GetWeaponType()) {
			case RE::WEAPON_TYPE::kBow:
			case RE::WEAPON_TYPE::kOneHandSword:
			case RE::WEAPON_TYPE::kOneHandDagger:
			case RE::WEAPON_TYPE::kOneHandAxe:
			case RE::WEAPON_TYPE::kOneHandMace:
			case RE::WEAPON_TYPE::kTwoHandSword:
			case RE::WEAPON_TYPE::kTwoHandAxe:
				return true;
			default:
				return false;
			}
		}

		struct FoundWeapon
		{
			std::string   name;
			std::string   plugin;
			std::uint32_t id{ 0 };
		};

		std::string Lower(std::string_view a_text)
		{
			if (a_text.empty()) {
				return {};
			}
			const int    size = static_cast<int>(a_text.size());
			std::wstring wide(static_cast<std::size_t>(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, a_text.data(), size, nullptr, 0)), L'\0');
			if (wide.empty()) {
				std::string out(a_text);  // not UTF-8: A-Z only
				std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				return out;
			}
			MultiByteToWideChar(CP_UTF8, 0, a_text.data(), size, wide.data(), static_cast<int>(wide.size()));
			CharLowerBuffW(wide.data(), static_cast<DWORD>(wide.size()));
			std::string out(static_cast<std::size_t>(WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr)), '\0');
			WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), static_cast<int>(out.size()), nullptr, nullptr);
			return out;
		}

		// the plugin a form (weapon, magic effect) comes from and its id there
		bool Identify(const RE::TESForm* a_form, std::string& a_plugin, std::uint32_t& a_id)
		{
			const auto file = a_form ? a_form->GetFile(0) : nullptr;
			if (!file) {
				return false;  // made in game
			}
			a_plugin = file->GetFilename();
			a_id = file->IsLight() ? a_form->GetFormID() & 0xFFF : a_form->GetFormID() & 0xFFFFFF;
			return true;
		}

		int RuleIndex(const Settings::Values& a_s, std::string_view a_plugin, std::uint32_t a_id)
		{
			const std::string plugin(a_plugin);
			for (std::size_t i = 0; i < a_s.weapons.size(); ++i) {
				const auto& rule = a_s.weapons[i];
				if (rule.id == a_id && _stricmp(rule.plugin.c_str(), plugin.c_str()) == 0) {
					return static_cast<int>(i);
				}
			}
			return -1;
		}

		void AddRule(Settings::Values& a_s, const std::string& a_plugin, std::uint32_t a_id, const std::string& a_name, const std::array<float, 3>& a_color)
		{
			if (a_plugin.empty() || RuleIndex(a_s, a_plugin, a_id) >= 0) {
				return;
			}
			Settings::WeaponRule rule;
			rule.plugin = a_plugin;
			rule.id = a_id;
			rule.name = a_name;
			rule.color = a_color;
			a_s.weapons.push_back(std::move(rule));
			Changed(true);
		}

		// loaded melee weapons and bows whose names hold the text (at most 40)
		std::vector<FoundWeapon> Search(const std::string& a_text)
		{
			std::vector<FoundWeapon> found;
			const auto               data = RE::TESDataHandler::GetSingleton();
			if (!data || a_text.size() < 2) {
				return found;
			}
			const auto wanted = Lower(a_text);
			for (const auto weapon : data->GetFormArray<RE::TESObjectWEAP>()) {
				if (!weapon || !weapon->GetPlayable() || !HasTrails(weapon)) {
					continue;
				}
				const std::string_view name = weapon->GetName();
				if (name.empty() || Lower(name).find(wanted) == std::string::npos) {
					continue;
				}
				FoundWeapon f;
				f.name = name;
				if (!Identify(weapon, f.plugin, f.id)) {
					continue;
				}
				found.push_back(std::move(f));
				if (found.size() >= 40) {
					break;
				}
			}
			std::ranges::sort(found, {}, &FoundWeapon::name);
			return found;
		}

		// ---- the Colors page

		struct FoundEffect
		{
			std::string           name;
			std::string           plugin;
			std::uint32_t         id{ 0 };
			const RE::EffectSetting* effect{ nullptr };
		};

		int EnchantmentIndex(const Settings::Values& a_s, std::string_view a_plugin, std::uint32_t a_id)
		{
			const std::string plugin(a_plugin);
			for (std::size_t i = 0; i < a_s.enchantments.size(); ++i) {
				const auto& rule = a_s.enchantments[i];
				if (rule.id == a_id && _stricmp(rule.plugin.c_str(), plugin.c_str()) == 0) {
					return static_cast<int>(i);
				}
			}
			return -1;
		}

		// a new entry starts with the color the trail has now: its glow, else its kind's
		std::array<float, 3> SuggestedColor(const Settings::Values& a_s, const RE::EffectSetting* a_effect)
		{
			std::array<float, 3> color{ 1.0f, 1.0f, 1.0f };
			if (a_s.glowColors && Trails::GlowColor(a_effect, color)) {
				return color;
			}
			return a_s.elements[Trails::ElementOfEffect(a_effect)].color;
		}

		void AddEnchantment(Settings::Values& a_s, const std::string& a_plugin, std::uint32_t a_id, const std::string& a_name)
		{
			if (a_plugin.empty() || EnchantmentIndex(a_s, a_plugin, a_id) >= 0) {
				return;
			}
			const auto data = RE::TESDataHandler::GetSingleton();
			const auto effect = data ? data->LookupForm<RE::EffectSetting>(a_id, a_plugin) : nullptr;
			Settings::EnchantmentRule rule;
			rule.plugin = a_plugin;
			rule.id = a_id;
			rule.name = a_name;
			rule.color = SuggestedColor(a_s, effect);
			a_s.enchantments.push_back(std::move(rule));
			Changed(true);
		}

		// the magic effects enchantments are made of (every loaded plugin's), whose names hold the text (at most 40)
		std::vector<FoundEffect> SearchEffects(const std::string& a_text)
		{
			static std::vector<const RE::EffectSetting*> effects;  // gathered once
			std::vector<FoundEffect>                     found;
			const auto                                   data = RE::TESDataHandler::GetSingleton();
			if (!data || a_text.size() < 2) {
				return found;
			}
			if (effects.empty()) {
				std::unordered_set<const RE::EffectSetting*> seen;
				for (const auto enchantment : data->GetFormArray<RE::EnchantmentItem>()) {
					if (!enchantment) {
						continue;
					}
					for (const auto effect : enchantment->effects) {
						const auto mgef = effect ? effect->baseEffect : nullptr;
						if (mgef && mgef->GetName()[0] && seen.insert(mgef).second) {
							effects.push_back(mgef);
						}
					}
				}
			}
			const auto wanted = Lower(a_text);
			for (const auto effect : effects) {
				const std::string_view name = effect->GetName();
				if (Lower(name).find(wanted) == std::string::npos) {
					continue;
				}
				FoundEffect f;
				f.name = name;
				f.effect = effect;
				if (!Identify(effect, f.plugin, f.id)) {
					continue;
				}
				found.push_back(std::move(f));
				if (found.size() >= 40) {
					break;
				}
			}
			std::ranges::sort(found, {}, &FoundEffect::name);
			return found;
		}

		void __stdcall RenderColors()
		{
			std::scoped_lock                guard(Settings::Lock());
			auto&                           s = Settings::Get();
			static char                     search[64]{};
			static std::string              searched;
			static std::vector<FoundEffect> results;

			TextWrapped("%s", T("A trail takes the color of its weapon's enchantment. Switch a kind off to give those weapons no trail. Single weapons can have colors of their own on the Weapons page."));
			Spacing();
			Changed(Checkbox(Id("Glow color of the enchantment").c_str(), &s.glowColors));
			Hint("A trail takes the color its enchantment glows with on the item in your inventory: Fortify Health red, Fortify Stamina green, Fortify Magicka blue... Enchantments whose glow has no clear color (fire, frost, shock and some others) take the color of their kind below. Bound and ghost weapons always take theirs.");
			Spacing();
			constexpr std::array<const char*, kElements> kHints{
				"Weapons without an enchantment.",
				"Fire damage enchantments.",
				"Frost damage enchantments.",
				"Shock damage enchantments.",
				"Fire, frost and shock damage together (chaos damage).",
				"Enchantments that drain or absorb health.",
				"Enchantments that drain or absorb magicka.",
				"Enchantments that drain or absorb stamina.",
				"Soul trap enchantments.",
				"Paralysis enchantments.",
				"Fear enchantments.",
				"Turn undead enchantments.",
				"Banish enchantments (they send summoned daedra back).",
				"Calm enchantments.",
				"Frenzy enchantments.",
				"Conjured weapons (Bound Sword, Bound Battleaxe, Bound Bow...), whatever their enchantment.",
				"The weapons ghosts fight with, and the Ghostblade.",
				"Every other enchantment.",
			};
			if (BeginTable("colors", 3, ImGuiTableFlags_SizingStretchProp)) {
				for (std::size_t e = 0; e < kElements; ++e) {
					auto& el = s.elements[e];
					PushID(static_cast<int>(e));
					TableNextRow();
					TableNextColumn();
					Changed(Checkbox(T(Settings::kElementNames[e]), &el.enabled));
					Hint(kHints[e]);
					TableNextColumn();
					if (!el.enabled) {
						BeginDisabled();
					}
					Changed(ColorEdit3("##color", el.color.data(), ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_PickerHueWheel));
					if (!el.enabled) {
						EndDisabled();
					}
					TableNextColumn();
					if (el.color != Settings::kDefaultElements[e].color && SmallButton(T("Default"))) {
						el.color = Settings::kDefaultElements[e].color;
						Changed(true);
					}
					PopID();
				}
				EndTable();
			}

			Heading("Enchantment list");
			Hint("Enchantments with a color of their own, from any mod: every item enchanted with one of them (by the game, a mod or yourself) takes its color. These win over the colors above; only the Weapons page's own colors win over them. Trail off: those items leave no trail.");
			const auto data = RE::TESDataHandler::GetSingleton();
			if (s.enchantments.empty()) {
				TextColored(kDim, "%s", T("(none)"));
			} else if (BeginTable("enchantments", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
				TableSetupColumn(T("Trail"), ImGuiTableColumnFlags_WidthFixed);
				TableSetupColumn(T("Enchantment"), ImGuiTableColumnFlags_WidthStretch);
				TableSetupColumn(T("Color"), ImGuiTableColumnFlags_WidthFixed);
				TableSetupColumn("##remove", ImGuiTableColumnFlags_WidthFixed);
				TableHeadersRow();
				for (std::size_t i = 0; i < s.enchantments.size(); ++i) {
					auto& rule = s.enchantments[i];
					PushID(static_cast<int>(i) + 2000);
					TableNextRow();
					TableNextColumn();
					Changed(Checkbox("##trail", &rule.trail));
					TableNextColumn();
					const auto  effect = data ? data->LookupForm<RE::EffectSetting>(rule.id, rule.plugin) : nullptr;
					const char* name = effect && effect->GetName()[0] ? effect->GetName() : rule.name.c_str();
					if (rule.trail) {
						Text("%s", name);
					} else {
						TextDisabled("%s", name);
					}
					SameLine();
					if (effect) {
						TextDisabled("%s", std::format("{} {:06X}", rule.plugin, rule.id).c_str());
					} else {
						TextColored(kWarn, "%s", F("{} not loaded", rule.plugin).c_str());
					}
					TableNextColumn();
					if (!rule.trail) {
						BeginDisabled();
					}
					Changed(ColorEdit3("##color", rule.color.data(), ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_PickerHueWheel));
					if (!rule.trail) {
						EndDisabled();
					}
					TableNextColumn();
					const bool removed = SmallButton("x");
					PopID();
					if (removed) {
						s.enchantments.erase(s.enchantments.begin() + static_cast<std::ptrdiff_t>(i));
						Changed(true);
						break;
					}
				}
				EndTable();
			}

			Heading("In your hands");
			const auto                           st = Trails::GetStatus();
			constexpr std::array<const char*, 2> kHands{ "Right hand:", "Left hand:" };
			bool                                 any = false;
			for (std::size_t k = 0; k < 2; ++k) {
				const auto& h = st.hands[k];
				if (!h.weapon || !h.effectId) {
					continue;
				}
				any = true;
				PushID(static_cast<int>(k) + 3000);
				Text("%s", T(kHands[k]));
				SameLine();
				ColorButton("##now", ImVec4{ h.color[0], h.color[1], h.color[2], 1.0f }, ImGuiColorEditFlags_NoTooltip);
				SameLine();
				Text("%s", h.effectName.c_str());
				SameLine();
				TextDisabled("%s", h.fist ? (h.name.empty() ? T("Fists") : h.name.c_str()) : h.name.c_str());
				SameLine();
				if (EnchantmentIndex(s, h.effectPlugin, h.effectId) >= 0) {
					TextDisabled("%s", T("in the list"));
				} else if (!h.effectPlugin.empty() && SmallButton(T("Add to the list"))) {
					AddEnchantment(s, h.effectPlugin, h.effectId, h.effectName);
				}
				PopID();
			}
			if (!any) {
				TextColored(kDim, "%s", T("Draw an enchanted weapon (or raise your fists in enchanted gauntlets) to add its enchantment from here."));
			}

			Heading("Find an enchantment");
			SetNextItemWidth(260.0f);
			InputTextWithHint("##searchEnchantment", T("part of its name"), search, sizeof(search));
			if (searched != search) {
				searched = search;
				results = SearchEffects(searched);
			}
			if (searched.size() >= 2 && results.empty()) {
				TextColored(kDim, "%s", T("no enchantment has that in its name"));
			}
			for (std::size_t i = 0; i < results.size(); ++i) {
				const auto& f = results[i];
				PushID(static_cast<int>(i) + 4000);
				if (EnchantmentIndex(s, f.plugin, f.id) >= 0) {
					TextDisabled("%s", T("in the list"));
				} else if (SmallButton(T("Add"))) {
					AddEnchantment(s, f.plugin, f.id, f.name);
				}
				SameLine();
				const auto c = SuggestedColor(s, f.effect);
				ColorButton("##suggested", ImVec4{ c[0], c[1], c[2], 1.0f }, ImGuiColorEditFlags_NoTooltip);
				SameLine();
				Text("%s", f.name.c_str());
				SameLine();
				TextDisabled("%s", std::format("{} {:06X}", f.plugin, f.id).c_str());
				PopID();
			}
			SaveIfDue();
		}

		void __stdcall RenderWeapons()
		{
			std::scoped_lock                guard(Settings::Lock());
			auto&                           s = Settings::Get();
			static char                     search[64]{};
			static std::string              searched;
			static std::vector<FoundWeapon> results;

			TextWrapped("%s", T("Choose which weapons leave trails, and give single weapons a color of their own. A weapon's own color takes the place of its enchantment's color."));

			Heading("Weapons with trails");
			constexpr std::array<const char*, Settings::kWeaponFilters> kFilterHints{
				"Every melee weapon and bow (each enchantment kind can still be switched off on the Colors page).",
				"Daedric artifacts and weapons made with an enchantment of their own: Dawnbreaker, Chillrend, Ghostblade, Grimsever... Not the enchanted copies of ordinary weapons (an Ebony Sword of Burning) and not weapons you enchanted yourself. Unique weapons without an enchantment (Wuuthrad, the Longhammer) aren't recognized: add them to the list.",
				"Only the weapons in the list below with their trail on.",
			};
			for (int f = 0; f < Settings::kWeaponFilters; ++f) {
				Changed(RadioButton(Id(Settings::kWeaponFilterNames[f]).c_str(), &s.weaponFilter, f));
				Hint(kFilterHints[f]);
			}

			Heading("In your hands");
			const auto                           st = Trails::GetStatus();
			constexpr std::array<const char*, 2> kHands{ "Right hand:", "Left hand:" };
			for (std::size_t k = 0; k < 2; ++k) {
				const auto& h = st.hands[k];
				PushID(static_cast<int>(k));
				Text("%s", T(kHands[k]));
				SameLine();
				if (!h.weapon) {
					TextColored(kDim, "%s", T("no weapon drawn"));
					PopID();
					continue;
				}
				ColorButton("##now", ImVec4{ h.color[0], h.color[1], h.color[2], 1.0f }, ImGuiColorEditFlags_NoTooltip);
				SameLine();
				Text("%s", h.fist ? T("Fists") : h.name.c_str());
				SameLine();
				if (h.fist) {
					TextDisabled("%s", h.name.empty() ? T("no gauntlets") : h.name.c_str());
				} else {
					TextDisabled("%s", T(h.unique ? "unique" : "ordinary"));
				}
				SameLine();
				if (h.trailOn) {
					TextColored(kGood, "%s", T("trail"));
				} else {
					TextColored(kWarn, "%s", T("no trail"));
				}
				if (h.fist) {
					PopID();
					continue;  // not a weapon: nothing to add to the list
				}
				SameLine();
				if (RuleIndex(s, h.plugin, h.id) >= 0) {
					TextDisabled("%s", T("in the list"));
				} else if (!h.plugin.empty() && SmallButton(T("Add to the list"))) {
					AddRule(s, h.plugin, h.id, h.name, h.color);
				}
				PopID();
			}
			if (!st.hands[0].weapon && !st.hands[1].weapon) {
				TextColored(kDim, "%s", T("Draw a weapon to add it from here."));
			}

			Heading("Weapon list");
			Hint("Trail on: the weapon always leaves trails, whatever is chosen above. Trail off: it never does. Own color: its trails take this color instead of its enchantment's.");
			const auto data = RE::TESDataHandler::GetSingleton();
			if (s.weapons.empty()) {
				TextColored(kDim, "%s", T("(none)"));
			} else if (BeginTable("weapons", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
				TableSetupColumn(T("Trail"), ImGuiTableColumnFlags_WidthFixed);
				TableSetupColumn(T("Weapon"), ImGuiTableColumnFlags_WidthStretch);
				TableSetupColumn(T("Own color"), ImGuiTableColumnFlags_WidthFixed);
				TableSetupColumn("##remove", ImGuiTableColumnFlags_WidthFixed);
				TableHeadersRow();
				for (std::size_t i = 0; i < s.weapons.size(); ++i) {
					auto& rule = s.weapons[i];
					PushID(static_cast<int>(i));
					TableNextRow();
					TableNextColumn();
					Changed(Checkbox("##trail", &rule.trail));
					TableNextColumn();
					const auto  weapon = data ? data->LookupForm<RE::TESObjectWEAP>(rule.id, rule.plugin) : nullptr;
					const char* name = weapon && weapon->GetName()[0] ? weapon->GetName() : rule.name.c_str();
					if (rule.trail) {
						Text("%s", name);
					} else {
						TextDisabled("%s", name);
					}
					SameLine();
					if (weapon) {
						TextDisabled("%s", std::format("{} {:06X}", rule.plugin, rule.id).c_str());
					} else {
						TextColored(kWarn, "%s", F("{} not loaded", rule.plugin).c_str());
					}
					TableNextColumn();
					if (!rule.trail) {
						BeginDisabled();
					}
					Changed(Checkbox("##own", &rule.ownColor));
					SameLine();
					if (!rule.ownColor) {
						BeginDisabled();
					}
					Changed(ColorEdit3("##color", rule.color.data(), ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel));
					if (!rule.ownColor) {
						EndDisabled();
					}
					if (!rule.trail) {
						EndDisabled();
					}
					TableNextColumn();
					const bool removed = SmallButton("x");
					PopID();
					if (removed) {
						s.weapons.erase(s.weapons.begin() + static_cast<std::ptrdiff_t>(i));
						Changed(true);
						break;
					}
				}
				EndTable();
			}
			if (Button(T("Add the suggested colors"))) {
				for (const auto& rule : Settings::kSuggestedWeapons) {
					if (RuleIndex(s, rule.plugin, rule.id) < 0) {
						s.weapons.push_back(rule);
						Changed(true);
					}
				}
			}
			Hint("Puts back the colors the mod comes with, for the weapons missing from the list: Dawnbreaker gold, the Ebony Blade violet, Mehrunes' Razor and the Bloodskal Blade red, the Mace of Molag Bal purple, Volendrung amber, Harkon's Sword dark red.");

			Heading("Find a weapon");
			SetNextItemWidth(260.0f);
			InputTextWithHint("##search", T("part of its name"), search, sizeof(search));
			if (searched != search) {
				searched = search;
				results = Search(searched);
			}
			if (searched.size() >= 2 && results.empty()) {
				TextColored(kDim, "%s", T("no melee weapon or bow has that in its name"));
			}
			for (std::size_t i = 0; i < results.size(); ++i) {
				const auto& f = results[i];
				PushID(static_cast<int>(i) + 1000);
				if (RuleIndex(s, f.plugin, f.id) >= 0) {
					TextDisabled("%s", T("in the list"));
				} else if (SmallButton(T("Add"))) {
					AddRule(s, f.plugin, f.id, f.name, { 1.0f, 1.0f, 1.0f });
				}
				SameLine();
				Text("%s", f.name.c_str());
				SameLine();
				TextDisabled("%s", std::format("{} {:06X}", f.plugin, f.id).c_str());
				PopID();
			}
			SaveIfDue();
		}

		void __stdcall OnFrameworkEvent(SKSEMenuFramework::Model::EventType a_type)
		{
			if (a_type == SKSEMenuFramework::Model::EventType::kCloseMenu) {
				std::scoped_lock guard(Settings::Lock());
				dirty = true;
				SaveIfDue(true);
			}
		}
	}

	void Register()
	{
		if (!SKSEMenuFramework::IsInstalled() || !GetMenuFrameworkModule()) {
			logs::info("SKSE Menu Framework is not installed: no settings menu (the settings file still works)");
			return;
		}
		SKSEMenuFramework::SetSection(kSection);
		for (auto& page : pages) {
			page.shown = T(page.english);
			SKSEMenuFramework::AddSectionItem(page.shown.c_str(), page.render);
		}
		SKSEMenuFramework::AddEvent(OnFrameworkEvent, 0.0f);
		logs::info("SKSE Menu Framework {} found, pages added", SKSEMenuFramework::GetMenuFrameworkVersion());
	}
}
