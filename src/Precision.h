#pragma once

// Precision (by Ershin) draws its own weapon trails: a particle effect (BSTempEffectParticle::Spawn, game code) put
// where its attack collision's parent node is, the WEAPON or SHIELD node of the striking hand, and moved by it every
// frame. Precision has no API for them, so the spawn function is detoured, and only calls coming from Precision.dll
// are looked at: Precision passes that node's own world transform, which tells whose weapon the trail is for.
namespace Precision
{
	// all plugins loaded: finds Precision.dll and hooks the spawn if it's there
	void Install();

	// Precision is loaded and its trails can be told apart
	bool Found();

	// A trail Precision just made, and the node it was made for (held, so both stay valid until looked at).
	struct Spawned
	{
		RE::NiPointer<RE::BSTempEffectParticle> trail;
		RE::NiPointer<RE::NiAVObject>           node;
		bool                                    hiddenAtOnce{ false };  // culled right when it was made
	};

	// the trails made since the last call (Precision may make them on other threads)
	std::vector<Spawned> Take();

	// Precision's trails are hidden right when they're made (before the first frame they could show) while any kind
	// of weapon is set to these trails only; the ones that turn out to be wanted are shown again.
	void SetHideAtOnce(bool a_hide);
}
