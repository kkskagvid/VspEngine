#pragma once

namespace Vsp
{
	// -------------------------------------------------------------------------
	// Object
	// -------------------------------------------------------------------------
	// Root of the engine's class hierarchy. Every class/struct type that a
	// Callback returns must derive from Object (fundamental return types such
	// as int or double need no base class). The virtual destructor lets
	// derived instances be destroyed safely through an Object pointer.
	// -------------------------------------------------------------------------
	class Object
	{
	public:
		Object() {}
		virtual ~Object() = default;
	};
}
