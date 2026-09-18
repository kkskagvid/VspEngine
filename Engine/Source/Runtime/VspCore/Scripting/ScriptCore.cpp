#include "RuntimePCH.h"

#include "Scripting/ScriptCore.h"

namespace Vsp
{
	ScriptCore& ScriptCore::Get()
	{
		static ScriptCore s_Instance;
		return s_Instance;
	}
}
