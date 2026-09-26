#pragma once

namespace Vsp
{
	// -------------------------------------------------------------------------
	// Object
	// -------------------------------------------------------------------------
	// Optional common base of the engine's class hierarchy. Deriving from it is
	// a convenience, NOT a requirement: nothing in Core/Templates - Callback,
	// UnicastDelegate, MulticastDelegate, ArrayList - demands it of a type it
	// stores or returns. A type that does derive gets the virtual destructor
	// below, so derived instances can be destroyed safely through an Object
	// pointer; a type that does not is used just as well.
	// -------------------------------------------------------------------------
	class Object
	{
	public:
		Object() {}
		virtual ~Object() = default;
	};
}
