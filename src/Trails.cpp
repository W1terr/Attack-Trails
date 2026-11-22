#include "Trails.h"

#include "Attacks.h"

namespace Trails
{
	namespace
	{
		using Vec = RE::NiPoint3;
		using Settings::Element;

		constexpr auto        kParticleModel = "AttackTrails\\Particles.nif";
		constexpr auto        kLineModel = "AttackTrails\\Line.nif";
		// the same blended (NiAlphaProperty 4333) instead of added as light: for dark colors and black
		constexpr auto        kSolidParticleModel = "AttackTrails\\ParticlesSolid.nif";
		constexpr auto        kSolidLineModel = "AttackTrails\\LineSolid.nif";
		constexpr float       kDark = 0.35f;  // colors with no channel above this are drawn solid
		constexpr std::size_t kShapes = 4;       // shapes per particle batch (the model's)
		constexpr std::size_t kPerShape = 64;    // particles per shape, one bone each
		constexpr std::size_t kPerBatch = kShapes * kPerShape;
		constexpr std::array<float, kShapes> kShapeBrightness{ 1.0f, 0.65f, 1.3f, 0.85f };
		constexpr std::size_t kLineBones = 64;   // the line model's
		constexpr std::size_t kMaxBatches = 64;  // particle batches in use at once, everyone's together (of each kind: glowing, solid)
		constexpr std::size_t kMaxLines = 32;
		constexpr std::size_t kLinePoints = 512;  // a line keeps at most this many points of its path
		constexpr float       kLineStep = 3.0f;   // units of the edge's path between two of its points

		constexpr float kHistory = 1.6f;        // seconds of samples kept
		constexpr float kFollowThrough = 0.15f; // the swing window stays open this long after a hit (while the blade is fast)
		constexpr float kMaxStrike = 0.35f;     // a strike (from its start to the hit) is at most this long, at normal attack speed
		constexpr float kStrikeTurn = 2.8f;     // radians the tip's path may turn before the hit: more is the wind-up's curve
		constexpr float kNpcScan = 0.25f;       // seconds between looks for NPCs to watch
		constexpr float kMinReach = 8.0f;       // a held model shorter than this isn't a weapon
		constexpr float kMaxReach = 400.0f;
		constexpr float kBladeHead = 0.14f;     // swords and daggers: the outer share of the reach that leaves particles
		constexpr float kHeavyHead = 0.32f;     // axes, maces, hammers: their head
		constexpr std::size_t kHeadPoints = 192;
		constexpr float kWhiteCore = 0.25f;     // how much whiter than the trail's color the particles burn
		constexpr float kLineWhite = 0.5f;      // and the line
		constexpr float kLineHalfWidth = 0.9f;  // units
		constexpr float kStrikeShare = 0.45f;   // of a hand's usual strike speed: slower is a wind-up, not a swing
		constexpr float kFirstStrike = 1500.0f; // the usual strike speed before one was seen
		constexpr float kLeadShare = 0.5f;      // with a weapon in each hand, the one that strikes is at least twice as fast
		constexpr float kPairShare = 0.6f;      // both strike when the second is at least this share as fast (attacks naming no hand)
		constexpr float kMinSpeed = 450.0f;     // the least tip speed (units/s, against the body) a swing's trail needs

		// fists: the knuckles strike; a punch travels less and slower than a blade's tip
		constexpr auto  kRightHand = "NPC R Hand [RHnd]";
		constexpr auto  kLeftHand = "NPC L Hand [LHnd]";
		constexpr float kFistMinSpeed = 250.0f;
		constexpr float kFistFirstStrike = 650.0f;
		constexpr float kFistMinReach = 3.0f;
		constexpr float kFistHead = 0.25f;     // dots a little behind the knuckles too
		constexpr float kFistDensity = 1.6f;   // particles per unit of travel, against a blade's

		constexpr float kArrowMinSpeed = 700.0f;  // an arrow slower than this stuck in something (or something carries it)
		constexpr float kArrowFlight = 6.0f;      // its trail ends after this long anyway
		constexpr float kReleaseWindow = 0.6f;    // an arrow that shows up this soon after a shooter let go of the string is theirs
		constexpr float kDrawTime = 0.5f;         // a bow's draw, for animations that never say the bow is fully drawn

		// ---- math

		struct Quat
		{
			float w{ 1.0f }, x{ 0.0f }, y{ 0.0f }, z{ 0.0f };
		};

		// the rotation of a NiMatrix3 (which turns column vectors: entry[row][column])
		Quat FromMatrix(const RE::NiMatrix3& a_m)
		{
			const auto& m = a_m.entry;
			Quat        q;
			const float trace = m[0][0] + m[1][1] + m[2][2];
			if (trace > 0.0f) {
				const float s = std::sqrt(trace + 1.0f) * 2.0f;
				q = { 0.25f * s, (m[2][1] - m[1][2]) / s, (m[0][2] - m[2][0]) / s, (m[1][0] - m[0][1]) / s };
			} else if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) {
				const float s = std::sqrt(1.0f + m[0][0] - m[1][1] - m[2][2]) * 2.0f;
				q = { (m[2][1] - m[1][2]) / s, 0.25f * s, (m[0][1] + m[1][0]) / s, (m[0][2] + m[2][0]) / s };
			} else if (m[1][1] > m[2][2]) {
				const float s = std::sqrt(1.0f + m[1][1] - m[0][0] - m[2][2]) * 2.0f;
				q = { (m[0][2] - m[2][0]) / s, (m[0][1] + m[1][0]) / s, 0.25f * s, (m[1][2] + m[2][1]) / s };
			} else {
				const float s = std::sqrt(1.0f + m[2][2] - m[0][0] - m[1][1]) * 2.0f;
				q = { (m[1][0] - m[0][1]) / s, (m[0][2] + m[2][0]) / s, (m[1][2] + m[2][1]) / s, 0.25f * s };
			}
			const float n = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
			if (n < 1e-6f || !std::isfinite(n)) {
				return {};
			}
			return { q.w / n, q.x / n, q.y / n, q.z / n };
		}

		Vec Rotate(const Quat& a_q, const Vec& a_v)
		{
			const Vec u{ a_q.x, a_q.y, a_q.z };
			const Vec t = u.Cross(a_v) * 2.0f;
			return a_v + t * a_q.w + u.Cross(t);
		}

		float Dot(const Quat& a_a, const Quat& a_b) { return a_a.w * a_b.w + a_a.x * a_b.x + a_a.y * a_b.y + a_a.z * a_b.z; }

		Quat Negated(const Quat& a_q) { return { -a_q.w, -a_q.x, -a_q.y, -a_q.z }; }

		float Smoothstep(float a_lo, float a_hi, float a_x)
		{
			if (a_hi <= a_lo) {
				return a_x >= a_hi ? 1.0f : 0.0f;
			}
			const float t = std::clamp((a_x - a_lo) / (a_hi - a_lo), 0.0f, 1.0f);
			return t * t * (3.0f - 2.0f * t);
		}

		Vec Normalized(Vec a_v, const Vec& a_fallback)
		{
			return a_v.Unitize() > 1e-5f ? a_v : a_fallback;
		}

		Vec Lerp(const Vec& a_a, const Vec& a_b, float a_f) { return a_a + (a_b - a_a) * a_f; }

		// a cubic Hermite curve through p1 (time t1) and p2 (t2), slopes from the neighbours p0 and p3 (time based, so
		// uneven frames don't bend it)
		float Hermite(float a_p0, float a_p1, float a_p2, float a_p3, float a_t0, float a_t1, float a_t2, float a_t3, float a_f)
		{
			const float h = std::max(a_t2 - a_t1, 1e-4f);
			const float m1 = (a_p2 - a_p0) / std::max(a_t2 - a_t0, 1e-4f);
			const float m2 = (a_p3 - a_p1) / std::max(a_t3 - a_t1, 1e-4f);
			const float f2 = a_f * a_f, f3 = f2 * a_f;
			return a_p1 * (2 * f3 - 3 * f2 + 1) + m1 * h * (f3 - 2 * f2 + a_f) + a_p2 * (-2 * f3 + 3 * f2) + m2 * h * (f3 - f2);
		}

		// a rotation matrix from its columns
		RE::NiMatrix3 Columns(const Vec& a_x, const Vec& a_y, const Vec& a_z)
		{
			RE::NiMatrix3 m;
			m.entry[0][0] = a_x.x;
			m.entry[1][0] = a_x.y;
			m.entry[2][0] = a_x.z;
			m.entry[0][1] = a_y.x;
			m.entry[1][1] = a_y.y;
			m.entry[2][1] = a_y.z;
			m.entry[0][2] = a_z.x;
			m.entry[1][2] = a_z.y;
			m.entry[2][2] = a_z.z;
			return m;
		}

		float HalfToFloat(std::uint16_t a_h)
		{
			const std::uint32_t sign = (a_h & 0x8000u) << 16;
			std::uint32_t       exp = (a_h >> 10) & 0x1Fu;
			std::uint32_t       mant = a_h & 0x3FFu;
			std::uint32_t       bits;
			if (exp == 0) {
				if (mant == 0) {
					bits = sign;
				} else {
					exp = 127 - 15 + 1;
					while (!(mant & 0x400u)) {
						mant <<= 1;
						--exp;
					}
					mant &= 0x3FFu;
					bits = sign | (exp << 23) | (mant << 13);
				}
			} else if (exp == 31) {
				bits = sign | 0x7F800000u | (mant << 13);
			} else {
				bits = sign | ((exp + 127 - 15) << 23) | (mant << 13);
			}
			return std::bit_cast<float>(bits);
		}

		// where an object is against one of its ancestors, from the local transforms down to it: this frame's pose
		// even when the world transforms aren't updated yet
		bool RelativeTo(const RE::NiAVObject* a_object, const RE::NiAVObject* a_ancestor, RE::NiTransform& a_out)
		{
			RE::NiTransform t = a_object->local;
			for (auto p = a_object->parent; p; p = p->parent) {
				if (p == a_ancestor) {
					a_out = t;
					return true;
				}
				t = p->local * t;
			}
			return false;
		}

		std::minstd_rand random{ 0x5EED };

		float Random(float a_lo, float a_hi)
		{
			return a_lo + (a_hi - a_lo) * std::uniform_real_distribution<float>(0.0f, 1.0f)(random);
		}

		Vec RandomUnit()
		{
			for (int i = 0; i < 8; ++i) {
				Vec         v{ Random(-1.0f, 1.0f), Random(-1.0f, 1.0f), Random(-1.0f, 1.0f) };
				const float l = v.Length();
				if (l > 0.05f && l <= 1.0f) {
					return v * (1.0f / l);
				}
			}
			return { 0.0f, 0.0f, 1.0f };
		}

		// ---- settings as this module uses them

		Settings::Values tuning;
		std::uint32_t    tuningRevision{ 0 };

		// the weapon list's entries by the weapons' (loaded) form ids
		std::unordered_map<RE::FormID, std::size_t> ruleOf;

		// the enchantment list's entries by their magic effects' (loaded) form ids
		std::unordered_map<RE::FormID, std::size_t> ruleOfEffect;

		// the weapon and enchantment lists by the loaded forms
		void ResolveLists()
		{
			ruleOf.clear();
			ruleOfEffect.clear();
			const auto data = RE::TESDataHandler::GetSingleton();
			if (!data) {
				return;
			}
			for (std::size_t i = 0; i < tuning.enchantments.size(); ++i) {
				const auto& rule = tuning.enchantments[i];
				if (const auto effect = data->LookupForm<RE::EffectSetting>(rule.id, rule.plugin)) {
					ruleOfEffect.try_emplace(effect->GetFormID(), i);  // the first entry of an effect listed twice
				}
			}
			for (std::size_t i = 0; i < tuning.weapons.size(); ++i) {
				const auto& rule = tuning.weapons[i];
				if (const auto weapon = data->LookupForm<RE::TESObjectWEAP>(rule.id, rule.plugin)) {
					ruleOf.try_emplace(weapon->GetFormID(), i);  // the first entry of a weapon listed twice
				}
			}
		}

		void RefreshTuning()
		{
			const auto revision = Settings::Revision();
			if (revision == tuningRevision) {
				return;
			}
			std::unique_lock guard(Settings::Lock(), std::try_to_lock);
			if (!guard.owns_lock()) {
				return;  // the menu is drawing: next frame
			}
			tuning = Settings::Get();
			tuningRevision = revision;
			ResolveLists();
		}

		bool Debug() { return tuning.debugLog; }

		Vec Mix(const std::array<float, 3>& a_color, float a_white)
		{
			return { a_color[0] + (1.0f - a_color[0]) * a_white, a_color[1] + (1.0f - a_color[1]) * a_white, a_color[2] + (1.0f - a_color[2]) * a_white };
		}

		// ---- models
		//
		// Particle batches and swing lines are copies of their models, hung under the actor's skeleton root while in
		// use; their bones get world places each frame (turned into the root's space). A swing takes as many batches
		// as its particles need; a batch is given back when all its particles burned out and a line when its whole
		// length faded. Nothing a swing left is ever taken over by the next one.

		// [0] glowing (additive), [1] solid (blended: dark colors and black show)
		std::array<RE::NiPointer<RE::NiNode>, 2> particleModels;
		std::array<RE::NiPointer<RE::NiNode>, 2> lineModels;
		bool                      modelsLoaded{ false };
		bool                      reportedCopy{ false };

		RE::BSEffectShaderProperty* EffectOf(RE::NiAVObject* a_object)
		{
			const auto geometry = a_object ? a_object->AsGeometry() : nullptr;
			const auto property = geometry ? geometry->GetGeometryRuntimeData().shaderProperty.get() : nullptr;
			return property ? netimmerse_cast<RE::BSEffectShaderProperty*>(property) : nullptr;
		}

		// a shape of the copy with a material of its own (so each copy has its own color); nullptr if it is missing or
		// shares its shader property with the model (drawn shapes must never share one: the renderer keeps its passes
		// in it and loops)
		RE::BSEffectShaderMaterial* OwnMaterial(RE::NiNode* a_model, RE::NiNode* a_copy, const std::string& a_shape, RE::NiAVObject*& a_object)
		{
			const RE::BSFixedString name(a_shape);
			a_object = a_copy->GetObjectByName(name);
			const auto mine = EffectOf(a_object);
			const auto theirs = EffectOf(a_model->GetObjectByName(name));
			if (!mine || !mine->material) {
				logs::error("the model {} has no {} with an effect shader: is the mod's meshes folder from this version?", a_model->name.c_str(), a_shape);
				return nullptr;
			}
			if (mine == theirs) {
				logs::error("copies of {} share their shader properties: no trails (they could hang the renderer)", a_model->name.c_str());
				return nullptr;
			}
			if (theirs && theirs->material) {
				mine->SetMaterial(theirs->material, true);
				if (!reportedCopy) {
					reportedCopy = true;
					logs::info("model copies {} their own materials (own colors)", mine->material && theirs->material != mine->material ? "have" : "DON'T have");
				}
			}
			return static_cast<RE::BSEffectShaderMaterial*>(mine->material);
		}

		RE::NiNode* Copy(RE::NiNode* a_model)
		{
			const auto clone = a_model ? a_model->Clone() : nullptr;
			const auto node = clone ? netimmerse_cast<RE::NiNode*>(clone) : nullptr;
			if (!node && clone) {
				clone->DeleteThis();
			}
			return node;
		}

		RE::NiAVObject* Bone(RE::NiNode* a_copy, const std::string& a_name)
		{
			const auto found = a_copy->GetObjectByName(RE::BSFixedString(a_name));
			if (!found) {
				logs::error("the model {} has no {}: is the mod's meshes folder from this version?", a_copy->name.c_str(), a_name);
			}
			return found;
		}

		// a particle of a swing, in world space
		struct Spark
		{
			Vec   pos;
			Vec   vel;
			Vec   dir;  // the way it points (along the swing)
			float age{ 0.0f };
			float life{ 0.0f };
			float size{ 1.0f };  // its length (a dot's diameter)
			float drag{ 1.0f };
			float gravity{ 0.0f };
			float seed{ 0.0f };
			bool  twinkle{ false };
			bool  turns{ false };  // flying sparks turn into the way they fly
			bool  alive{ false };
		};

		// the model's particle quads: a round dot at every 8th index, the rest dashes
		constexpr bool IsDotSlot(std::size_t a_slot) { return a_slot % 8 == 0; }

		struct Batch
		{
			RE::NiPointer<RE::NiNode>                                    node;
			std::array<std::array<RE::NiAVObject*, kPerShape>, kShapes> bones{};
			std::array<RE::NiAVObject*, kShapes>                         shapes{};
			std::array<RE::BSEffectShaderMaterial*, kShapes>             materials{};
			std::array<Spark, kPerBatch>                                 sparks{};
			std::size_t                                                  used{ 0 };    // slots taken (alive)
			std::size_t                                                  cursor{ 0 };  // where the search for a free slot starts
			std::array<float, 3>                                         color{ 1.0f, 1.0f, 1.0f };
			RE::NiPointer<RE::NiNode>                                    parent;  // held, so detaching from it is always safe
			bool                                                         busy{ false };
			bool                                                         solid{ false };
		};

		struct LinePoint
		{
			Vec   pos;  // world
			float time;
			float weight;
		};

		struct Line
		{
			RE::NiPointer<RE::NiNode>              node;
			std::array<RE::NiAVObject*, kLineBones> bones{};
			RE::NiAVObject*                        shape{ nullptr };
			RE::BSEffectShaderMaterial*            material{ nullptr };
			std::vector<LinePoint>                 points;
			std::array<float, 3>                   color{ 1.0f, 1.0f, 1.0f };
			bool                                   open{ false };  // its swing still lays it
			RE::NiPointer<RE::NiNode>              parent;
			bool                                   busy{ false };
			bool                                   solid{ false };
		};

