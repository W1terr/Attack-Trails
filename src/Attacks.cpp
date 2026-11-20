#include "Attacks.h"

namespace Attacks
{
	namespace
	{
		enum class Class : std::uint8_t
		{
			kNone,
			kPowerStart,   // a power attack starts
			kAttackStart,  // an attack starts (power or not)
			kSwing,        // weaponSwing / weaponLeftSwing: the blade starts its swing
			kPreHit,       // preHitFrame: shortly before the hit
			kHit,          // HitFrame: the hit
			kStop,         // the attack ends or is interrupted
			kPrecision,    // Collision_AttackStart / Collision_Start: the animation times its own hits (Precision annotations)
			kCollisionAdd,     // Collision_Add: a hit window opens
			kCollisionRemove,  // Collision_Remove / Collision_AttackEnd / Collision_Cancel: it closes
			kClipEdge,         // a framework's attack clip begins or ends (MCO / BFCO: its attack speed set at the start, recovery)
		};

		std::mutex                              lock;  // the queue
		std::unordered_map<RE::FormID, Signals> queued;

		char Lower(char a_c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(a_c))); }

		std::string Lowered(std::string_view a_text)
		{
			std::string out(a_text);
			std::ranges::transform(out, out.begin(), Lower);
			return out;
		}

		bool Same(std::string_view a_a, std::string_view a_b)
		{
			return a_a.size() == a_b.size() && std::ranges::equal(a_a, a_b, [](char a, char b) { return Lower(a) == Lower(b); });
		}

		bool StartsWith(std::string_view a_text, std::string_view a_prefix)
		{
			return a_text.size() >= a_prefix.size() && Same(a_text.substr(0, a_prefix.size()), a_prefix);
		}

		bool Has(const std::string& a_lower, std::initializer_list<const char*> a_words)
		{
			return std::ranges::any_of(a_words, [&](const char* a_word) { return a_lower.find(a_word) != std::string::npos; });
		}

		// words that make an event an end, an exit or a recovery rather than a start
		bool IsEnding(const std::string& a_lower)
		{
			return Has(a_lower, { "stop", "end", "exit", "cancel", "out", "recover", "finish", "interrupt" });
		}

		// the text in "Name(...)" of a Precision payload ("Node(WEAPON)|Duration(0.3)|..."), lower case
		std::string Parameter(const std::string& a_lowerPayload, std::string_view a_name)
		{
			const auto at = a_lowerPayload.find(std::string(a_name) + "(");
			if (at == std::string::npos) {
				return {};
			}
			const auto from = at + a_name.size() + 1;
			const auto to = a_lowerPayload.find(')', from);
			return a_lowerPayload.substr(from, to == std::string::npos ? std::string::npos : to - from);
		}

		Class Classify(std::string_view a_tag, std::string_view a_payload)
		{
			// MCO / BFCO attack clips set their attack speed and next attacks when they begin, and end with a recovery: the
			// marks between one attack and the next (two strikes inside one clip are one attack)
			if (Same(a_tag, "PIE") && !a_payload.empty()) {
				const auto payload = Lowered(a_payload);
				if (Has(payload, { "attackspeed", "nextattack", "nextpowerattack" })) {
					return Class::kClipEdge;
				}
			}
			if (Same(a_tag, "weaponSwing") || Same(a_tag, "weaponLeftSwing")) {
				return Class::kSwing;
			}
			if (Same(a_tag, "preHitFrame")) {
				return Class::kPreHit;
			}
			if (Same(a_tag, "HitFrame")) {
				return Class::kHit;
			}
			// Precision's annotations (MCO and newer animation packs carry them): the exact hit windows
			if (Same(a_tag, "Collision_AttackStart") || Same(a_tag, "Collision_Start")) {
				return Class::kPrecision;
			}
			if (Same(a_tag, "Collision_Add")) {
				return Class::kCollisionAdd;
			}
			if (Same(a_tag, "Collision_Remove") || Same(a_tag, "Collision_AttackEnd") || Same(a_tag, "Collision_Cancel")) {
				return Class::kCollisionRemove;
			}
			if (Same(a_tag, "attackStop") || Same(a_tag, "staggerStart") || Same(a_tag, "recoilStart") || Same(a_tag, "recoilLargeStart") ||
				Same(a_tag, "blockStart") || Same(a_tag, "weaponSheathe") || Same(a_tag, "bleedOutStart") || Same(a_tag, "Ragdoll")) {
				return Class::kStop;
			}
			// vanilla power attacks: attackPowerStartForward, ...InPlace, ...Left, ...Right, ...Backward, _Sprint,
			// ...DualWield, ...LeftHand
			if (StartsWith(a_tag, "attackPowerStart")) {
				return Class::kPowerStart;
			}
			const auto lower = Lowered(a_tag);
			if (Has(lower, { "recovery" })) {
				return Class::kClipEdge;  // MCO_Recovery, BFCO_Recovery...
			}
			// frameworks name their own: "MCO_PowerAttack...", "BFCO_PowerAttack...", "BFCO_NextIsPowerAttack",
			// "PowerAttack_Start...", "SCAR_AttackPower...", heavy attacks of For Honor style combat; ends and exits don't
			// start one
			if (Has(lower, { "powerattack", "power_attack", "attackpower", "attack_power", "pwrattack", "powerstrike", "heavyattack", "heavy_attack" })) {
				return IsEnding(lower) ? Class::kNone : Class::kPowerStart;
			}
			if (StartsWith(a_tag, "attackStart") || (Has(lower, { "attackstart", "attack_start", "attackinitiate" }) && !IsEnding(lower))) {
				return Class::kAttackStart;
			}
			return Class::kNone;
		}

		class EventSink : public RE::BSTEventSink<RE::BSAnimationGraphEvent>
		{
		public:
			static EventSink* GetSingleton()
			{
				static EventSink sink;
				return &sink;
			}

			RE::BSEventNotifyControl ProcessEvent(const RE::BSAnimationGraphEvent* a_event, RE::BSTEventSource<RE::BSAnimationGraphEvent>*) override
			{
				if (!a_event || !a_event->holder || !a_event->tag.c_str()) {
					return RE::BSEventNotifyControl::kContinue;
				}
				const std::string_view tag(a_event->tag.c_str());
				const auto             kind = Classify(tag, a_event->payload.c_str() ? std::string_view(a_event->payload.c_str()) : std::string_view{});
				if (kind == Class::kNone) {
					return RE::BSEventNotifyControl::kContinue;
				}
				std::scoped_lock guard(lock);
				auto&            s = queued[a_event->holder->GetFormID()];
				switch (kind) {
				case Class::kPowerStart:
					s.powerStart = true;
					s.attackStart = true;
					break;
				case Class::kAttackStart:
					s.attackStart = true;
					break;
				case Class::kSwing:
					s.swing = true;
					(Same(tag, "weaponLeftSwing") ? s.swingLeft : s.swingRight) = true;
					break;
				case Class::kPreHit:
					s.preHit = true;
					break;
				case Class::kHit:
					s.hit = true;
					break;
				case Class::kStop:
					s.stop = true;
					break;
				case Class::kPrecision:
					s.precision = true;
					break;
				case Class::kCollisionAdd:
					{
						// the node the hit window is on: the right weapon (or fist), the left one (SHIELD), else a kick or a bash
						const auto payload = Lowered(a_event->payload.c_str() ? a_event->payload.c_str() : "");
						const auto node = Parameter(payload, "node");
						const auto has = [&](const char* a_part) { return node.find(a_part) != std::string::npos; };
						const std::uint8_t hands = node.empty() || node == "weapon" || has("[rhnd]") ? 1 : node == "shield" || has("[lhnd]") ? 2 : 0;
						s.precision = true;
						s.collisionAdd |= hands;
						if (const auto duration = Parameter(payload, "duration"); !duration.empty()) {
							try {
								s.collisionDuration = std::max(s.collisionDuration, std::stof(duration));
							} catch (const std::exception&) {
							}
						}
						break;
					}
				case Class::kCollisionRemove:
					s.collisionRemove = true;
					break;
				case Class::kClipEdge:
					s.clipEdge = true;
					break;
				default:
					break;
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};
	}

	bool Watch(RE::Actor* a_actor)
	{
		if (!a_actor) {
			return false;
		}
		// removed first, so a graph never gets the sink twice
		a_actor->RemoveAnimationGraphEventSink(EventSink::GetSingleton());
		return a_actor->AddAnimationGraphEventSink(EventSink::GetSingleton());
	}

	void Unwatch(RE::Actor* a_actor)
	{
		if (!a_actor) {
			return;
		}
		a_actor->RemoveAnimationGraphEventSink(EventSink::GetSingleton());
		std::scoped_lock guard(lock);
		queued.erase(a_actor->GetFormID());
	}

	Signals Take(RE::FormID a_actor)
	{
		std::scoped_lock guard(lock);
		const auto       it = queued.find(a_actor);
		if (it == queued.end()) {
			return {};
		}
		const auto out = it->second;
		queued.erase(it);
		return out;
	}

	void Clear()
	{
		std::scoped_lock guard(lock);
		queued.clear();
	}
}
