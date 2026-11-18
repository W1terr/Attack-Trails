#pragma once

// Animation events of the actors whose attacks are watched. The game sends them from its animation threads; they are
// queued per actor and taken once a frame on the main thread.
//
// Power attacks are found by name, so custom attack animations count too: vanilla "attackPowerStart..." events and
// anything named like a power or heavy attack that isn't an end (MCO, BFCO, SCAR, For Honor style and similar
// frameworks). The plugin also trusts the game's own attack data (its power attack flag), which vanilla, Precision
// and most frameworks set.
namespace Attacks
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
	};

	struct Signals
	{
		bool powerStart{ false };
		bool attackStart{ false };
		bool swing{ false };
		bool swingRight{ false };  // weaponSwing: the right hand's weapon swings
		bool swingLeft{ false };   // weaponLeftSwing: the left hand's
		bool preHit{ false };
		bool hit{ false };
		bool stop{ false };

		bool          precision{ false };       // the animation has Precision's own hit windows
		std::uint8_t  collisionAdd{ 0 };        // the hands a Collision_Add opened (1 right, 2 left)
		float         collisionDuration{ 0.0f };  // its Duration(), 0: until Collision_Remove
		bool          collisionRemove{ false };

		bool Any() const { return powerStart || attackStart || swing || preHit || hit || stop || precision || collisionAdd || collisionRemove; }
	};

	Class Classify(std::string_view a_tag);

	// adds the event sink to the actor's animation graphs (again when it got a new 3D); false if it has none
	bool Watch(RE::Actor* a_actor);
	void Unwatch(RE::Actor* a_actor);

	// what the actor's events said since the last call
	Signals Take(RE::FormID a_actor);
	void    Clear();
}