		std::array<std::vector<std::unique_ptr<Batch>>, 2> batchPools;
		std::array<std::vector<std::unique_ptr<Line>>, 2>  linePools;

		// dark colors are drawn with the solid models (added light can't be dark), when they are there
		bool Solid(const std::array<float, 3>& a_color)
		{
			return particleModels[1] && lineModels[1] && std::max({ a_color[0], a_color[1], a_color[2] }) < kDark;
		}

		std::unique_ptr<Batch> MakeBatch(bool a_solid)
		{
			const auto& model = particleModels[a_solid ? 1 : 0];
			const auto  node = Copy(model.get());
			if (!node) {
				return nullptr;
			}
			auto b = std::make_unique<Batch>();
			b->node.reset(node);
			b->solid = a_solid;
			for (std::size_t s = 0; s < kShapes; ++s) {
				for (std::size_t i = 0; i < kPerShape; ++i) {
					if (!(b->bones[s][i] = Bone(node, std::format("PAPart{}_{:02}", s, i)))) {
						return nullptr;
					}
				}
				if (!(b->materials[s] = OwnMaterial(model.get(), node, std::format("Particles{}", s), b->shapes[s]))) {
					return nullptr;
				}
			}
			return b;
		}

		std::unique_ptr<Line> MakeLine(bool a_solid)
		{
			const auto& model = lineModels[a_solid ? 1 : 0];
			const auto  node = Copy(model.get());
			if (!node) {
				return nullptr;
			}
			auto l = std::make_unique<Line>();
			l->node.reset(node);
			l->solid = a_solid;
			for (std::size_t i = 0; i < kLineBones; ++i) {
				if (!(l->bones[i] = Bone(node, std::format("PALine{:02}", i)))) {
					return nullptr;
				}
			}
			if (!(l->material = OwnMaterial(model.get(), node, "Line", l->shape))) {
				return nullptr;
			}
			l->points.reserve(kLinePoints + 8);
			return l;
		}

		template <class T, class Make>
		T* Take(std::vector<std::unique_ptr<T>>& a_pool, std::size_t a_max, Make&& a_make, RE::NiNode* a_parent)
		{
			T* free = nullptr;
			for (const auto& item : a_pool) {
				if (!item->busy) {
					free = item.get();
					break;
				}
			}
			if (!free) {
				if (a_pool.size() >= a_max) {
					return nullptr;
				}
				auto made = a_make();
				if (!made) {
					return nullptr;
				}
				free = a_pool.emplace_back(std::move(made)).get();
			}
			free->busy = true;
			free->parent.reset(a_parent);
			a_parent->AttachChild(free->node.get(), true);
			return free;
		}

		template <class T>
		void GiveBack(T* a_item)
		{
			if (!a_item) {
				return;
			}
			if (a_item->parent && a_item->node->parent == a_item->parent.get()) {
				a_item->parent->DetachChild(a_item->node.get());
			}
			a_item->parent.reset();
			a_item->busy = false;
		}

		// what an enchantment says about the trail's color: an entry of the enchantment list, or the color it glows with
		struct Tint
		{
			int                       rule{ -1 };  // its entry in the enchantment list
			bool                      glow{ false };
			std::array<float, 3>      color{ 1.0f, 1.0f, 1.0f };  // the glow's
			const RE::EffectSetting*  effect{ nullptr };          // the effect that decided it (the costliest if none did)
		};

		// ---- the watched actors

		struct Sample
		{
			float         time;
			Vec           pos;  // the hand's weapon node, against the skeleton root
			Quat          rot;
			float         scale;
			float         speed;  // of the weapon's farthest point, units/s
			std::uint32_t attack;  // the attack it was taken in, 0 = none
			bool          window;  // inside a swing window
			bool          lead;    // this hand moved at least half as fast as the other one (or there is no other)
			bool          strong;  // keeps up with the other hand (kPairShare of its speed, or there is no other): a two-weapon swing
			bool          emitted;  // its stretch of the swing laid its particles
			std::uint8_t  parts;    // the body parts its Precision hit windows are on (1 right, 2 left, 4 another), 0: no such window
			float         shoulderDist;  // fists: the knuckles' distance from the shoulder
			float         out;           // and how fast it grows (units/s): negative while the arm pulls back
		};

		struct Hand
		{
			bool                  left{ false };
			bool                  fist{ false };    // nothing in it (or a hand-to-hand weapon): the fist strikes
			float                 minSpeed{ kMinSpeed };
			RE::NiAVObject*       node{ nullptr };  // WEAPON / SHIELD of the root below (the hand itself for a fist)
			const RE::NiAVObject* held{ nullptr };  // the model hanging under it, as measured
			RE::TESObjectWEAP*    weapon{ nullptr };
			RE::TESObjectWEAP*    looked{ nullptr };  // the weapon this hand holds (a bow too), whose look is below
			RE::EnchantmentItem*  enchantment{ nullptr };
			Element               element{ Settings::kPlain };
			float                 elementCheck{ 0.0f };
			bool                  unique{ false };     // a daedric artifact or a weapon made with its own enchantment
			int                   rule{ -1 };          // its entry in the weapon list
			bool                  trailOn{ true };     // the settings let it leave trails
			std::array<float, 3>  color{ 1.0f, 1.0f, 1.0f };
			Tint                  tint;
			std::uint32_t         tuned{ 0 };  // the settings revision the look was worked out with

			// the weapon's shape, in the weapon node's space
			Vec              axis{ 0.0f, 1.0f, 0.0f };  // hand to its farthest point
			float            reach{ 0.0f };
			bool             fromVertices{ false };
			bool             bladeOk{ false };
			float            measureAgain{ 0.0f };
			float            findAgain{ 0.0f };  // VR: the next look for the weapon / hand node
			const RE::NiAVObject* shoulder{ nullptr };    // fists: the upper arm above the hand
			const RE::NiAVObject* shoulderOf{ nullptr };  // the hand node it was found for
			bool             heavy{ false };  // an axe, mace or hammer: its head strikes, not a tip
			std::vector<Vec> head;            // points of the part that strikes
			Vec              headCenter;
			std::vector<Vec> edges;           // where that part could lead a swing (heavy weapons); [0] = the farthest point

			std::deque<Sample> history;
			float              strikeSpeed{ kFirstStrike };  // its usual speed in a swing (learned)

			std::vector<Batch*> batches;  // holding this hand's particles
			std::vector<Line*>  lines;    // this hand's lines, the newest maybe still laid
			Line*               current{ nullptr };
			std::uint32_t       currentAttack{ 0 };
			std::size_t         edge{ 0 };  // the edge the current swing's line follows
			float               lastLaid{ -10.0f };
		};

		struct Tracked
		{
			RE::ActorHandle handle;
			RE::FormID      formID{ 0 };
			bool            player{ false };
			const void*     watchedRoot{ nullptr };  // the 3D the event sink was added with
			RE::NiNode*     root{ nullptr };          // the skeleton the samples are taken against
			bool            firstPerson{ false };
			bool            vr{ false };              // the VR player: hands follow the controllers, swings are the player's own motion
			bool            vrBowSeen{ false };       // VR: the bow's draw amount was seen moving

			// the attack running (0 = none)
			std::uint32_t attack{ 0 };
			bool          power{ false };
			float         startedAt{ 0.0f };
			float         lastEventAt{ 0.0f };
			bool          stateSeen{ false };  // the game's attack state showed it
			float         noneFor{ 0.0f };
			bool          sawWindow{ false };  // it sent swing / hit events
			float         windowUntil{ -1.0f };
			bool          precision{ false };  // the attack's animation times its hits with Precision annotations
			std::uint8_t  windowParts{ 0 };    // the parts its open Precision hit windows are on
			float         swingAt{ -1.0f };    // when, while its hit hasn't come (-1: no swing waiting)
			bool          sawHit{ false };
			std::uint8_t  nameHands{ 0 };       // the hands its attack data names (1 right, 2 left), 0 = unknown
			std::uint8_t  eventHands{ 0 };      // the hands its swing events named

			// Whether the game's attack data belongs to the attack running. The game sets it when an attack goes
			// through its own attack start (which puts the attack state into its wind-up, kDraw); combat frameworks
			// that start attacks their own way leave the last attack's data there, a power attack's too.
			const RE::BGSAttackData*  lastData{ nullptr };   // last frame's
			RE::ATTACK_STATE_ENUM     lastState{ RE::ATTACK_STATE_ENUM::kNone };
			const RE::BGSAttackData*  beginData{ nullptr };  // what it was when the attack running began
			bool                      sawDraw{ false };      // the attack running went through the game's wind-up
			float                     powerEventAt{ -100.0f };  // the latest power attack event
			float                     edgeAt{ -100.0f };        // the latest sign of an attack beginning or ending (start events, clip edges)

			// the latest attacks: power attack or not, the hands that strike
			struct Past
			{
				std::uint32_t attack{ 0 };
				bool          power{ false };
				std::uint8_t  hands{ 3 };
				bool          named{ false };  // the hands come from the attack data's name (else from swing events or a guess)
			};
			std::array<Past, 8> past{};
			std::size_t         pastNext{ 0 };

			std::array<Hand, 2> hands{};
			bool                keep{ true };
			bool                drawn{ false };

			const RE::TESRace* race{ nullptr };  // the race punches was worked out for
			bool               punches{ false };

			// the gauntlets worn (they color the fists' trails)
			RE::TESObjectARMO*   gauntlet{ nullptr };
			RE::EnchantmentItem* gauntletEnchantment{ nullptr };
			float                gauntletCheck{ 0.0f };

			// the bow: since when it is drawn (-1: it isn't), since when fully, and the last shot's hold
			float bowDrawStart{ -1.0f };
			float bowFullAt{ -1.0f };
			float lastHold{ 0.0f };
			float releasedAt{ -100.0f };
		};

		std::vector<std::unique_ptr<Tracked>> tracked;  // [0] = the player
		float                                 now{ 0.0f };
		float                                 npcScanIn{ 0.0f };
		float                                 statusIn{ 0.0f };
		std::uint32_t                         attackCounter{ 0 };

		std::mutex statusLock;
		Status     status;

		const Tracked::Past* PastOf(const Tracked& a_t, std::uint32_t a_attack)
		{
			if (!a_attack) {
				return nullptr;
			}
			const auto it = std::ranges::find(a_t.past, a_attack, &Tracked::Past::attack);
			return it != a_t.past.end() ? &*it : nullptr;
		}

		Tracked::Past& CurrentPast(Tracked& a_t)
		{
			for (auto& p : a_t.past) {
				if (p.attack == a_t.attack) {
					return p;
				}
			}
			auto& p = a_t.past[a_t.pastNext];
			a_t.pastNext = (a_t.pastNext + 1) % a_t.past.size();
			p = { a_t.attack, false, 3, false };
			return p;
		}

		void ReleaseHand(Hand& a_h, bool a_clearHistory)
		{
			for (auto* b : a_h.batches) {
				for (auto& sp : b->sparks) {
					sp.alive = false;
				}
				b->used = 0;
				GiveBack(b);
			}
			a_h.batches.clear();
			for (auto* l : a_h.lines) {
				l->points.clear();
				l->open = false;
				GiveBack(l);
			}
			a_h.lines.clear();
			a_h.current = nullptr;
			if (a_clearHistory) {
				a_h.history.clear();
			}
		}

		void ReleaseHands(Tracked& a_t, bool a_clearHistory)
		{
			for (auto& h : a_t.hands) {
				ReleaseHand(h, a_clearHistory);
			}
		}

		// ---- weapons

