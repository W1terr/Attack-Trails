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
		std::uint8_t  collisionAdd{ 0 };        // the parts a Collision_Add opened: 1 right hand / weapon, 2 left, 4 another (a foot)
		float         collisionDuration{ 0.0f };  // its Duration(), 0: until Collision_Remove
		bool          collisionRemove{ false };
		std::uint8_t  collisionRemoved{ 0 };    // the parts it closed (as above), 0xFF: all of them
		bool          clipEdge{ false };

		bool Any() const { return powerStart || attackStart || swing || preHit || hit || stop || precision || collisionAdd || collisionRemove || clipEdge; }
	};

	// adds the event sink to the actor's animation graphs (again when it got a new 3D); false if it has none
	bool Watch(RE::Actor* a_actor);
	void Unwatch(RE::Actor* a_actor);

	// what the actor's events said since the last call
	Signals Take(RE::FormID a_actor);
	void    Clear();
}