		bool IsMelee(const RE::TESObjectWEAP* a_weapon)
		{
			if (!a_weapon) {
				return false;
			}
			switch (a_weapon->GetWeaponType()) {
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

		bool IsHeavy(const RE::TESObjectWEAP* a_weapon)
		{
			switch (a_weapon->GetWeaponType()) {
			case RE::WEAPON_TYPE::kOneHandAxe:
			case RE::WEAPON_TYPE::kOneHandMace:
			case RE::WEAPON_TYPE::kTwoHandAxe:  // battle axes and warhammers
				return true;
			default:
				return false;
			}
		}

		// Soul traps aren't always made as soul trap effects: the game's own Soul Trap enchantment (EnchSoulTrapFFContact,
		// "Binding" weapons) is a script effect. Also recognized: that effect itself, and effects that show the soul trap
		// shader on their target (mods' soul traps made like the game's).
		RE::EffectSetting*   soulTrapEnchantment{ nullptr };
		RE::TESEffectShader* soulTrapShader{ nullptr };

		void FindSoulTrapForms()
		{
			const auto data = RE::TESDataHandler::GetSingleton();
			soulTrapEnchantment = data ? data->LookupForm<RE::EffectSetting>(0x05B452, "Skyrim.esm") : nullptr;
			soulTrapShader = data ? data->LookupForm<RE::TESEffectShader>(0x0506D7, "Skyrim.esm") : nullptr;  // SoulTrapFXShader
		}

		bool IsSoulTrap(const RE::EffectSetting* a_effect)
		{
			return a_effect && (a_effect->GetArchetype() == RE::EffectArchetypes::ArchetypeID::kSoulTrap || (soulTrapEnchantment && a_effect == soulTrapEnchantment) ||
								   (soulTrapShader && a_effect->data.effectShader == soulTrapShader));
		}

		Element Classify(const RE::EnchantmentItem* a_enchantment)
		{
			if (!a_enchantment) {
				return Settings::kPlain;
			}
			// fire, frost, shock: the strongest; all three together are chaos
			Element       best = Settings::kOther;
			float         bestMagnitude = -1.0f;
			std::uint32_t elements = 0;
			for (const auto effect : a_enchantment->effects) {
				const auto mgef = effect ? effect->baseEffect : nullptr;
				if (!mgef) {
					continue;
				}
				Element    element = Settings::kOther;
				const auto resist = mgef->data.resistVariable;
				if (mgef->HasKeywordString("MagicDamageFire") || resist == RE::ActorValue::kResistFire) {
					element = Settings::kFire;
				} else if (mgef->HasKeywordString("MagicDamageFrost") || resist == RE::ActorValue::kResistFrost) {
					element = Settings::kFrost;
				} else if (mgef->HasKeywordString("MagicDamageShock") || resist == RE::ActorValue::kResistShock) {
					element = Settings::kShock;
				}
				if (element == Settings::kOther) {
					continue;
				}
				elements |= 1u << element;
				if (effect->effectItem.magnitude > bestMagnitude) {
					best = element;
					bestMagnitude = effect->effectItem.magnitude;
				}
			}
			if (std::popcount(elements) >= 3) {
				return Settings::kChaos;
			}
			if (best != Settings::kOther) {
				return best;
			}
			// no fire, frost or shock: what it drains or absorbs (fire damage also hits health, so this comes second)
			bestMagnitude = -1.0f;
			for (const auto effect : a_enchantment->effects) {
				const auto mgef = effect ? effect->baseEffect : nullptr;
				if (!mgef) {
					continue;
				}
				Element element = Settings::kOther;
				switch (mgef->data.primaryAV) {
				case RE::ActorValue::kHealth:
					element = Settings::kHealth;
					break;
				case RE::ActorValue::kMagicka:
					element = Settings::kMagicka;
					break;
				case RE::ActorValue::kStamina:
					element = Settings::kStamina;
					break;
				default:
					break;
				}
				if (element != Settings::kOther && effect->effectItem.magnitude > bestMagnitude) {
					best = element;
					bestMagnitude = effect->effectItem.magnitude;
				}
			}
			if (best != Settings::kOther) {
				return best;
			}
			// the rest by what kind of effect it is
			using Kind = RE::EffectArchetypes::ArchetypeID;
			for (const auto effect : a_enchantment->effects) {
				const auto mgef = effect ? effect->baseEffect : nullptr;
				if (!mgef) {
					continue;
				}
				if (IsSoulTrap(mgef)) {
					return Settings::kSoulTrap;
				}
				switch (mgef->GetArchetype()) {
				case Kind::kParalysis:
					return Settings::kParalysis;
				case Kind::kDemoralize:
					return Settings::kFear;
				case Kind::kTurnUndead:
					return Settings::kTurnUndead;
				case Kind::kBanish:
					return Settings::kBanish;
				case Kind::kCalm:
					return Settings::kCalm;
				case Kind::kFrenzy:
					return Settings::kFrenzy;
				default:
					break;
				}
			}
			return Settings::kOther;
		}

		// The color an enchanted item glows with: its effect's enchant shader. Its edge color mostly (armor enchantments:
		// Fortify Health dark red, Fortify Stamina green...), else a fill or particle color; only a clearly colored one,
		// brought up to full brightness. Shaders colored by their textures (fire, shock) give white or grey: no glow color.
		bool Glow(const RE::EffectSetting* a_effect, std::array<float, 3>& a_color)
		{
			const auto shader = a_effect ? a_effect->data.enchantShader : nullptr;
			if (!shader) {
				return false;
			}
			const auto& d = shader->data;
			float       best = 0.0f;
			for (const auto& c : { d.edgeEffectColor, d.fillTextureEffectColorKey1, d.fillTextureEffectColorKey2, d.fillTextureEffectColorKey3, d.colorKey1 }) {
				const float r = c.red / 255.0f, g = c.green / 255.0f, b = c.blue / 255.0f;
				const float hi = std::max({ r, g, b });
				const float lo = std::min({ r, g, b });
				if (hi < 0.08f) {
					continue;  // black: adds nothing
				}
				const float saturation = (hi - lo) / hi;
				if (saturation >= 0.3f && saturation > best) {
					best = saturation;
					a_color = { r / hi, g / hi, b / hi };
				}
			}
			return best > 0.0f;
		}

		Tint TintOf(const RE::EnchantmentItem* a_enchantment)
		{
			Tint t;
			if (!a_enchantment) {
				return t;
			}
			float ruleCost = -1.0f, glowCost = -1.0f, anyCost = -1.0f;
			const RE::EffectSetting* glowEffect = nullptr;
			for (const auto effect : a_enchantment->effects) {
				const auto mgef = effect ? effect->baseEffect : nullptr;
				if (!mgef) {
					continue;
				}
				const float cost = std::max(effect->cost, 0.0f) + effect->effectItem.magnitude * 1e-4f;
				if (const auto found = ruleOfEffect.find(mgef->GetFormID()); found != ruleOfEffect.end() && cost > ruleCost) {
					ruleCost = cost;
					t.rule = static_cast<int>(found->second);
					t.effect = mgef;
				}
				std::array<float, 3> c{};
				if (cost > glowCost && Glow(mgef, c)) {
					glowCost = cost;
					t.glow = true;
					t.color = c;
					glowEffect = mgef;
				}
				if (cost > anyCost) {
					anyCost = cost;
					if (t.rule < 0) {
						t.effect = mgef;
					}
				}
			}
			if (t.rule < 0 && glowEffect) {
				t.effect = glowEffect;
			}
			return t;
		}

		// the color before the weapon list: the enchantment list, else the glow (not for bound and ghost weapons, which
		// have their own), else the kind's
		std::array<float, 3> BaseColor(Element a_element, const Tint& a_tint)
		{
			if (a_tint.rule >= 0) {
				return tuning.enchantments[static_cast<std::size_t>(a_tint.rule)].color;
			}
			if (tuning.glowColors && a_tint.glow && a_element != Settings::kBound && a_element != Settings::kGhost) {
				return a_tint.color;
			}
			return tuning.elements[a_element].color;
		}

		bool BaseOn(Element a_element, const Tint& a_tint)
		{
			return a_tint.rule >= 0 ? tuning.enchantments[static_cast<std::size_t>(a_tint.rule)].trail : tuning.elements[a_element].enabled;
		}

		// weapons the game deals out as loot: in a leveled list together with weapons of other names (the Drainheart
		// Sword with the other draugr swords). The leveled versions of one unique weapon (Chillrend, the Nightingale
		// Blade) share their name.
		std::unordered_set<RE::FormID> lootWeapons;

		void FindLoot()
		{
			lootWeapons.clear();
			const auto data = RE::TESDataHandler::GetSingleton();
			if (!data) {
				return;
			}
			std::vector<const RE::TESObjectWEAP*> inList;
			for (const auto list : data->GetFormArray<RE::TESLevItem>()) {
				if (!list) {
					continue;
				}
				inList.clear();
				for (const auto& entry : list->entries) {
					if (const auto weapon = entry.form ? entry.form->As<RE::TESObjectWEAP>() : nullptr) {
						inList.push_back(weapon);
					}
				}
				if (inList.size() < 2) {
					continue;
				}
				const std::string_view first = inList.front()->GetName();
				if (std::ranges::any_of(inList, [&](const RE::TESObjectWEAP* a_w) { return std::string_view(a_w->GetName()) != first; })) {
					for (const auto weapon : inList) {
						lootWeapons.insert(weapon->GetFormID());
					}
				}
			}
			logs::info("{} weapons are loot from mixed leveled lists (never unique)", lootWeapons.size());
		}

		// a daedric artifact, or a weapon made with an enchantment of its own: Dawnbreaker, Chillrend, Ghostblade,
		// Grimsever... but not the enchanted copies of ordinary weapons ("Ebony Sword of Burning" is an Ebony Sword
		// with a template) nor what the player enchanted (the enchantment is on the item, not the weapon)
		bool IsUnique(const RE::TESObjectWEAP* a_weapon)
		{
			if (!a_weapon) {
				return false;
			}
			if (a_weapon->HasKeywordString("DaedricArtifact")) {
				return true;
			}
			if (!a_weapon->formEnchanting || lootWeapons.contains(a_weapon->GetFormID())) {
				return false;
			}
			const auto base = a_weapon->templateWeapon;
			if (!base || base == a_weapon) {
				return true;
			}
			const std::string_view name = a_weapon->GetName();
			const std::string_view baseName = base->GetName();
			return baseName.empty() || name.find(baseName) == std::string_view::npos;  // a name of its own
		}

		// whether the settings let a weapon leave trails, and in which color
		struct Verdict
		{
			bool                 on{ true };
			std::array<float, 3> color{ 1.0f, 1.0f, 1.0f };
			int                  rule{ -1 };  // its entry in the weapon list
		};

		Verdict LookAt(const RE::TESObjectWEAP* a_weapon, Element a_element, bool a_unique, const Tint& a_tint)
		{
			Verdict     v;
			const auto  found = a_weapon ? ruleOf.find(a_weapon->GetFormID()) : ruleOf.end();
			const bool  enabled = BaseOn(a_element, a_tint);
			v.color = BaseColor(a_element, a_tint);
			if (found != ruleOf.end()) {
				const auto& rule = tuning.weapons[found->second];
				v.rule = static_cast<int>(found->second);
				v.on = rule.trail;
				if (rule.ownColor) {
					v.color = rule.color;
				}
				return v;
			}
			switch (tuning.weaponFilter) {
			case Settings::kUniqueWeapons:
				v.on = a_unique && enabled;
				break;
			case Settings::kListedWeapons:
				v.on = false;
				break;
			default:
				v.on = enabled;
				break;
			}
			return v;
		}

		void Look(Hand& a_h)
		{
			if (a_h.fist) {
				// fists aren't weapons: the weapon filter and list don't apply, only their own switch and the color's
				a_h.trailOn = tuning.fists && BaseOn(a_h.element, a_h.tint);
				a_h.color = BaseColor(a_h.element, a_h.tint);
				a_h.rule = -1;
				return;
			}
			const auto v = LookAt(a_h.looked, a_h.element, a_h.unique, a_h.tint);
			a_h.trailOn = tuning.enabled && v.on;  // weapons, fists and arrows each have their own switch
			a_h.color = v.color;
			a_h.rule = v.rule;
		}

		// ---- ghosts (the rule of Incorporeal Ghosts): a ghost keyword, a ghost race, or a ghost ability (a constant
		// scripted effect, not hostile, that puts one of the ghost effect shaders on the actor)

		struct GhostForms
		{
			std::vector<RE::BGSKeyword*>     keywords;
			std::vector<RE::TESRace*>        races;
			std::vector<RE::EffectSetting*>  effects;
			RE::TESObjectWEAP*               ghostblade{ nullptr };
		} ghostForms;

		void FindGhostForms()
		{
			ghostForms = {};
			const auto data = RE::TESDataHandler::GetSingleton();
			if (!data) {
				return;
			}
			for (const auto& [plugin, id] : std::initializer_list<std::pair<const char*, RE::FormID>>{ { "Skyrim.esm", 0x0D205E } }) {
				if (const auto keyword = data->LookupForm<RE::BGSKeyword>(id, plugin)) {
					ghostForms.keywords.push_back(keyword);  // ActorTypeGhost
				}
			}
			for (const auto keyword : data->GetFormArray<RE::BGSKeyword>()) {
				if (keyword && keyword->formEditorID == "IG_Incorporeal") {
					ghostForms.keywords.push_back(keyword);  // Incorporeal Ghosts' own
				}
			}
			// WispRace, WispShadeRace, DLC2GhostFrostGiantRace
			for (const auto& [plugin, id] : std::initializer_list<std::pair<const char*, RE::FormID>>{ { "Skyrim.esm", 0x013208 }, { "Skyrim.esm", 0x0F1182 }, { "Dragonborn.esm", 0x01CAD8 } }) {
				if (const auto race = data->LookupForm<RE::TESRace>(id, plugin)) {
					ghostForms.races.push_back(race);
				}
			}
			// GhostFXShader, GhostFXShaderNew, MGGhostFXShader, GhostVioletFXShader, GhostFXShaderNightingale,
			// GhostFXShaderNightingaleCorrupt, DLC1SoulCairnGhostFXShader
			std::unordered_set<const RE::TESEffectShader*> shaders;
			for (const auto& [plugin, id] : std::initializer_list<std::pair<const char*, RE::FormID>>{ { "Skyrim.esm", 0x03B6CB }, { "Skyrim.esm", 0x0D2057 },
					 { "Skyrim.esm", 0x06A155 }, { "Skyrim.esm", 0x103129 }, { "Skyrim.esm", 0x0C5EF7 }, { "Skyrim.esm", 0x0C7908 }, { "Dawnguard.esm", 0x019AB3 } }) {
				if (const auto shader = data->LookupForm<RE::TESEffectShader>(id, plugin)) {
					shaders.insert(shader);
				}
			}
			for (const auto effect : data->GetFormArray<RE::EffectSetting>()) {
				if (!effect) {
					continue;
				}
				const auto& d = effect->data;
				if (d.archetype == RE::EffectArchetypes::ArchetypeID::kScript && d.castingType == RE::MagicSystem::CastingType::kConstantEffect &&
					!effect->IsHostile() && !effect->IsDetrimental() && (shaders.contains(d.effectShader) || shaders.contains(d.enchantShader))) {
					ghostForms.effects.push_back(effect);
				}
			}
			ghostForms.ghostblade = data->LookupForm<RE::TESObjectWEAP>(0x094A2B, "Skyrim.esm");
			logs::info("ghosts: {} keywords, {} races, {} ghost ability effects", ghostForms.keywords.size(), ghostForms.races.size(), ghostForms.effects.size());
		}

		bool IsGhost(RE::Actor* a_actor)
		{
			if (!a_actor || a_actor->IsPlayerRef()) {
				return false;
			}
			const auto base = a_actor->GetActorBase();
			const auto race = a_actor->GetRace();
			for (const auto keyword : ghostForms.keywords) {
				if ((base && base->HasKeyword(keyword)) || (race && race->HasKeyword(keyword))) {
					return true;
				}
			}
			if (race && std::ranges::find(ghostForms.races, race) != ghostForms.races.end()) {
				return true;
			}
			const auto target = a_actor->AsMagicTarget();
			return target && std::ranges::any_of(ghostForms.effects, [&](RE::EffectSetting* a_effect) { return target->HasMagicEffect(a_effect); });
		}

		// a weapon's kind of trail: bound weapons and ghosts' weapons (and the Ghostblade) have their own, else the
		// enchantment's
		Element ElementOf(RE::Actor* a_actor, const RE::TESObjectWEAP* a_weapon, const RE::EnchantmentItem* a_enchantment)
		{
			if (a_weapon && a_weapon->IsBound()) {
				return Settings::kBound;
			}
			if ((a_weapon && a_weapon == ghostForms.ghostblade) || IsGhost(a_actor)) {
				return Settings::kGhost;
			}
			return Classify(a_enchantment);
		}

		RE::EnchantmentItem* EnchantmentOf(RE::Actor* a_actor, bool a_left, RE::TESObjectWEAP* a_weapon)
		{
			if (const auto entry = a_actor->GetEquippedEntryData(a_left)) {
				if (const auto enchantment = entry->GetEnchantment()) {
					return enchantment;
				}
			}
			return a_weapon ? a_weapon->formEnchanting : nullptr;
		}

		// a hand with nothing in it, or a hand-to-hand weapon (claws, knuckles)
		bool IsUnarmed(const RE::TESForm* a_form)
		{
			if (!a_form) {
				return true;
			}
			const auto weapon = a_form->As<RE::TESObjectWEAP>();
			return weapon && weapon->GetWeaponType() == RE::WEAPON_TYPE::kHandToHandMelee;
		}

		bool HoldsWeapon(const RE::TESForm* a_form)
		{
			const auto weapon = a_form ? a_form->As<RE::TESObjectWEAP>() : nullptr;
			return weapon && weapon->GetWeaponType() != RE::WEAPON_TYPE::kHandToHandMelee;
		}

		// people punch; creatures and beast forms (werewolves, vampire lords, werebears: no ActorTypeNPC) claw and bite
		bool CanPunch(Tracked& a_t, RE::Actor* a_actor)
		{
			const auto race = a_actor->GetRace();
			if (race != a_t.race) {
				a_t.race = race;
				a_t.punches = race && race->HasKeywordString("ActorTypeNPC");
			}
			return a_t.punches;
		}

		// the gauntlets worn and their enchantment (one you put on them yourself, else their own), once a second
		void CheckGauntlets(Tracked& a_t, RE::Actor* a_actor)
		{
			if (now < a_t.gauntletCheck) {
				return;
			}
			a_t.gauntletCheck = now + 1.0f;
			a_t.gauntlet = nullptr;
			a_t.gauntletEnchantment = nullptr;
			const auto inventory = a_actor->GetInventory([](RE::TESBoundObject& a_object) { return a_object.IsArmor(); });
			for (const auto& [item, data] : inventory) {
				const auto& [count, entry] = data;
				const auto armor = item ? item->As<RE::TESObjectARMO>() : nullptr;
				if (count > 0 && entry && armor && entry->IsWorn() && armor->HasPartOf(RE::BGSBipedObjectForm::BipedObjectSlot::kHands)) {
					a_t.gauntlet = armor;
					a_t.gauntletEnchantment = entry->GetEnchantment();
					if (!a_t.gauntletEnchantment) {
						a_t.gauntletEnchantment = armor->formEnchanting;
					}
					break;
				}
			}
		}

		// the points of a held model, in the hand's (weapon node's) space: its vertices if the game kept them, else
		// the far side of its bounds
		void Gather(RE::NiAVObject* a_object, const RE::NiAVObject* a_hand, std::vector<Vec>& a_points, bool& a_vertices, int a_depth)
		{
			if (!a_object || a_depth > 16 || a_object->GetFlags().any(RE::NiAVObject::Flag::kHidden)) {
				return;
			}
			if (const auto node = a_object->AsNode()) {
				for (const auto& child : node->GetChildren()) {
					Gather(child.get(), a_hand, a_points, a_vertices, a_depth + 1);
				}
				return;
			}
			const auto shape = a_object->AsTriShape();
			if (!shape || EffectOf(shape)) {
				return;  // particles, glows and enchantment art: not the weapon
			}
			RE::NiTransform rel;
			if (!RelativeTo(shape, a_hand, rel)) {
				return;
			}
			auto&       data = shape->GetGeometryRuntimeData();
			const auto  renderer = data.rendererData;
			const auto  desc = std::bit_cast<std::uint64_t>(data.vertexDesc);
			const auto  stride = static_cast<std::size_t>(desc & 0xF) * 4;
			const auto  count = shape->GetTrishapeRuntimeData().vertexCount;
			const auto& bound = shape->GetModelData().modelBound;

			// The precision flag can't be trusted (weapons often store half floats without it, skinned meshes full
			// ones): the UV right after the position tells the position's size; the reading that keeps the vertices
			// inside the model's bounding sphere wins, and if neither does, the sphere is used
			if (renderer && renderer->rawVertexData && stride >= 6 && count > 0 && bound.radius > 0.0f) {
				const auto flags = static_cast<std::uint16_t>(desc >> 44);
				const auto uvOffset = static_cast<std::uint32_t>((desc >> (4 * RE::BSGraphics::Vertex::VA_TEXCOORD0 + 2)) & 0x3C);
				bool       full = (flags & RE::BSGraphics::Vertex::VF_FULLPREC) != 0;
				if (flags & RE::BSGraphics::Vertex::VF_UV) {
					full = uvOffset >= 12;
				}
				const auto read = [&](std::uint32_t a_v, bool a_full) {
					const auto* p = renderer->rawVertexData + static_cast<std::size_t>(a_v) * stride;
					Vec         local;
					if (a_full) {
						std::memcpy(&local, p, 12);
					} else {
						std::uint16_t h[3];
						std::memcpy(h, p, 6);
						local = { HalfToFloat(h[0]), HalfToFloat(h[1]), HalfToFloat(h[2]) };
					}
					return local;
				};
				const float slack = bound.radius * 1.05f + 1.0f;
				const auto  outside = [&](bool a_full) -> std::uint32_t {
					if (a_full && stride < 12) {
						return count;
					}
					std::uint32_t bad = 0;
					for (std::uint32_t v = 0; v < count; ++v) {
						const Vec local = read(v, a_full);
						if (!std::isfinite(local.x) || !std::isfinite(local.y) || !std::isfinite(local.z) || local.GetDistance(bound.center) > slack) {
							++bad;
						}
					}
					return bad;
				};
				auto bad = outside(full);
				if (bad > 0) {
					const auto other = outside(!full);
					if (other < bad) {
						full = !full;
						bad = other;
					}
				}
				if (bad <= static_cast<std::uint32_t>(count) / 50u) {
					for (std::uint32_t v = 0; v < count; ++v) {
						const Vec local = read(v, full);
						if (local.GetDistance(bound.center) <= slack) {
							const Vec p = rel * local;
							if (p.Length() < kMaxReach) {
								a_points.push_back(p);
							}
						}
					}
					a_vertices = true;
					return;
				}
			}
			const Vec   center = rel * bound.center;
			const float length = center.Length();
			if (length > 1.0f && bound.radius > 0.0f) {
				const Vec farSide = center * ((length + bound.radius * rel.scale) / length);
				if (farSide.Length() < kMaxReach) {
					a_points.push_back(farSide);
				}
			}
		}

		// the weapon's reach and the part of it that strikes: a sword's outer blade, an axe's, mace's or hammer's head
		void MeasureWeapon(Hand& a_h)
		{
			std::vector<Vec> points;
			bool             vertices = false;
			Gather(a_h.node, a_h.node, points, vertices, 0);
			a_h.reach = 0.0f;
			a_h.axis = { 0.0f, 1.0f, 0.0f };
			a_h.fromVertices = vertices;
			for (const auto& p : points) {
				const float d = p.Length();
				if (std::isfinite(d) && d > a_h.reach) {
					a_h.reach = d;
					a_h.axis = p * (1.0f / d);
				}
			}
			a_h.bladeOk = a_h.reach >= kMinReach;
			a_h.head.clear();
			a_h.edges.clear();
			if (!a_h.bladeOk) {
				return;
			}
			const float share = a_h.heavy ? kHeavyHead : kBladeHead;
			const float from = a_h.reach * (1.0f - share);
			std::vector<Vec> head;
			for (const auto& p : points) {
				if (p.Length() >= from) {
					head.push_back(p);
				}
			}
			if (head.size() < 4) {
				// no vertices: along the weapon's line
				head.clear();
				for (int i = 0; i <= 16; ++i) {
					head.push_back(a_h.axis * (from + (a_h.reach - from) * static_cast<float>(i) / 16.0f));
				}
			}
			const std::size_t step = std::max<std::size_t>(1, head.size() / kHeadPoints);
			for (std::size_t i = 0; i < head.size(); i += step) {
				a_h.head.push_back(head[i]);
			}
			Vec center{};
			for (const auto& p : a_h.head) {
				center += p;
			}
			a_h.headCenter = center * (1.0f / static_cast<float>(a_h.head.size()));

			// the edges a swing can lead with: the farthest point, and for heavy weapons the head's extremes across
			// the haft (an axe's blade or its back, a hammer's faces, a mace's flanges)
			a_h.edges.push_back(a_h.axis * a_h.reach);
			if (a_h.heavy) {
				const Vec e1 = Normalized(a_h.axis.Cross(std::abs(a_h.axis.z) < 0.9f ? Vec{ 0.0f, 0.0f, 1.0f } : Vec{ 1.0f, 0.0f, 0.0f }), { 1.0f, 0.0f, 0.0f });
				const Vec e2 = a_h.axis.Cross(e1);
				for (const Vec& dir : { e1, e1 * -1.0f, e2, e2 * -1.0f }) {
					const Vec* best = nullptr;
					float      most = -1e9f;
					for (const auto& p : a_h.head) {
						const float d = (p - a_h.headCenter).Dot(dir);
						if (d > most) {
							most = d;
							best = &p;
						}
					}
					if (best) {
						a_h.edges.push_back(*best);
					}
				}
			}
		}

		bool NamedAs(const RE::NiAVObject* a_object, const char* a_name)
		{
			const char* name = a_object ? a_object->name.c_str() : nullptr;
			return name && _stricmp(name, a_name) == 0;
		}

		// the first object of that name under a_object (or a_object itself), by its text
		RE::NiAVObject* FindNamed(RE::NiAVObject* a_object, const char* a_name, int a_depth = 0)
		{
			if (!a_object || a_depth > 96) {
				return nullptr;
			}
			if (NamedAs(a_object, a_name)) {
				return a_object;
			}
			if (const auto node = a_object->AsNode()) {
				for (const auto& child : node->GetChildren()) {
					if (const auto found = FindNamed(child.get(), a_name, a_depth + 1)) {
						return found;
					}
				}
			}
			return nullptr;
		}

		void CollectNamed(RE::NiAVObject* a_object, const char* a_name, std::vector<RE::NiNode*>& a_out, int a_depth = 0)
		{
			const auto node = a_object && a_depth <= 96 ? a_object->AsNode() : nullptr;
			if (!node) {
				return;
			}
			if (NamedAs(node, a_name)) {
				a_out.push_back(node);
			}
			for (const auto& child : node->GetChildren()) {
				CollectNamed(child.get(), a_name, a_out, a_depth + 1);
			}
		}

		std::string FingerName(bool a_left, int a_finger)
		{
			const char side = a_left ? 'L' : 'R';
			return std::format("NPC {} Finger{}0 [{}F{}0]", side, a_finger, side, a_finger);
		}

		// The skeleton's hand bone. Worn armor may bring nodes of the skeleton's bone names (nothing under them, never
		// animated), so not just the first node of that name: the one with the fingers under it, else the one holding the
		// weapon node (WEAPON / SHIELD hang under the hands), else the first.
		RE::NiNode* FindHand(RE::NiNode* a_root, bool a_left, bool a_report)
		{
			const char*               handName = a_left ? kLeftHand : kRightHand;
			std::vector<RE::NiNode*>  hands;
			CollectNamed(a_root, handName, hands);
			const auto finger = FingerName(a_left, 2);
			RE::NiNode* chosen = nullptr;
			const char* how = "";
			for (const auto hand : hands) {
				if (FindNamed(hand, finger.c_str())) {
					chosen = hand;
					how = "with its fingers";
					break;
				}
			}
			if (!chosen) {
				if (const auto slot = FindNamed(a_root, a_left ? "SHIELD" : "WEAPON")) {
					for (auto p = slot->parent; p; p = p->parent) {
						if (NamedAs(p, handName)) {
							chosen = p;
							how = "holding the weapon node";
							break;
						}
					}
				}
			}
			if (!chosen && !hands.empty()) {
				chosen = hands.front();
				how = "the first of its name";
			}
			if (a_report) {
				std::string children;
				if (chosen) {
					int n = 0;
					for (const auto& child : chosen->GetChildren()) {
						if (child && n++ < 16) {
							children += std::format("{}'{}'", children.empty() ? "" : ", ", child->name.c_str() ? child->name.c_str() : "");
						}
					}
				}
				logs::info("{} hand bone: {} node(s) named {}, chosen: {} ({}), under it: {}", a_left ? "left" : "right", hands.size(), handName,
					chosen ? "yes" : "none", how, children.empty() ? "nothing" : children);
			}
			return chosen;
		}

		// the knuckle joints of the vanilla skeleton (finger bones' roots, index to little finger, right hand; the left
		// one is its mirror), for hands whose fingers can't be found
		constexpr std::array<std::array<float, 3>, 4> kVanillaKnuckles{ { { 2.54f, -0.11f, 8.39f }, { 0.37f, -0.08f, 8.26f }, { -1.44f, -0.64f, 7.98f },
			{ -3.0f, -1.58f, 7.42f } } };

		// A fist's striking part: the knuckles, in the hand bone's space. The finger bones' roots sit at the knuckle
		// joints and don't move against the hand however the fingers curl; the knuckles' skin is a little beyond them.
		void MeasureFist(Hand& a_h)
		{
			a_h.reach = 0.0f;
			a_h.axis = { 0.0f, 1.0f, 0.0f };
			a_h.fromVertices = false;
			a_h.bladeOk = false;
			a_h.head.clear();
			a_h.edges.clear();
			const auto hand = a_h.node ? a_h.node->AsNode() : nullptr;
			if (!hand) {
				return;
			}
			std::array<Vec, 4> knuckles{};  // index to little finger
			a_h.fromVertices = true;        // "from its fingers"
			for (int f = 1; f <= 4; ++f) {
				const auto      bone = FindNamed(hand, FingerName(a_h.left, f).c_str());
				RE::NiTransform rel;
				if (!bone || !RelativeTo(bone, hand, rel)) {
					a_h.fromVertices = false;
					break;
				}
				knuckles[static_cast<std::size_t>(f - 1)] = rel.translate;
			}
			if (!a_h.fromVertices) {
				for (std::size_t k = 0; k < 4; ++k) {
					const auto& v = kVanillaKnuckles[k];
					knuckles[k] = { a_h.left ? -v[0] : v[0], v[1], v[2] };
				}
			}
			Vec center{};
			for (const auto& k : knuckles) {
				center += k;
			}
			center = center * 0.25f;
			const Vec   forward = Normalized(center, { 0.0f, 1.0f, 0.0f });
			const float skin = std::max(1.0f, knuckles[0].GetDistance(knuckles[3]) * 0.3f);
			for (int i = 0; i <= 15; ++i) {
				const float       t = static_cast<float>(i) / 15.0f * 3.0f;
				const std::size_t seg = std::min<std::size_t>(static_cast<std::size_t>(t), 2);
				const Vec         base = Lerp(knuckles[seg], knuckles[seg + 1], t - static_cast<float>(seg));
				for (const float depth : { 0.4f, 0.75f, 1.1f }) {
					a_h.head.push_back(base + forward * (skin * depth));
				}
			}
			a_h.headCenter = center + forward * (skin * 0.75f);
			const Vec front = center + forward * skin;
			a_h.reach = front.Length();
			a_h.axis = Normalized(front, forward);
			a_h.edges.push_back(front);
			a_h.bladeOk = a_h.reach >= kFistMinReach;
		}

		// what hangs under the weapon node: its first child (the weapon's model)
		const RE::NiAVObject* HeldModel(RE::NiAVObject* a_node)
		{
			const auto node = a_node ? a_node->AsNode() : nullptr;
			if (!node) {
				return nullptr;
			}
			for (const auto& child : node->GetChildren()) {
				if (child) {
					return child.get();
				}
			}
			return nullptr;
		}

		// ---- attacks

		// The swing window, timed like Precision's attack collisions: from the swing event (weaponSwing, else
		// preHitFrame) for 0.3 s (1.8x for power attacks) at normal attack speed; Precision's own annotations
		// (Collision_Add / Collision_Remove) instead when the animation has them. Nothing before the swing event shows:
		// the wind-up never leaves a trail.
		constexpr float kSwingWindow = 0.3f;
		constexpr float kPowerSwingMult = 1.8f;

		// the actor's attack speed (its animation graph's weaponSpeedMult), 1 = normal
		float AttackSpeed(RE::Actor* a_actor)
		{
			float speed = 1.0f;
			if (!a_actor->GetGraphVariableFloat("weaponSpeedMult", speed) || speed <= 0.0f) {
				speed = 1.0f;
			}
			return std::clamp(speed, 0.3f, 3.0f);
		}

		float SwingWindow(const Tracked& a_t, RE::Actor* a_actor)
		{
			return kSwingWindow * (a_t.power ? kPowerSwingMult : 1.0f) / AttackSpeed(a_actor);
		}

		// opens the window from a_lead before now until a_until after now (a_set: exactly then, else at least then)
		void Open(Tracked& a_t, float a_lead, float a_until, bool a_set)
		{
			a_t.sawWindow = true;
			if (now >= a_t.windowUntil) {
				for (auto& h : a_t.hands) {
					for (auto it = h.history.rbegin(); it != h.history.rend() && it->time >= now - a_lead; ++it) {
						if (it->attack == a_t.attack) {
							it->window = true;
						}
					}
				}
			}
			a_t.windowUntil = a_set ? now + a_until : std::max(a_t.windowUntil, now + a_until);
		}

		Vec TipOf(const Hand& a_h, const Sample& a_s) { return a_s.pos + Rotate(a_s.rot, a_h.axis * (a_h.reach * a_s.scale)); }

		// Where the strike that hit at a_anchor began, for a hand: back from the hit along the tip's path, to the
		// pause at the top of the wind-up (the tip slower than 40% of the strike's speed) or to where the path turned
		// back on itself (the wind-up's curve), at most kMaxStrike. Everything before it is the wind-up.
		float StrikeFrom(const Tracked& a_t, const Hand& a_h, float a_anchor, float a_maxBack)
		{
			const auto& hist = a_h.history;
			std::ptrdiff_t i = static_cast<std::ptrdiff_t>(hist.size()) - 1;
			while (i > 0 && hist[static_cast<std::size_t>(i)].time > a_anchor) {
				--i;
			}
			float start = a_anchor;
			float peak = 0.0f;
			float turned = 0.0f;
			Vec   later{};
			bool  haveLater = false;
			for (; i > 0; --i) {
				const auto& b = hist[static_cast<std::size_t>(i)];
				const auto& a = hist[static_cast<std::size_t>(i - 1)];
				if (b.attack != a_t.attack || b.time < a_anchor - a_maxBack) {
					break;
				}
				peak = std::max(peak, b.speed);
				if (peak > a_h.minSpeed * 0.8f && b.speed < peak * 0.4f) {
					break;  // the pause before the strike
				}
				Vec d = TipOf(a_h, b) - TipOf(a_h, a);
				if (d.Unitize() > 1e-3f) {
					if (haveLater) {
						turned += std::acos(std::clamp(d.Dot(later), -1.0f, 1.0f));
						if (turned > kStrikeTurn) {
							break;  // the path curls back: the wind-up
						}
					}
					later = d;
					haveLater = true;
				}
				start = a.time;
			}
			return start;
		}

		// the strike's samples of every hand show, and the window stays open a little after the hit
		void MarkStrike(Tracked& a_t, RE::Actor* a_actor, float a_anchor, const char* a_why)
		{
			const float speed = AttackSpeed(a_actor);
			a_t.sawWindow = true;
			a_t.sawHit = true;
			a_t.swingAt = -1.0f;
			for (auto& h : a_t.hands) {
				const float from = StrikeFrom(a_t, h, a_anchor, kMaxStrike / speed);
				for (auto& smp : h.history) {
					if (smp.attack == a_t.attack && smp.time >= from && smp.time <= a_anchor) {
						smp.window = true;
					}
				}
				if (Debug() && !h.history.empty()) {
					logs::info("{:08X} {} hand: strike from {:.2f} s before the {}", a_t.formID, h.left ? "left" : "right", a_anchor - from, a_why);
				}
			}
			a_t.windowUntil = std::max(a_t.windowUntil, a_anchor + kFollowThrough / speed);
		}

		// the fastest moment of the attack since a_from (for swings whose hit event never comes)
		float FastestSince(const Tracked& a_t, float a_from)
		{
			float best = -1.0f;
			float time = now;
			for (const auto& h : a_t.hands) {
				for (const auto& smp : h.history) {
					if (smp.attack == a_t.attack && smp.time >= a_from && smp.lead && smp.speed > best) {
						best = smp.speed;
						time = smp.time;
					}
				}
			}
			return time;
		}

		void MarkPower(Tracked& a_t, std::string_view a_source)
		{
			if (a_t.power) {
				return;
			}
			a_t.power = true;
			CurrentPast(a_t).power = true;
			if (Debug()) {
				logs::info("{:08X}: power attack ({})", a_t.formID, a_source);
			}
		}

		void EndAttack(Tracked& a_t, const char* a_why)
		{
			if (!a_t.attack) {
				return;
			}
			// what the swing's fastest moment tells about this hand's usual strike speed
			for (auto& h : a_t.hands) {
				float peak = 0.0f;
				for (const auto& s : h.history) {
					if (s.attack == a_t.attack && s.window && s.lead) {
						peak = std::max(peak, s.speed);
					}
				}
				if (peak > h.minSpeed) {
					h.strikeSpeed += (peak - h.strikeSpeed) * 0.35f;
				}
			}
			if (Debug()) {
				logs::info("{:08X}: attack {} ended ({}, {:.2f} s), usual strike speed right {:.0f} left {:.0f}", a_t.formID, a_t.attack, a_why,
					now - a_t.startedAt, a_t.hands[0].strikeSpeed, a_t.hands[1].strikeSpeed);
			}
			a_t.attack = 0;
			a_t.power = false;
			a_t.windowUntil = std::min(a_t.windowUntil, now);
		}

		// the hands an attack data names: "...LeftHand" the left, "...DualWield" both, "...RightHand" the right, else
		// none (0). A bare "Left" is the way the attack goes (attackPowerStartLeft: the power attack while moving left).
		std::uint8_t HandsNamed(const RE::BGSAttackData* a_data)
		{
			const char* name = a_data ? a_data->event.c_str() : nullptr;
			if (!name || !*name) {
				return 0;
			}
			std::string lower(name);
			std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			const auto has = [&](const char* a_word) { return lower.find(a_word) != std::string::npos; };
			if (has("dual")) {
				return 3;
			}
			if (has("lefthand") || has("left_hand") || lower.ends_with("_lh") || lower.ends_with("lh")) {
				return 2;
			}
			if (has("righthand") || has("right_hand") || lower.ends_with("_rh") || lower.ends_with("rh")) {
				return 1;
			}
			// vanilla right hand attacks ("attackStart", "attackPowerStartForward") name no hand: the swing events tell
			return 0;
		}

		// an attack data's name without its hand: "attackPowerStartInPlaceLeftHand" -> "attackpowerstartinplace"
		std::string MoveOf(const RE::BGSAttackData* a_data)
		{
			std::string name = a_data && a_data->event.c_str() ? a_data->event.c_str() : "";
			std::ranges::transform(name, name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			for (const auto word : { "lefthand", "left_hand", "righthand", "right_hand" }) {
				if (const auto at = name.find(word); at != std::string::npos) {
					name.erase(at, std::string_view(word).size());
				}
			}
			for (const auto end : { "_lh", "_rh", "lh", "rh" }) {
				if (name.ends_with(end)) {
					name.resize(name.size() - std::string_view(end).size());
					break;
				}
			}
			return name;
		}

		void UpdateAttack(Tracked& a_t, RE::Actor* a_actor, float a_dt, bool a_drawn)
		{
			const auto signals = Attacks::Take(a_t.formID);
			const auto state = a_actor->AsActorState()->GetAttackState();
			const bool melee = state >= RE::ATTACK_STATE_ENUM::kDraw && state <= RE::ATTACK_STATE_ENUM::kFollowThrough;
			const auto process = a_actor->GetActorRuntimeData().currentProcess;
			const auto high = process ? process->high : nullptr;
			const auto data = high ? high->attackData.get() : nullptr;
			const bool dataPower = data && data->data.flags.all(RE::AttackData::AttackFlag::kPowerAttack);
			if (signals.Any()) {
				a_t.lastEventAt = now;
			}
			// the game's wind-up starting again, or new attack data, in the middle of an attack: the next attack of a
			// combo (chained attacks don't always send start events or leave the attack state in between)
			const bool drawStarted = state == RE::ATTACK_STATE_ENUM::kDraw && a_t.lastState != RE::ATTACK_STATE_ENUM::kDraw;
			const bool dataChanged = melee && data && data != a_t.lastData;
			a_t.lastState = state;
			const auto previousData = a_t.lastData;
			a_t.lastData = data;

			if (a_t.attack) {
				if (signals.stop) {
					EndAttack(a_t, "its end event");
				} else if (!a_drawn) {
					EndAttack(a_t, "weapon sheathed");
				} else if (a_actor->IsDead() || a_actor->AsActorState()->IsStaggered()) {
					EndAttack(a_t, "staggered or dead");
				} else if (a_t.stateSeen && a_t.noneFor > 0.2f) {
					EndAttack(a_t, "attack state over");
				} else if (!a_t.stateSeen && now - std::max(a_t.startedAt, a_t.lastEventAt) > 1.5f) {
					EndAttack(a_t, "no news for 1.5 s");
				} else if (now - a_t.startedAt > 5.0f) {
					EndAttack(a_t, "longer than 5 s");
				}
			}

			const auto start = [&](const char* a_why, const RE::BGSAttackData* a_beginData) {
				a_t.attack = ++attackCounter;
				a_t.power = false;
				a_t.startedAt = now;
				a_t.stateSeen = false;
				a_t.noneFor = 0.0f;
				a_t.sawWindow = false;
				a_t.precision = false;
				a_t.windowParts = 0;
				a_t.swingAt = -1.0f;
				a_t.sawHit = false;
				a_t.nameHands = 0;
				a_t.eventHands = 0;
				a_t.windowUntil = std::min(a_t.windowUntil, now);
				a_t.beginData = a_beginData;
				a_t.sawDraw = false;
				CurrentPast(a_t);
				if (Debug()) {
					logs::info("{:08X}: attack {} began ({}), attack data {} ({})", a_t.formID, a_t.attack, a_why,
						data && data->event.c_str() ? data->event.c_str() : "none", dataPower ? "power" : "not power");
				}
			};
			// The attack data switching to the other hand's version of the same move before the attack struck (For Honor
			// style dual power attacks: attackPowerStartInPlace, then ...LeftHand for the same swing) is one move with
			// both weapons, not the next attack. After a hit it is the next attack of a combo.
			const bool otherHand = dataChanged && a_t.attack && previousData && !a_t.sawHit && MoveOf(previousData) == MoveOf(data);
			if (otherHand) {
				a_t.nameHands = 3;
				if (Debug()) {
					logs::info("{:08X}: attack {} strikes with both hands (its attack data turned to {})", a_t.formID, a_t.attack, data->event.c_str());
				}
			}
			const bool next = a_t.attack && now - a_t.startedAt > 0.12f && (signals.attackStart || drawStarted || (dataChanged && !otherHand));
			const bool begin = a_drawn && (!a_t.attack ? (melee || signals.attackStart) : next);
			if (begin) {
				if (a_t.attack) {
					EndAttack(a_t, "the next attack began");
				}
				start(signals.attackStart ? "start event" : drawStarted ? "the game's wind-up" : dataChanged ? "new attack data" : "attack state",
					dataChanged ? previousData : data);
			}
			if (signals.powerStart) {
				a_t.powerEventAt = now;
			}
			if (signals.attackStart || signals.clipEdge) {
				a_t.edgeAt = now;
			}

			// A framework's chained swings: an attack that never went through the game's own start swings again after its
			// last swing's hit window closed, and its clip ended or the next one began since (MCO recovery, attack start
			// marks). The new swing is the next attack, and it has to show it is a power attack itself (a power event since
			// the last swing, or attack data of its own). Without such a mark it is the next strike of the same clip (the
			// second weapon of a two-weapon power attack) and stays part of its attack.
			const bool swingNow = signals.swing || signals.preHit || signals.hit || signals.collisionAdd;
			if (a_drawn && swingNow && a_t.attack && a_t.sawWindow && !a_t.sawDraw && now >= a_t.windowUntil && a_t.edgeAt >= a_t.windowUntil) {
				const auto  old = a_t.attack;
				const float closed = a_t.windowUntil;
				EndAttack(a_t, "its next swing began");
				start("the next swing of a chain", data);
				for (auto& h : a_t.hands) {
					for (auto& smp : h.history) {
						if (smp.attack == old && smp.time > closed) {
							smp.attack = a_t.attack;
						}
					}
				}
				if (a_t.powerEventAt > closed) {
					MarkPower(a_t, "animation event");
				}
			}
			if (!a_t.attack) {
				return;
			}
			if (state == RE::ATTACK_STATE_ENUM::kDraw) {
				a_t.sawDraw = true;
			}
			// the attack data is this attack's if the attack went through the game's own start, or if it changed since
			// the attack began; else it is a leftover of an earlier attack (maybe a power attack) and says nothing
			const bool dataFresh = data && (a_t.sawDraw || data != a_t.beginData);
			if (signals.powerStart) {
				MarkPower(a_t, "animation event");
			} else if (melee && dataPower && dataFresh) {
				MarkPower(a_t, "attack data");
			}
			if (melee) {
				a_t.stateSeen = true;
				a_t.noneFor = 0.0f;
				if (!a_t.nameHands && dataFresh) {
					a_t.nameHands = HandsNamed(data);
				}
			} else {
				a_t.noneFor += a_dt;
			}
			a_t.eventHands |= (signals.swingRight ? 1 : 0) | (signals.swingLeft ? 2 : 0) | (signals.collisionAdd & 3);
			auto& past = CurrentPast(a_t);
			past.hands = a_t.nameHands ? a_t.nameHands : a_t.eventHands ? a_t.eventHands : 3;
			past.named = a_t.nameHands != 0;

			// the swing window
			if (signals.precision) {
				a_t.precision = true;
			}
			if (a_t.precision) {
				// Each window is on its body part: the hand (or weapon) it names strikes, whatever the attack data's name
				// says; a window on a foot (a kick) leaves the hands out. A fist's punch is traced back from the window's
				// start, which some movesets put near the impact.
				if (signals.collisionAdd) {
					if (now >= a_t.windowUntil) {
						a_t.windowParts = 0;
					}
					a_t.windowParts |= signals.collisionAdd;
					Open(a_t, 0.02f, signals.collisionDuration > 0.0f ? signals.collisionDuration : 3.0f, false);
					const float speed = AttackSpeed(a_actor);
					for (auto& h : a_t.hands) {
						const std::uint8_t part = h.left ? 2 : 1;
						const float        from = h.fist && (signals.collisionAdd & part) ? StrikeFrom(a_t, h, now, kMaxStrike / speed) : now - 0.02f;
						for (auto it = h.history.rbegin(); it != h.history.rend() && it->time >= from; ++it) {
							if (it->attack == a_t.attack) {
								it->window = true;
								it->parts |= signals.collisionAdd;
							}
						}
					}
					if (Debug()) {
						logs::info("{:08X}: Precision hit window opens on {}{}{} ({:.2f} s)", a_t.formID, signals.collisionAdd & 1 ? "right " : "",
							signals.collisionAdd & 2 ? "left " : "", signals.collisionAdd & 4 ? "another part " : "", signals.collisionDuration);
					}
				}
				if (signals.collisionRemove) {
					a_t.windowParts &= static_cast<std::uint8_t>(~signals.collisionRemoved);
					if (!a_t.windowParts) {
						a_t.windowUntil = std::min(a_t.windowUntil, now + 0.03f);
					}
				}
				return;
			}
			// The strike shows, not the wind-up: the swing event (weaponSwing, preHitFrame) only says one is coming; the
			// hit event (HitFrame) tells where it struck, and the strike is traced back from there (its particles laid
			// with their age, so they look as if they followed the blade). After the hit it goes on live for a moment.
			if ((signals.swing || signals.preHit) && a_t.swingAt < 0.0f) {
				a_t.swingAt = now;
			}
			if (signals.hit) {
				MarkStrike(a_t, a_actor, now, "hit");
			} else if (a_t.swingAt >= 0.0f && now - a_t.swingAt > SwingWindow(a_t, a_actor) + 0.05f) {
				// a swing whose hit event never came (some custom animations): the strike is the fastest moment
				MarkStrike(a_t, a_actor, FastestSince(a_t, a_t.swingAt), "fastest moment (no hit event)");
			}
		}

		// ---- samples

		// The VR player's weapon node: WEAPON / SHIELD of the VR body (first person skeleton) holding a model, else of the
		// third person one (body mods may move the weapon there), else anywhere under the VR rig.
		RE::NiAVObject* VrWeaponNode(RE::Actor* a_actor, bool a_left)
		{
			const char* name = a_left ? "SHIELD" : "WEAPON";
			RE::NiAVObject* first = nullptr;
			for (const bool firstPerson : { true, false }) {
				const auto root = a_actor->Get3D(firstPerson);
				const auto node = root ? FindNamed(root, name) : nullptr;
				if (node && HeldModel(node)) {
					return node;
				}
				if (!first) {
					first = node;
				}
			}
			const auto player = a_actor->IsPlayerRef() ? static_cast<RE::PlayerCharacter*>(a_actor) : nullptr;
			const auto vr = player ? player->GetVRNodeData() : nullptr;
			if (vr && vr->PlayerWorldNode) {
				if (const auto node = FindNamed(vr->PlayerWorldNode.get(), name); node && HeldModel(node)) {
					return node;
				}
			}
			return first;
		}

		// a fist's shoulder: the upper arm above the hand bone (walked up from it, so armor's copies of the bone names
		// can't stand in), else the hand's grandparent
		const RE::NiAVObject* ShoulderOf(const RE::NiAVObject* a_hand, bool a_left)
		{
			const char* name = a_left ? "NPC L UpperArm [LUar]" : "NPC R UpperArm [RUar]";
			int         depth = 0;
			for (auto p = a_hand ? a_hand->parent : nullptr; p && depth < 8; p = p->parent, ++depth) {
				if (NamedAs(p, name)) {
					return p;
				}
			}
			return a_hand && a_hand->parent ? a_hand->parent->parent : nullptr;
		}

		// the VR player's hand: the one the game moves with the controller
		RE::NiNode* VrHand(RE::Actor* a_actor, RE::NiNode* a_root, bool a_left)
		{
			const auto player = a_actor->IsPlayerRef() ? static_cast<RE::PlayerCharacter*>(a_actor) : nullptr;
			const auto vr = player ? player->GetVRNodeData() : nullptr;
			if (vr) {
				if (const auto hand = a_left ? vr->NPCLHnd.get() : vr->NPCRHnd.get()) {
					return hand;
				}
			}
			return FindHand(a_root, a_left, Debug());
		}

		void SampleHand(Tracked& a_t, Hand& a_h, RE::Actor* a_actor, bool a_drawn)
		{
			const auto form = a_actor->GetEquippedObject(a_h.left);
			const auto weapon = form ? form->As<RE::TESObjectWEAP>() : nullptr;
			// a two-handed weapon (or a bow) is "equipped" in both hands: only the right one holds it
			const auto type = weapon ? weapon->GetWeaponType() : RE::WEAPON_TYPE::kHandToHandMelee;
			const bool twoHanded = weapon && (type == RE::WEAPON_TYPE::kTwoHandSword || type == RE::WEAPON_TYPE::kTwoHandAxe || type == RE::WEAPON_TYPE::kBow ||
												 type == RE::WEAPON_TYPE::kCrossbow);
			// a fist: nothing in this hand and no weapon in the other (a sword in the right leaves the left out of it)
			const bool fist = tuning.fists && IsUnarmed(form) && !HoldsWeapon(a_actor->GetEquippedObject(!a_h.left)) && CanPunch(a_t, a_actor);
			if (fist != a_h.fist) {
				a_h.fist = fist;
				a_h.minSpeed = fist ? kFistMinSpeed : kMinSpeed;
				a_h.strikeSpeed = fist ? kFistFirstStrike : kFirstStrike;
				a_h.node = nullptr;
				a_h.held = nullptr;
				a_h.bladeOk = false;
				a_h.elementCheck = 0.0f;
				a_h.history.clear();
			}
			const auto looked = fist || (a_h.left && twoHanded) ? nullptr : weapon;
			if (looked != a_h.looked) {
				a_h.looked = looked;
				a_h.unique = IsUnique(looked);
				a_h.elementCheck = 0.0f;
			}
			if (fist) {
				CheckGauntlets(a_t, a_actor);
				if (a_h.enchantment != a_t.gauntletEnchantment) {
					a_h.elementCheck = 0.0f;
				}
			}
			if (a_h.tuned != tuningRevision) {
				a_h.tuned = tuningRevision;
				a_h.elementCheck = 0.0f;
			}
			if (now >= a_h.elementCheck) {
				a_h.elementCheck = now + 1.0f;
				a_h.enchantment = fist ? a_t.gauntletEnchantment : looked ? EnchantmentOf(a_actor, a_h.left, looked) : nullptr;
				a_h.element = ElementOf(a_actor, looked, a_h.enchantment);
				a_h.tint = TintOf(a_h.enchantment);
			}
			Look(a_h);
			if (!a_drawn || !a_t.root || !(fist || (IsMelee(weapon) && !(a_h.left && twoHanded)))) {
				a_h.weapon = weapon;
				a_h.held = nullptr;
				a_h.bladeOk = false;
				a_h.history.clear();
				return;
			}
			if (a_t.vr) {
				// looked for again while it holds nothing (the model may hang elsewhere), a few times a second
				const bool lost = !a_h.node || a_h.node->parent == nullptr || (!fist && !HeldModel(a_h.node));
				if (lost && now >= a_h.findAgain) {
					a_h.findAgain = now + 0.5f;
					const auto before = a_h.node;
					a_h.node = fist ? VrHand(a_actor, a_t.root, a_h.left) : VrWeaponNode(a_actor, a_h.left);
					if (Debug() || a_h.node != before) {
						const auto parent = a_h.node ? a_h.node->parent : nullptr;
						const auto model = !fist && a_h.node ? HeldModel(a_h.node) : nullptr;
						logs::info("VR {} hand: {} {} (under '{}'){}", a_h.left ? "left" : "right", fist ? "hand bone" : "weapon node", a_h.node ? "found" : "not found",
							parent && parent->name.c_str() ? parent->name.c_str() : "",
							fist ? std::string{} : model ? std::format(", holds '{}'", model->name.c_str() ? model->name.c_str() : "") : std::string(", holds nothing"));
					}
				}
			} else if (!a_h.node || a_h.node->parent == nullptr) {
				if (!fist) {
					a_h.node = a_t.root->GetObjectByName(a_h.left ? "SHIELD" : "WEAPON");
				} else if (now >= a_h.measureAgain) {
					a_h.node = FindHand(a_t.root, a_h.left, Debug());  // a search of the whole 3D: not every frame when it fails
					if (!a_h.node) {
						a_h.measureAgain = now + 1.0f;
						if (Debug() || a_t.player) {
							logs::info("{:08X} {} hand: no hand bone in its skeleton, no fist trail", a_t.formID, a_h.left ? "left" : "right");
						}
					}
				}
			}
			const auto held = fist ? a_h.node : HeldModel(a_h.node);
			if (!held) {
				a_h.bladeOk = false;
				a_h.history.clear();
				return;
			}
			// a model just attached may not have its shapes yet (it measures 0): measured again until it does
			const bool retry = !a_h.bladeOk && now >= a_h.measureAgain;
			if (held != a_h.held || weapon != a_h.weapon || retry) {
				a_h.measureAgain = now + 0.5f;
				const bool quiet = retry && held == a_h.held && weapon == a_h.weapon;
				if (weapon != a_h.weapon) {
					a_h.strikeSpeed = fist ? kFistFirstStrike : kFirstStrike;
				}
				a_h.held = held;
				a_h.weapon = weapon;
				a_h.heavy = !fist && IsHeavy(weapon);
				a_h.history.clear();
				if (fist) {
					MeasureFist(a_h);
				} else {
					MeasureWeapon(a_h);
				}
				if ((Debug() || a_t.player) && (!quiet || a_h.bladeOk)) {
					logs::info("{:08X} {} hand: {} reaches {:.0f} units (from its {}), {} {} head points{}", a_t.formID, a_h.left ? "left" : "right",
						fist ? "the fist" : weapon->GetName(), a_h.reach, fist ? (a_h.fromVertices ? "fingers" : "usual knuckle places") : a_h.fromVertices ? "vertices" : "bounds",
						fist ? "knuckles," : a_h.heavy ? "heavy head," : "blade,", a_h.head.size(), a_h.bladeOk ? "" : ": too short, no trail");
				}
			}
			if (!a_h.bladeOk) {
				return;
			}
			RE::NiTransform rel;
			if (a_t.vr) {
				// VR places the hands from the controllers: their world transforms, against the VR body's
				rel = a_t.root->world.Invert() * a_h.node->world;
			} else if (!RelativeTo(a_h.node, a_t.root, rel)) {
				a_h.node = nullptr;
				a_h.history.clear();
				return;
			}
			if (!a_h.history.empty() && now <= a_h.history.back().time) {
				return;  // no time passed (paused)
			}
			Sample s{};
			s.time = now;
			s.pos = rel.translate;
			s.rot = FromMatrix(rel.rotate);
			s.scale = rel.scale;
			s.attack = a_t.attack;
			s.window = now < a_t.windowUntil;
			s.parts = s.window && a_t.precision ? a_t.windowParts : 0;
			s.lead = true;
			s.strong = true;
			const Vec tip = s.pos + Rotate(s.rot, a_h.axis * (a_h.reach * s.scale));
			if (a_h.fist) {
				if (a_h.shoulderOf != a_h.node) {
					a_h.shoulderOf = a_h.node;
					a_h.shoulder = ShoulderOf(a_h.node, a_h.left);
				}
				RE::NiTransform shoulder;
				if (a_h.shoulder && !a_t.vr && RelativeTo(a_h.shoulder, a_t.root, shoulder)) {
					s.shoulderDist = tip.GetDistance(shoulder.translate);
				}
			}
			if (!a_h.history.empty()) {
				const auto& last = a_h.history.back();
				const Vec   lastTip = last.pos + Rotate(last.rot, a_h.axis * (a_h.reach * last.scale));
				// the same pose as last time: the skeleton wasn't animated this frame (busy load orders skip frames).
				// A sample of it would stall the weapon and then jump: wait for the next real pose instead.
				if (tip.GetDistance(lastTip) < 1e-3f && s.pos.GetDistance(last.pos) < 1e-3f && now - last.time < 0.1f) {
					return;
				}
				s.speed = tip.GetDistance(lastTip) / std::max(now - last.time, 1e-4f);
				if (s.shoulderDist > 0.0f && last.shoulderDist > 0.0f) {
					s.out = (s.shoulderDist - last.shoulderDist) / std::max(now - last.time, 1e-4f);
				}
			}
			a_h.history.push_back(s);
			while (!a_h.history.empty() && a_h.history.front().time < now - kHistory) {
				a_h.history.pop_front();
			}
		}

		// with a weapon in each hand: the one that strikes moves much faster than the other
		void CompareHands(Tracked& a_t)
		{
			auto& r = a_t.hands[0];
			auto& l = a_t.hands[1];
			if (r.history.empty() || l.history.empty() || r.history.back().time != now || l.history.back().time != now) {
				return;
			}
			auto& rs = r.history.back();
			auto& ls = l.history.back();
			rs.lead = rs.speed >= ls.speed * kLeadShare;
			ls.lead = ls.speed >= rs.speed * kLeadShare;
			rs.strong = rs.speed >= ls.speed * kPairShare;
			ls.strong = ls.speed >= rs.speed * kPairShare;
		}

		// how much a sample's moment of the swing shows (0..1)
		float Weight(const Tracked& a_t, const Hand& a_h, const Sample& a_s)
		{
			// VR: the player swings the controllers, no attack animation tells a swing: a fast enough swing is one, each
			// hand on its own
			if (a_t.vr && tuning.vrSwings) {
				const float threshold = tuning.vrSwingSpeed * (a_h.fist ? 0.6f : 1.0f);
				return Smoothstep(threshold * 0.6f, threshold, a_s.speed);
			}
			const std::uint8_t part = a_h.left ? 2 : 1;
			if (!a_s.window) {
				return 0.0f;
			}
			if (a_s.parts) {
				if (!(a_s.parts & part)) {
					return 0.0f;  // a Precision window on the other hand or a foot
				}
			} else if (!a_s.lead) {
				return 0.0f;
			}
			const auto past = PastOf(a_t, a_s.attack);
			if (!past || !(tuning.normalAttacks || past->power)) {
				return 0.0f;
			}
			// without a Precision window naming it: the hands the attack names strike; unnamed (combat frameworks name
			// their attacks their own way), the hand of the swing events strikes, and the other one too when it keeps up
			// with it (a swing with both weapons). Fists take names only as a hint (power punches are named "RightHand"
			// whichever hand throws them): the other fist strikes too when it keeps up.
			if (!a_s.parts && !(past->hands & part) && ((past->named && !a_h.fist) || !a_s.strong)) {
				return 0.0f;
			}
			const float threshold = std::max(a_h.minSpeed, a_h.strikeSpeed * kStrikeShare);
			float       w = threshold > 0.0f ? Smoothstep(threshold * 0.6f, threshold, a_s.speed) : 1.0f;
			if (a_h.fist && a_s.shoulderDist > 0.0f) {
				// a punch goes out (or around, a hook); pulling the arm back afterwards leaves nothing
				w *= Smoothstep(-0.5f, -0.15f, a_s.out / std::max(a_s.speed, 1.0f));
			}
			return w;
		}

		// ---- a stretch of the swing between two samples
		//
		// The hand moves on a curve through the samples and the weapon turns smoothly with it: every point of the
		// weapon draws a round arc, however far it moved in one frame. A stretch waits for the sample after it, so the
		// curve bends the way the weapon went on.

		struct Stretch
		{
			const Sample& s0;
			const Sample& a;
			const Sample& b;
			const Sample& s3;
			std::array<Quat, 4> q;

			Stretch(const Sample& a_s0, const Sample& a_a, const Sample& a_b, const Sample& a_s3) :
				s0(a_s0), a(a_a), b(a_b), s3(a_s3)
			{
				// the rotations on one side of the sphere, so the curve takes the short way
				q[1] = a.rot;
				q[0] = Dot(s0.rot, q[1]) < 0.0f ? Negated(s0.rot) : s0.rot;
				q[2] = Dot(b.rot, q[1]) < 0.0f ? Negated(b.rot) : b.rot;
				q[3] = Dot(s3.rot, q[2]) < 0.0f ? Negated(s3.rot) : s3.rot;
			}

			float Span() const { return std::max(b.time - a.time, 1e-4f); }
			float TimeAt(float a_f) const { return a.time + a_f * Span(); }

			float H(float a_p0, float a_p1, float a_p2, float a_p3, float a_f) const
			{
				return Hermite(a_p0, a_p1, a_p2, a_p3, s0.time, a.time, b.time, s3.time, a_f);
			}

			// a point of the weapon (its node's space) at a share of the stretch, in the root's space
			Vec At(const Vec& a_point, float a_f) const
			{
				const Vec hand{ H(s0.pos.x, a.pos.x, b.pos.x, s3.pos.x, a_f), H(s0.pos.y, a.pos.y, b.pos.y, s3.pos.y, a_f), H(s0.pos.z, a.pos.z, b.pos.z, s3.pos.z, a_f) };
				Quat        r{ H(q[0].w, q[1].w, q[2].w, q[3].w, a_f), H(q[0].x, q[1].x, q[2].x, q[3].x, a_f), H(q[0].y, q[1].y, q[2].y, q[3].y, a_f),
                    H(q[0].z, q[1].z, q[2].z, q[3].z, a_f) };
				const float n = std::sqrt(Dot(r, r));
				r = n > 1e-6f ? Quat{ r.w / n, r.x / n, r.y / n, r.z / n } : a.rot;
				const float scale = a.scale + (b.scale - a.scale) * a_f;
				return hand + Rotate(r, a_point * scale);
			}

			// its velocity there, per second, in the root's space
			Vec VelocityAt(const Vec& a_point, float a_f) const
			{
				const float f0 = std::max(a_f - 0.05f, 0.0f);
				const float f1 = std::min(a_f + 0.05f, 1.0f);
				return (At(a_point, f1) - At(a_point, f0)) * (1.0f / ((f1 - f0) * Span()));
			}
		};

		// ---- particles

		constexpr float kSparkStep = 1.0f / 60.0f;

		void Advance(Spark& a_s, float a_dt)
		{
			while (a_dt > 0.0f && a_s.alive) {
				const float dt = std::min(a_dt, kSparkStep);
				a_dt -= dt;
				a_s.vel *= std::exp(-a_s.drag * dt);
				a_s.vel.z -= a_s.gravity * dt;
				a_s.pos += a_s.vel * dt;
				a_s.age += dt;
				if (a_s.turns) {
					const float speed = a_s.vel.Length();
					if (speed > 15.0f) {
						Vec dir = a_s.dir + a_s.vel * (std::min(1.0f, dt * 10.0f) / speed);
						if (dir.Unitize() > 1e-4f) {
							a_s.dir = dir;
						}
					}
				}
				if (a_s.age >= a_s.life) {
					a_s.alive = false;
				}
			}
		}

		// a free particle of the hand's color: in a batch it holds already, else in a new one; nullptr when every
		// batch is in use
		Spark* NewSpark(RE::NiNode* a_root, Hand& a_h, bool a_dot)
		{
			const auto freeSlot = [&](Batch& a_b) -> Spark* {
				if (a_b.used >= kPerBatch) {
					return nullptr;
				}
				std::size_t other = kPerBatch;
				for (std::size_t n = 0; n < kPerBatch; ++n) {
					const std::size_t i = (a_b.cursor + n) % kPerBatch;
					if (a_b.sparks[i].alive) {
						continue;
					}
					if (IsDotSlot(i) == a_dot) {
						a_b.cursor = i + 1;
						return &a_b.sparks[i];
					}
					if (other == kPerBatch) {
						other = i;
					}
				}
				return other < kPerBatch ? &a_b.sparks[other] : nullptr;  // none of the wanted shape left: the other shape
			};
			Spark* sp = nullptr;
			for (auto it = a_h.batches.rbegin(); it != a_h.batches.rend() && !sp; ++it) {
				if ((*it)->color == a_h.color) {
					sp = freeSlot(**it);
				}
			}
			if (!sp) {
				const bool solid = Solid(a_h.color);
				auto*      b = Take(batchPools[solid ? 1 : 0], kMaxBatches, [solid] { return MakeBatch(solid); }, a_root);
				if (!b) {
					return nullptr;
				}
				b->color = a_h.color;
				b->used = 0;
				b->cursor = 0;
				a_h.batches.push_back(b);
				sp = freeSlot(*b);
			}
			if (!sp) {
				return nullptr;
			}
			for (auto* b : a_h.batches) {
				if (sp >= b->sparks.data() && sp < b->sparks.data() + kPerBatch) {
					++b->used;
					break;
				}
			}
			*sp = Spark{};
			sp->alive = true;
			sp->seed = Random(0.0f, 6.2832f);
			return sp;
		}

		// where the trails hang: under the skeleton root, for the VR player under its VR rig
		RE::NiNode* DrawRoot(const Tracked& a_t)
		{
			if (a_t.vr) {
				const auto player = RE::PlayerCharacter::GetSingleton();
				const auto vr = player ? player->GetVRNodeData() : nullptr;
				if (vr && vr->PlayerWorldNode) {
					return vr->PlayerWorldNode.get();
				}
			}
			return a_t.root;
		}

		// ---- lines

		Line* LineFor(RE::NiNode* a_root, Hand& a_h, std::uint32_t a_attack, float a_time)
		{
			if (a_h.current && (a_h.currentAttack != a_attack || a_time - a_h.lastLaid > 0.12f || a_h.current->color != a_h.color)) {
				a_h.current->open = false;
				a_h.current = nullptr;
			}
			if (!a_h.current) {
				const bool solid = Solid(a_h.color);
				auto*      l = Take(linePools[solid ? 1 : 0], kMaxLines, [solid] { return MakeLine(solid); }, a_root);
				if (!l) {
					return nullptr;
				}
				l->points.clear();
				l->open = true;
				l->color = a_h.color;
				a_h.lines.push_back(l);
				a_h.current = l;
				a_h.currentAttack = a_attack;
			}
			return a_h.current;
		}

		void AddLinePoint(Line& a_l, const Vec& a_pos, float a_time, float a_weight)
		{
			a_l.points.push_back({ a_pos, a_time, a_weight });
			auto& pts = a_l.points;
			if (pts.size() > kLinePoints) {
				// every other point of the older half goes: the line keeps its whole length, a little coarser
				const std::size_t half = pts.size() / 2;
				std::size_t       out = 0;
				for (std::size_t i = 0; i < pts.size(); ++i) {
					if (i >= half || i % 2 == 0) {
						pts[out++] = pts[i];
					}
				}
				pts.resize(out);
			}
		}

		// ---- what a stretch of a visible swing lays

		// the edge a swing leads with: for blades their tip; for heavy heads the extreme that moves first into the
		// swing (an axe's blade, a hammer's face)
		std::size_t LeadingEdge(const Hand& a_h, const Stretch& a_s)
		{
			if (!a_h.heavy || a_h.edges.size() < 2) {
				return 0;
			}
			const Vec   center = a_s.At(a_h.headCenter, 1.0f);
			const Vec   motion = Normalized(a_s.VelocityAt(a_h.headCenter, 1.0f), { 0.0f, 0.0f, 1.0f });
			std::size_t best = 0;
			float       most = -1e9f;
			for (std::size_t e = 1; e < a_h.edges.size(); ++e) {
				const float lead = (a_s.At(a_h.edges[e], 1.0f) - center).Dot(motion);
				if (lead > most) {
					most = lead;
					best = e;
				}
			}
			return best;
		}

		void Lay(Tracked& a_t, Hand& a_h, const RE::NiTransform& a_root)
		{
			auto&       hist = a_h.history;
			const float amount = tuning.sparkAmount * (a_h.fist ? kFistDensity : 1.0f);
			const float size = tuning.sparkSize;
			const float duration = tuning.sparkLife;
			const float spread = tuning.sparkSpread;
			const float share = a_h.fist ? kFistHead : a_h.heavy ? kHeavyHead : kBladeHead;
			for (std::size_t i = 1; i + 1 < hist.size(); ++i) {
				auto&       b = hist[i];
				const auto& a = hist[i - 1];
				if (b.emitted || now - b.time > 0.8f) {
					continue;
				}
				const float w = 0.5f * (Weight(a_t, a_h, a) + Weight(a_t, a_h, b));
				if (w <= 0.02f) {
					continue;
				}
				b.emitted = true;
				const Stretch stretch(hist[i >= 2 ? i - 2 : i - 1], a, b, hist[i + 1]);

				// a new swing (or a long pause in this one) starts a new line and picks its leading edge
				const bool fresh = a_h.currentAttack != b.attack || a.time - a_h.lastLaid > 0.12f;
				if (fresh) {
					a_h.edge = LeadingEdge(a_h, stretch);
				}
				const Vec edge = a_h.edges.empty() ? a_h.axis * a_h.reach : a_h.edges[std::min(a_h.edge, a_h.edges.size() - 1)];

				const auto world = [&](const Vec& a_p) { return a_root * a_p; };
				const auto worldVel = [&](const Vec& a_v) { return (a_root.rotate * a_v) * a_root.scale; };
				const Vec   out = Normalized(a_root.rotate * Rotate(b.rot, Normalized(edge, a_h.axis)), { 0.0f, 0.0f, 1.0f });  // hand to edge, world
				const float travel = stretch.At(edge, 0.0f).GetDistance(stretch.At(edge, 1.0f)) * a_root.scale;

				// the thin line along the leading edge
				const auto drawRoot = DrawRoot(a_t);
				if (tuning.line) {
					if (auto* line = LineFor(drawRoot, a_h, b.attack, a.time)) {
						if (line->points.empty()) {
							AddLinePoint(*line, world(stretch.At(edge, 0.0f)), a.time, w);
						}
						// a point every few units of the edge's path (fast parts of the swing get as many as slow ones)
						const int steps = std::clamp(static_cast<int>(std::ceil(travel / kLineStep)), 2, 24);
						for (int k = 1; k <= steps; ++k) {
							const float f = static_cast<float>(k) / static_cast<float>(steps);
							AddLinePoint(*line, world(stretch.At(edge, f)), stretch.TimeAt(f), w);
						}
					}
				}
				a_h.lastLaid = b.time;
				a_h.currentAttack = b.attack;
				if (!tuning.line && a_h.current) {
					a_h.current->open = false;
					a_h.current = nullptr;
				}
				if (!tuning.sparks) {
					continue;
				}

				int        budget = 300;
				const auto count = [&](float a_expected) {
					int n = static_cast<int>(a_expected);
					if (Random(0.0f, 1.0f) < a_expected - static_cast<float>(n)) {
						++n;
					}
					n = std::min(n, budget);
					budget -= n;
					return n;
				};
				// a new particle at a point of the weapon, a random moment of the stretch
				const auto spawn = [&](const Vec& a_point, bool a_dot) -> Spark* {
					auto* sp = NewSpark(drawRoot, a_h, a_dot);
					if (!sp) {
						return nullptr;
					}
					const float f = Random(0.0f, 1.0f);
					sp->pos = world(stretch.At(a_point, f));
					sp->vel = worldVel(stretch.VelocityAt(a_point, f));  // the weapon's, for now
					sp->dir = Normalized(sp->vel, out);
					sp->age = -(now - stretch.TimeAt(f));               // how long ago it was laid, to catch up below
					return sp;
				};
				const auto settle = [&](Spark& a_sp) {
					const float ago = -a_sp.age;
					a_sp.age = 0.0f;
					Advance(a_sp, ago);
				};
				const auto randomHead = [&]() -> const Vec& { return a_h.head[static_cast<std::size_t>(Random(0.0f, 0.999f) * static_cast<float>(a_h.head.size()))]; };

				// the edge: long dashes packed along the leading edge's path
				for (int j = count(travel / 4.0f * amount * w); j > 0; --j) {
					if (auto* sp = spawn(edge + RandomUnit() * Random(0.0f, 0.6f), false)) {
						const Vec bladeVel = sp->vel;
						sp->vel = bladeVel * Random(0.0f, 0.03f) + out * (Random(3.0f, 18.0f) * spread) + RandomUnit() * (Random(0.0f, 6.0f) * spread);
						sp->size = Random(6.0f, 11.0f) * size;
						sp->life = Random(0.8f, 1.5f) * duration;
						sp->drag = 1.2f;
						sp->gravity = 4.0f;
						settle(*sp);
					}
				}
				// the band: dashes crowded over the part that strikes
				for (int j = count(travel / 1.6f * amount * w); j > 0; --j) {
					const bool longDash = Random(0.0f, 1.0f) < 0.6f;
					if (auto* sp = spawn(randomHead(), false)) {
						const Vec bladeVel = sp->vel;
						sp->vel = bladeVel * Random(0.0f, 0.04f) + out * (Random(4.0f, 26.0f) * spread) + RandomUnit() * (Random(0.0f, 10.0f) * spread);
						sp->size = (longDash ? Random(5.0f, 12.0f) : Random(3.0f, 6.0f)) * size;
						sp->life = Random(0.7f, 1.4f) * duration;
						sp->drag = 1.2f;
						sp->gravity = 6.0f;
						sp->twinkle = Random(0.0f, 1.0f) < 0.3f;
						settle(*sp);
					}
				}
				// dots scattered over the striking part and a little further in
				for (int j = count(travel / 12.0f * amount * w); j > 0; --j) {
					const Vec point = randomHead() * (1.0f - std::pow(Random(0.0f, 1.0f), 1.5f) * share);
					if (auto* sp = spawn(point, true)) {
						sp->vel = out * (Random(6.0f, 40.0f) * spread) + RandomUnit() * (Random(0.0f, 18.0f) * spread);
						sp->size = Random(1.2f, 2.6f) * size;
						sp->life = Random(0.6f, 1.3f) * duration;
						sp->drag = 1.5f;
						sp->gravity = 10.0f;
						sp->twinkle = true;
						settle(*sp);
					}
				}
				// sparks flying off it
				for (int j = count(travel / 20.0f * tuning.flyingSparks * w); j > 0; --j) {
					const bool dot = Random(0.0f, 1.0f) < 0.5f;
					if (auto* sp = spawn(Random(0.0f, 1.0f) < 0.5f ? edge : randomHead(), dot)) {
						const Vec bladeVel = sp->vel;
						sp->vel = bladeVel * Random(0.1f, 0.35f) + RandomUnit() * (Random(80.0f, 260.0f) * spread) + Vec{ 0.0f, 0.0f, Random(0.0f, 60.0f) * spread };
						sp->dir = Normalized(sp->vel, out);
						sp->size = (dot ? Random(1.2f, 2.0f) : Random(4.0f, 8.0f)) * size;
						sp->life = Random(0.3f, 0.7f) * duration;
						sp->drag = 3.5f;
						sp->gravity = 140.0f;
						sp->twinkle = true;
						sp->turns = true;
						settle(*sp);
					}
				}
			}
			// the swing went on without laying anything for a moment: its line is complete
			if (a_h.current && now - a_h.lastLaid > 0.15f) {
				a_h.current->open = false;
				a_h.current = nullptr;
			}
		}

		// ---- drawing

		void SetBatchColors(Batch& a_b)
		{
			const auto& c = a_b.color;
			// glowing: hotter (whiter) than the color, each shape a little brighter or dimmer; solid: the exact color
			const Vec   hot = Mix(c, a_b.solid ? 0.0f : kWhiteCore * 0.6f);
			const float glow = tuning.brightness * tuning.intensity;
			for (std::size_t s = 0; s < kShapes; ++s) {
				if (const auto m = a_b.materials[s]) {
					m->baseColor = { hot.x, hot.y, hot.z, tuning.opacity };
					m->baseColorScale = a_b.solid ? 1.0f : glow * kShapeBrightness[s] * 1.3f;
				}
			}
		}

		// steps the batch's particles; false when all burned out
		bool StepBatch(Batch& a_b, float a_dt)
		{
			std::size_t used = 0;
			for (auto& sp : a_b.sparks) {
				if (sp.alive) {
					Advance(sp, a_dt);
					used += sp.alive ? 1 : 0;
				}
			}
			a_b.used = used;
			return used > 0;
		}

		// the particles' bones: each quad centered on its particle, its length along the way it points, its width
		// across that, facing the camera
		void PlaceBatch(Batch& a_b, const RE::NiTransform& a_toRoot, const Vec& a_camera)
		{
			for (std::size_t s = 0; s < kShapes; ++s) {
				bool any = false;
				for (std::size_t k = 0; k < kPerShape; ++k) {
					const auto& sp = a_b.sparks[s * kPerShape + k];
					auto*       bone = a_b.bones[s][k];
					if (!sp.alive) {
						bone->local = RE::NiTransform{};
						bone->local.scale = 1e-3f;
						continue;
					}
					any = true;
					const float t = sp.age / std::max(sp.life, 1e-3f);
					const float fade = t < 0.4f ? 1.0f : std::max(0.0f, 1.0f - std::pow((t - 0.4f) / 0.6f, 1.4f));
					const float twinkle = sp.twinkle ? 0.6f + 0.4f * std::sin(sp.seed + sp.age * 36.0f) : 1.0f;
					const Vec   x = sp.dir;
					Vec         y = x.Cross(a_camera - sp.pos);
					if (y.Unitize() < 1e-4f) {
						y = Normalized(x.Cross(Vec{ 0.0f, 0.0f, 1.0f }), Vec{ 1.0f, 0.0f, 0.0f });
					}
					RE::NiTransform world;
					world.rotate = Columns(x, y, x.Cross(y));
					world.translate = sp.pos;
					world.scale = std::max(sp.size * fade * twinkle, 0.02f);
					bone->local = a_toRoot * world;
				}
				a_b.shapes[s]->SetAppCulled(!any);
			}
			SetBatchColors(a_b);
			a_b.node->local = RE::NiTransform{};
			RE::NiUpdateData update{};
			a_b.node->Update(update);
		}

		// The line's bones spread evenly over its path, a thin strip facing the camera. Each stretch stays for a while
		// and thins out with its age, so the line fades from where it began, like the particles. False when gone.
		bool PlaceLine(Line& a_l, const RE::NiTransform& a_toRoot, const Vec& a_camera)
		{
			const float life = 0.9f * tuning.sparkLife;
			const auto faded = std::ranges::find_if(a_l.points, [&](const LinePoint& a_p) { return now - a_p.time <= life; });
			a_l.points.erase(a_l.points.begin(), faded);
			const std::size_t n = a_l.points.size();
			if (n < 2) {
				a_l.shape->SetAppCulled(true);
				return a_l.open;
			}
			a_l.shape->SetAppCulled(false);
			const auto& pts = a_l.points;
			// the length along the path up to each point: the bones are spread evenly over the length (not over the
			// points, which crowd where the swing is slow and leave long straight chords where it is fast)
			static thread_local std::vector<float> length;
			length.resize(n);
			length[0] = 0.0f;
			for (std::size_t j = 1; j < n; ++j) {
				length[j] = length[j - 1] + pts[j].pos.GetDistance(pts[j - 1].pos);
			}
			const float total = length[n - 1];
			std::size_t j = 0;
			for (std::size_t i = 0; i < kLineBones; ++i) {
				const float at = total * static_cast<float>(i) / static_cast<float>(kLineBones - 1);
				while (j + 2 < n && length[j + 1] < at) {
					++j;
				}
				const float span = length[j + 1] - length[j];
				const float f = span > 1e-5f ? std::clamp((at - length[j]) / span, 0.0f, 1.0f) : 0.0f;
				const auto& p0 = pts[j];
				const auto& p1 = pts[j + 1];
				// a curve through the points (Hermite, slopes from the neighbours weighted by the distances, so uneven
				// spacing doesn't overshoot): round where the path bends, no corners
				const Vec&  q0 = j > 0 ? pts[j - 1].pos : p0.pos;
				const Vec&  q3 = j + 2 < n ? pts[j + 2].pos : p1.pos;
				const float d0 = j > 0 ? length[j] - length[j - 1] : span;
				const float d2 = j + 2 < n ? length[j + 2] - length[j + 1] : span;
				const Vec   m0 = (p1.pos - q0) * (span / std::max(d0 + span, 1e-5f));
				const Vec   m1 = (q3 - p0.pos) * (span / std::max(span + d2, 1e-5f));
				const float f2 = f * f, f3 = f2 * f;
				const Vec   pos = p0.pos * (2.0f * f3 - 3.0f * f2 + 1.0f) + m0 * (f3 - 2.0f * f2 + f) + p1.pos * (-2.0f * f3 + 3.0f * f2) + m1 * (f3 - f2);
				const Vec   slope = p0.pos * (6.0f * f2 - 6.0f * f) + m0 * (3.0f * f2 - 4.0f * f + 1.0f) + p1.pos * (-6.0f * f2 + 6.0f * f) + m1 * (3.0f * f2 - 2.0f * f);
				const float age = now - (p0.time + (p1.time - p0.time) * f);
				const float weight = std::min(p0.weight + (p1.weight - p0.weight) * f, 1.0f);
				const Vec   along = Normalized(slope, Normalized(p1.pos - p0.pos, { 0.0f, 0.0f, 1.0f }));
				Vec         side = along.Cross(a_camera - pos);
				if (side.Unitize() < 1e-4f) {
					side = Normalized(along.Cross(Vec{ 0.0f, 0.0f, 1.0f }), Vec{ 1.0f, 0.0f, 0.0f });
				}
				const float ageFade = age < 0.4f * life ? 1.0f : std::max(0.0f, 1.0f - (age - 0.4f * life) / (0.6f * life));
				const float ends = 0.3f + 0.7f * std::min(1.0f, static_cast<float>(std::min(i, kLineBones - 1 - i)) / 5.0f);
				RE::NiTransform world;
				world.rotate = Columns(side, along, side.Cross(along));
				world.translate = pos;
				world.scale = std::max(kLineHalfWidth * ageFade * ends * weight, 1e-3f);
				a_l.bones[i]->local = a_toRoot * world;
			}
			const auto& c = a_l.color;
			const Vec   hot = Mix(c, a_l.solid ? 0.0f : kLineWhite);
			a_l.material->baseColor = { hot.x, hot.y, hot.z, tuning.opacity };
			a_l.material->baseColorScale = a_l.solid ? 1.0f : tuning.brightness * tuning.intensity;
			a_l.node->local = RE::NiTransform{};
			RE::NiUpdateData update{};
			a_l.node->Update(update);
			return true;
		}

		// last frame's particles move on; burned out batches go back
		void StepParticles(Hand& a_h, float a_dt)
		{
			for (auto it = a_h.batches.begin(); it != a_h.batches.end();) {
				if (!StepBatch(**it, a_dt)) {
					GiveBack(*it);
					it = a_h.batches.erase(it);
				} else {
					++it;
				}
			}
		}

		// the particles and lines placed for this frame; faded lines go back
		void DrawParticles(Hand& a_h, const RE::NiTransform& a_rootWorld)
		{
			if (a_h.batches.empty() && a_h.lines.empty()) {
				return;
			}
			const auto toRoot = a_rootWorld.Invert();
			const auto camera = RE::PlayerCamera::GetSingleton();
			const Vec  eye = camera && camera->cameraRoot ? camera->cameraRoot->world.translate : a_rootWorld.translate;
			for (auto* b : a_h.batches) {
				PlaceBatch(*b, toRoot, eye);
			}
			for (auto it = a_h.lines.begin(); it != a_h.lines.end();) {
				if (!PlaceLine(**it, toRoot, eye)) {
					if (a_h.current == *it) {
						a_h.current = nullptr;
					}
					(*it)->points.clear();
					GiveBack(*it);
					it = a_h.lines.erase(it);
				} else {
					++it;
				}
			}
		}

		void UpdateHand(Tracked& a_t, Hand& a_h, float a_dt)
		{
			if (!a_t.root) {
				ReleaseHand(a_h, false);
				return;
			}
			StepParticles(a_h, a_dt);
			if (a_h.bladeOk && !a_h.head.empty() && a_h.trailOn && a_h.history.size() >= 3) {
				Lay(a_t, a_h, a_t.root->world);  // the samples are against the skeleton root
			}
			DrawParticles(a_h, DrawRoot(a_t)->world);
		}

		// ---- actors

		RE::NiNode* RootOf(RE::Actor* a_actor, bool a_firstPerson)
		{
			const auto root = a_actor->Get3D(a_firstPerson);
			return root ? root->AsNode() : nullptr;
		}

		std::unique_ptr<Tracked> MakeTracked(RE::Actor* a_actor, bool a_player)
		{
			auto t = std::make_unique<Tracked>();
			t->handle = a_actor->GetHandle();
			t->formID = a_actor->GetFormID();
			t->player = a_player;
			t->hands[1].left = true;
			return t;
		}

		void Forget(Tracked& a_t)
		{
			ReleaseHands(a_t, true);
			if (const auto actor = a_t.handle.get()) {
				Attacks::Unwatch(actor.get());
			}
			a_t.watchedRoot = nullptr;
		}

		// how long the bow has been held fully drawn (now, or at the shot just made); -1: no shot
		float HoldOf(const Tracked& a_t)
		{
			if (a_t.bowDrawStart >= 0.0f) {
				return a_t.bowFullAt >= 0.0f ? now - a_t.bowFullAt : std::max(0.0f, now - a_t.bowDrawStart - kDrawTime);
			}
			return now - a_t.releasedAt <= kReleaseWindow ? a_t.lastHold : -1.0f;
		}

		void TrackBow(Tracked& a_t, RE::Actor* a_actor)
		{
			using State = RE::ATTACK_STATE_ENUM;
			const auto state = a_actor->AsActorState()->GetAttackState();
			bool       drawing = state == State::kBowDraw || state == State::kBowAttached || state == State::kBowDrawn;
			bool       full = state == State::kBowDrawn;
			// VR: the string is pulled by hand; the game keeps how far (0..1)
			if (a_t.vr) {
				const auto vr = static_cast<RE::PlayerCharacter*>(a_actor)->GetVRNodeData();
				const float amount = vr ? vr->currentBowDrawAmount : 0.0f;
				if (amount > 0.05f && amount <= 1.01f) {
					a_t.vrBowSeen = true;
					drawing = true;
					full = full || amount >= 0.95f;
				}
			}
			if (drawing) {
				if (a_t.bowDrawStart < 0.0f) {
					a_t.bowDrawStart = now;
				}
				if (full && a_t.bowFullAt < 0.0f) {
					a_t.bowFullAt = now;
				}
				return;
			}
			if (a_t.bowDrawStart >= 0.0f) {
				a_t.lastHold = HoldOf(a_t);
				a_t.releasedAt = now;
				if (Debug()) {
					logs::info("{:08X}: bow let go after {:.2f} s fully drawn{}", a_t.formID, a_t.lastHold, a_t.bowFullAt >= 0.0f ? "" : " (guessed: never said fully drawn)");
				}
			}
			a_t.bowDrawStart = -1.0f;
			a_t.bowFullAt = -1.0f;
		}

		void UpdateActor(Tracked& a_t, float a_dt)
		{
			const auto ptr = a_t.handle.get();
			const auto actor = ptr.get();
			if (!actor || !actor->Is3DLoaded() || actor->IsDisabled()) {
				ReleaseHands(a_t, true);
				a_t.root = nullptr;
				a_t.watchedRoot = nullptr;
				a_t.attack = 0;
				return;
			}
			// the event sink goes with the animation graphs, which come with the 3D
			if (const void* third = actor->Get3D(false); third != a_t.watchedRoot) {
				a_t.watchedRoot = Attacks::Watch(actor) ? third : nullptr;
				if (a_t.player) {
					logs::info("watching your animation events: {}", a_t.watchedRoot ? "yes" : "no graphs yet");
				}
			}
			bool firstPerson = false;
			a_t.vr = a_t.player && REL::Module::IsVR();
			if (a_t.vr) {
				firstPerson = true;  // the VR body: the hands that follow the controllers
			} else if (a_t.player) {
				const auto camera = RE::PlayerCamera::GetSingleton();
				firstPerson = camera && camera->IsInFirstPerson();
			}
			const auto root = RootOf(actor, firstPerson);
			if (root != a_t.root || firstPerson != a_t.firstPerson) {
				ReleaseHands(a_t, true);
				for (auto& h : a_t.hands) {
					h.node = nullptr;
					h.held = nullptr;
				}
				a_t.root = root;
				a_t.firstPerson = firstPerson;
			}
			const bool drawn = actor->AsActorState()->IsWeaponDrawn() && !actor->IsDead();
			a_t.drawn = drawn;
			TrackBow(a_t, actor);
			UpdateAttack(a_t, actor, a_dt, drawn);
			for (auto& h : a_t.hands) {
				SampleHand(a_t, h, actor, drawn);
			}
			CompareHands(a_t);
			for (auto& h : a_t.hands) {
				UpdateHand(a_t, h, a_dt);
			}
		}

		// the NPCs (and creatures with weapons) near you with their weapons out, nearest first
		void ScanNpcs(RE::PlayerCharacter* a_player)
		{
			for (auto it = tracked.begin() + 1; it != tracked.end(); ++it) {
				(*it)->keep = false;
			}
			if (tuning.npcs && tuning.npcMax > 0) {
				const auto lists = RE::ProcessLists::GetSingleton();
				const auto here = a_player->GetPosition();
				std::vector<std::pair<float, RE::Actor*>> nearby;
				if (lists) {
					for (const auto& handle : lists->highActorHandles) {
						const auto ptr = handle.get();
						const auto actor = ptr.get();
						if (!actor || actor == a_player || actor->IsDead() || !actor->Is3DLoaded() || !actor->AsActorState()->IsWeaponDrawn()) {
							continue;
						}
						const float distance = actor->GetPosition().GetDistance(here);
						if (distance <= tuning.npcRange) {
							nearby.emplace_back(distance, actor);
						}
					}
				}
				std::ranges::sort(nearby, {}, &std::pair<float, RE::Actor*>::first);
				if (nearby.size() > static_cast<std::size_t>(tuning.npcMax)) {
					nearby.resize(static_cast<std::size_t>(tuning.npcMax));
				}
				for (const auto& [distance, actor] : nearby) {
					const auto id = actor->GetFormID();
					const auto it = std::ranges::find_if(tracked, [&](const auto& a_t) { return a_t->formID == id; });
					if (it != tracked.end()) {
						(*it)->keep = true;
					} else {
						tracked.push_back(MakeTracked(actor, false));
					}
				}
			}
			for (auto it = tracked.begin() + 1; it != tracked.end();) {
				if (!(*it)->keep) {
					Forget(**it);
					it = tracked.erase(it);
				} else {
					++it;
				}
			}
		}

		// ---- arrows
		//
		// An arrow shot from a bow held fully drawn long enough leaves a trail of the bow's color behind it, laid in
		// world space along its flight (the particles hang under the player's skeleton) until it hits
		// something or slows down.

		struct Arrow
		{
			RE::ObjectRefHandle handle;
			RE::FormID          shooter{ 0 };
			std::uint32_t       id{ 0 };
			Hand                trail;  // its particles, lines and color
			Vec                 last;
			float               lastTime{ 0.0f };
			float               born{ 0.0f };
			bool                flying{ true };
			bool                started{ false };  // it moved
			bool                player{ false };   // shot by the player
		};

		std::vector<std::unique_ptr<Arrow>>     arrows;
		std::unordered_map<RE::FormID, float>   seenArrows;  // projectiles looked at, when
		float                                   arrowPrune{ 0.0f };

		template <class T>
		void MoveTo(T* a_item, RE::NiNode* a_root)
		{
			if (a_item->parent.get() == a_root) {
				return;
			}
			if (a_item->parent && a_item->node->parent == a_item->parent.get()) {
				a_item->parent->DetachChild(a_item->node.get());
			}
			a_item->parent.reset(a_root);
			a_root->AttachChild(a_item->node.get(), true);
		}

		void Reparent(Hand& a_h, RE::NiNode* a_root)
		{
			for (auto* b : a_h.batches) {
				MoveTo(b, a_root);
			}
			for (auto* l : a_h.lines) {
				MoveTo(l, a_root);
			}
		}

		Tracked* TrackedOf(RE::FormID a_id)
		{
			const auto it = std::ranges::find_if(tracked, [&](const auto& a_t) { return a_t->formID == a_id; });
			return it != tracked.end() ? it->get() : nullptr;
		}

		// new arrows of watched shooters
		void FindArrows()
		{
			const auto manager = RE::Projectile::Manager::GetSingleton();
			if (!manager) {
				return;
			}
			std::vector<RE::NiPointer<RE::Projectile>> found;
			{
				RE::BSSpinLockGuard guard(manager->projectileLock);
				for (const auto* list : { &manager->unlimited, &manager->limited, &manager->pending }) {
					for (const auto& handle : *list) {
						auto projectile = handle.get();
						if (projectile && projectile->GetFormType() == RE::FormType::ProjectileArrow && !seenArrows.contains(projectile->GetFormID())) {
							found.push_back(std::move(projectile));
						}
					}
				}
			}
			for (const auto& projectile : found) {
				seenArrows[projectile->GetFormID()] = now;
				auto&      data = projectile->GetProjectileRuntimeData();
				const auto bow = data.weaponSource;
				if (!bow || bow->GetWeaponType() != RE::WEAPON_TYPE::kBow) {
					continue;
				}
				const auto shooterRef = data.shooter.get();
				const auto shooter = shooterRef ? shooterRef->As<RE::Actor>() : nullptr;
				const auto t = shooter ? TrackedOf(shooter->GetFormID()) : nullptr;
				if (!t || !t->root) {
					continue;
				}
				float hold = HoldOf(*t);
				if (hold < 0.0f && t->vr && !t->vrBowSeen && projectile->GetProjectileRuntimeData().livingTime < 0.5f) {
					hold = tuning.arrowHold;  // VR without a draw to time: a fresh arrow of yours is a shot
				}
				if (hold < 0.0f) {
					continue;  // not just shot (it was in the world already)
				}
				RE::EnchantmentItem* enchantment = bow->formEnchanting;
				if (shooter->GetEquippedObject(false) == bow) {
					enchantment = EnchantmentOf(shooter, false, bow);
				}
				const auto element = ElementOf(shooter, bow, enchantment);
				const auto v = LookAt(bow, element, IsUnique(bow), TintOf(enchantment));
				const bool trail = v.on && hold >= tuning.arrowHold;
				if (Debug() || t->player) {
					logs::info("{:08X}: arrow from {}, bow held {:.2f} s fully drawn ({:.1f} s needed), {}: {}", t->formID, bow->GetName(), hold, tuning.arrowHold,
						Settings::kElementNames[element], trail ? "trail" : (v.on ? "no trail (not held long enough)" : "no trail (Weapons page)"));
				}
				if (!trail) {
					continue;
				}
				auto a = std::make_unique<Arrow>();
				a->handle = projectile->GetHandle();
				a->shooter = t->formID;
				a->id = ++attackCounter;
				a->trail.color = v.color;
				a->last = projectile->GetPosition();
				a->player = t->player;
				a->lastTime = now;
				a->born = now;
				arrows.push_back(std::move(a));
			}
		}

		// what a stretch of the flight lays: a crisp line, dashes packed along the path, dots, a few sparks
		void LayArrow(Arrow& a_a, RE::NiNode* a_root, const Vec& a_from, const Vec& a_to, float a_t0, float a_t1)
		{
			auto&       h = a_a.trail;
			const Vec   d = a_to - a_from;
			const float len = d.Length();
			if (len < 1e-3f) {
				return;
			}
			const Vec   dir = d / len;
			const float speed = len / std::max(a_t1 - a_t0, 1e-4f);
			const Vec   side = Normalized(dir.Cross(Vec{ 0.0f, 0.0f, 1.0f }), Vec{ 1.0f, 0.0f, 0.0f });
			const Vec   up = side.Cross(dir);
			const auto  across = [&](float a_radius) {
				const float angle = Random(0.0f, 6.2832f);
				return (side * std::cos(angle) + up * std::sin(angle)) * a_radius;
			};
			const float amount = tuning.sparkAmount;
			const float size = tuning.sparkSize;
			const float duration = tuning.sparkLife;
			const float spread = tuning.sparkSpread;

			if (tuning.line) {
				if (auto* line = LineFor(a_root, h, a_a.id, a_t0)) {
					if (line->points.empty()) {
						AddLinePoint(*line, a_from, a_t0, 1.0f);
					}
					const int steps = std::clamp(static_cast<int>(len / 60.0f), 1, 8);
					for (int k = 1; k <= steps; ++k) {
						const float f = static_cast<float>(k) / static_cast<float>(steps);
						AddLinePoint(*line, Lerp(a_from, a_to, f), a_t0 + (a_t1 - a_t0) * f, 1.0f);
					}
				}
			}
			h.lastLaid = a_t1;
			h.currentAttack = a_a.id;
			if (!tuning.sparks) {
				return;
			}

			int        budget = 400;
			const auto count = [&](float a_expected) {
				int n = static_cast<int>(a_expected);
				if (Random(0.0f, 1.0f) < a_expected - static_cast<float>(n)) {
					++n;
				}
				n = std::min(n, budget);
				budget -= n;
				return n;
			};
			const auto spawn = [&](bool a_dot, float a_radius) -> Spark* {
				auto* sp = NewSpark(a_root, h, a_dot);
				if (!sp) {
					return nullptr;
				}
				const float f = Random(0.0f, 1.0f);
				sp->pos = Lerp(a_from, a_to, f) + across(Random(0.0f, a_radius));
				sp->dir = dir;
				sp->age = -(now - (a_t0 + (a_t1 - a_t0) * f));
				return sp;
			};
			const auto settle = [&](Spark& a_sp) {
				const float ago = -a_sp.age;
				a_sp.age = 0.0f;
				Advance(a_sp, ago);
			};
			// dashes along the path
			for (int j = count(len / 7.0f * amount); j > 0; --j) {
				if (auto* sp = spawn(false, 1.2f + 3.0f * spread)) {
					sp->vel = dir * (speed * Random(0.0f, 0.01f)) + across(Random(2.0f, 12.0f) * spread);
					sp->size = Random(5.0f, 10.0f) * size;
					sp->life = Random(0.6f, 1.2f) * duration;
					sp->drag = 1.4f;
					sp->gravity = 3.0f;
					sp->twinkle = Random(0.0f, 1.0f) < 0.25f;
					settle(*sp);
				}
			}
			// dots around it
			for (int j = count(len / 30.0f * amount); j > 0; --j) {
				if (auto* sp = spawn(true, 2.0f + 5.0f * spread)) {
					sp->vel = across(Random(3.0f, 20.0f) * spread);
					sp->size = Random(1.0f, 2.2f) * size;
					sp->life = Random(0.5f, 1.1f) * duration;
					sp->drag = 1.5f;
					sp->gravity = 8.0f;
					sp->twinkle = true;
					settle(*sp);
				}
			}
			// sparks thrown off
			for (int j = count(len / 90.0f * tuning.flyingSparks); j > 0; --j) {
				const bool dot = Random(0.0f, 1.0f) < 0.5f;
				if (auto* sp = spawn(dot, 1.0f)) {
					sp->vel = dir * (speed * Random(0.02f, 0.08f)) + RandomUnit() * (Random(60.0f, 200.0f) * spread);
					sp->dir = Normalized(sp->vel, dir);
					sp->size = (dot ? Random(1.2f, 2.0f) : Random(4.0f, 7.0f)) * size;
					sp->life = Random(0.25f, 0.55f) * duration;
					sp->drag = 3.5f;
					sp->gravity = 140.0f;
					sp->twinkle = true;
					sp->turns = true;
					settle(*sp);
				}
			}
		}

		// follows a flying arrow; it stops at a hit, when it slows down or when it is gone. A new arrow may sit still for
		// a few frames before it flies (it is launched with the next projectile update): it gets half a second to start.
		void FlyArrow(Arrow& a_a, RE::NiNode* a_root)
		{
			const auto  ref = a_a.handle.get();
			const auto  arrow = ref ? skyrim_cast<RE::Projectile*>(ref.get()) : nullptr;
			const char* why = nullptr;
			if (!arrow || arrow->IsDeleted()) {
				why = "the arrow is gone";
			} else if (now - a_a.born > kArrowFlight) {
				why = "flew too long";
			} else if (a_a.started && !arrow->GetProjectileRuntimeData().impacts.empty()) {
				why = "it hit something";
			}
			if (!why) {
				const float dt = now - a_a.lastTime;
				if (dt <= 0.0f) {
					return;  // paused
				}
				const Vec   pos = arrow->GetPosition();
				const float speed = pos.GetDistance(a_a.last) / dt;
				if (speed >= kArrowMinSpeed && speed < 100000.0f) {
					a_a.started = true;
					LayArrow(a_a, a_root, a_a.last, pos, a_a.lastTime, now);
				} else if (a_a.started) {
					why = "it stopped";
				} else if (now - a_a.born > 0.5f) {
					why = "it never flew";
				}
				if (!why) {
					a_a.last = pos;
					a_a.lastTime = now;
					return;
				}
			}
			a_a.flying = false;
			if (a_a.trail.current) {
				a_a.trail.current->open = false;
				a_a.trail.current = nullptr;
			}
			if (Debug() || a_a.player) {
				logs::info("arrow {}: trail ends after {:.2f} s ({})", a_a.id, now - a_a.born, why);
			}
		}

		void UpdateArrows(float a_dt)
		{
			for (auto it = arrows.begin(); it != arrows.end();) {
				auto&      a = **it;
				// under your skeleton (it is drawn whenever you see anything), whoever shot it
				const auto root = tracked.empty() || !tracked.front()->root ? nullptr : DrawRoot(*tracked.front());
				if (!root) {
					ReleaseHand(a.trail, true);
					it = arrows.erase(it);
					continue;
				}
				Reparent(a.trail, root);
				StepParticles(a.trail, a_dt);
				if (a.flying) {
					FlyArrow(a, root);
				}
				DrawParticles(a.trail, root->world);
				if (!a.flying && a.trail.batches.empty() && a.trail.lines.empty()) {
					it = arrows.erase(it);
					continue;
				}
				++it;
			}
			// forget the projectiles looked at long ago (their ids come back for new ones)
			arrowPrune -= a_dt;
			if (arrowPrune <= 0.0f) {
				arrowPrune = 5.0f;
				std::erase_if(seenArrows, [](const auto& a_seen) { return now - a_seen.second > 30.0f; });
			}
		}

		void DropArrows()
		{
			for (auto& a : arrows) {
				ReleaseHand(a->trail, true);
			}
			arrows.clear();
			seenArrows.clear();
		}

		void WriteStatus()
		{
			Status st;
			if (!tracked.empty()) {
				const auto& you = *tracked.front();
				for (std::size_t k = 0; k < 2; ++k) {
					const auto& h = you.hands[k];
					auto&       out = st.hands[k];
					if (const auto effect = h.tint.effect; effect && (h.fist || h.looked)) {
						out.effectName = effect->GetName();
						if (const auto file = effect->GetFile(0)) {
							out.effectPlugin = file->GetFilename();
							out.effectId = file->IsLight() ? effect->GetFormID() & 0xFFF : effect->GetFormID() & 0xFFFFFF;
						}
					}
					if (h.fist) {
						out.weapon = you.drawn && h.held != nullptr;
						out.fist = true;
						out.name = you.gauntlet ? you.gauntlet->GetName() : "";
						out.element = h.element;
						out.trailOn = h.trailOn;
						out.color = h.color;
						continue;
					}
					const auto  w = h.looked;
					out.weapon = w && you.drawn && (IsMelee(w) ? h.held != nullptr : w->GetWeaponType() == RE::WEAPON_TYPE::kBow);
					if (!w) {
						continue;
					}
					out.name = w->GetName();
					out.element = h.element;
					out.unique = h.unique;
					out.listed = h.rule >= 0;
					out.trailOn = h.trailOn;
					out.color = h.color;
					const auto file = w->GetFile(0);
					out.plugin = file ? file->GetFilename() : "";
					out.id = file && file->IsLight() ? w->GetFormID() & 0xFFF : w->GetFormID() & 0xFFFFFF;
				}
			}
			std::scoped_lock guard(statusLock);
			status = std::move(st);
		}

		void DropAll()
		{
			DropArrows();
			for (auto& t : tracked) {
				Forget(*t);
				t->attack = 0;
				t->root = nullptr;
			}
		}

		RE::NiPointer<RE::NiNode> Load(const char* a_path, const char* a_without = "no trails")
		{
			RE::NiPointer<RE::NiNode>         loaded;
			RE::BSModelDB::DBTraits::ArgsType args{};
			if (RE::BSModelDB::Demand(a_path, loaded, args) != RE::BSResource::ErrorCode::kNone || !loaded) {
				logs::error("couldn't load meshes\\{} (is the whole mod installed?): {}", a_path, a_without);
				return nullptr;
			}
			return loaded;
		}
	}

	void Init()
	{
		particleModels[0] = Load(kParticleModel);
		lineModels[0] = Load(kLineModel);
		if (!particleModels[0] || !lineModels[0]) {
			return;
		}
		particleModels[1] = Load(kSolidParticleModel, "dark colors glow like the others");
		lineModels[1] = Load(kSolidLineModel, "dark colors glow like the others");
		modelsLoaded = true;
		RefreshTuning();
		FindLoot();
		FindGhostForms();
		FindSoulTrapForms();
		// a few made ahead (copying a model mid-fight could hitch), and a check that copies work at all
		for (int i = 0; i < 8; ++i) {
			auto b = MakeBatch(false);
			if (!b) {
				modelsLoaded = false;
				return;
			}
			batchPools[0].push_back(std::move(b));
		}
		for (int i = 0; i < 4; ++i) {
			auto l = MakeLine(false);
			if (!l) {
				modelsLoaded = false;
				return;
			}
			linePools[0].push_back(std::move(l));
		}
		logs::info("models loaded: {} particle batches ({} particles each) and {} lines made ahead{}", batchPools[0].size(), kPerBatch, linePools[0].size(),
			particleModels[1] && lineModels[1] ? ", solid models for dark colors" : "");
	}

	void Reset()
	{
		DropAll();
		tracked.clear();
		Attacks::Clear();
		npcScanIn = 0.0f;
	}

	void Frame(float a_delta)
	{
		RefreshTuning();
		const auto player = RE::PlayerCharacter::GetSingleton();
		if (!modelsLoaded || !player) {
			return;
		}
		const float dt = std::clamp(a_delta, 0.0f, 0.25f);
		now += dt;
		if (!tuning.enabled && !tuning.fists && !tuning.arrows) {
			if (!tracked.empty()) {
				Reset();
			}
			WriteStatus();
			return;
		}
		if (tracked.empty() || tracked.front()->formID != player->GetFormID()) {
			Reset();
			tracked.push_back(MakeTracked(player, true));
		}
		npcScanIn -= dt;
		if (npcScanIn <= 0.0f) {
			npcScanIn = kNpcScan;
			ScanNpcs(player);
		}
		for (auto& t : tracked) {
			UpdateActor(*t, dt);
		}
		if (tuning.arrows) {
			FindArrows();
		}
		UpdateArrows(dt);
		statusIn -= dt;
		if (statusIn <= 0.0f) {
			statusIn = 0.25f;
			WriteStatus();
		}
	}

	bool GlowColor(const RE::EffectSetting* a_effect, std::array<float, 3>& a_color)
	{
		return Glow(a_effect, a_color);
	}

	Settings::Element ElementOfEffect(const RE::EffectSetting* a_effect)
	{
		if (!a_effect) {
			return Settings::kOther;
		}
		const auto resist = a_effect->data.resistVariable;
		if (a_effect->HasKeywordString("MagicDamageFire") || resist == RE::ActorValue::kResistFire) {
			return Settings::kFire;
		}
		if (a_effect->HasKeywordString("MagicDamageFrost") || resist == RE::ActorValue::kResistFrost) {
			return Settings::kFrost;
		}
		if (a_effect->HasKeywordString("MagicDamageShock") || resist == RE::ActorValue::kResistShock) {
			return Settings::kShock;
		}
		switch (a_effect->data.primaryAV) {
		case RE::ActorValue::kHealth:
			return Settings::kHealth;
		case RE::ActorValue::kMagicka:
			return Settings::kMagicka;
		case RE::ActorValue::kStamina:
			return Settings::kStamina;
		default:
			break;
		}
		if (IsSoulTrap(a_effect)) {
			return Settings::kSoulTrap;
		}
		using Kind = RE::EffectArchetypes::ArchetypeID;
		switch (a_effect->GetArchetype()) {
		case Kind::kParalysis:
			return Settings::kParalysis;
		case Kind::kDemoralize:
			return Settings::kFear;
		case Kind::kTurnUndead:
			return Settings::kTurnUndead;
		case Kind::kBanish:
			return Settings::kBanish;
		case Kind::kCalm:
			return Settings::kCalm;
		case Kind::kFrenzy:
			return Settings::kFrenzy;
		default:
			return Settings::kOther;
		}
	}

	Status GetStatus()
	{
		std::scoped_lock guard(statusLock);
		return status;
	}
}
