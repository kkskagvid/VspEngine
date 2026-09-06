#pragma once

#include "Core/String/VspString.h"
#include "Scripting/ScriptEngine.h"

namespace Vsp
{
	// -------------------------------------------------------------------------
	// ManagedScript
	// -------------------------------------------------------------------------
	// Convenience wrapper binding one C++-side handle to one managed script
	// instance. The underlying ScriptEngine (CoreCLR host) does the actual
	// work; this class only remembers the non-negative InstanceID.
	// -------------------------------------------------------------------------
	class ManagedScript
	{
	public:
		ManagedScript() = default;

		~ManagedScript()
		{
			DestroyInstance();
		}

		// Creates the managed instance; false when creation failed (id stays 0).
		bool CreateInstance(const VspString& sTypeName)
		{
			m_uInstanceId = ScriptEngine::Get().CreateScriptInstance(sTypeName);
			return m_uInstanceId != 0;
		}

		void CallInit()
		{
			ScriptEngine::Get().CallScriptInit(m_uInstanceId);
		}

		void CallStart()
		{
			ScriptEngine::Get().CallScriptStart(m_uInstanceId);
		}

		void CallUpdate()
		{
			ScriptEngine::Get().CallScriptUpdate(m_uInstanceId);
		}

		void DestroyInstance()
		{
			if (m_uInstanceId != 0)
			{
				ScriptEngine::Get().DestroyScriptInstance(m_uInstanceId);
				m_uInstanceId = 0;
			}
		}

		ScriptInstanceId GetInstanceId() const { return m_uInstanceId; }
		bool IsValid() const { return m_uInstanceId != 0; }

	private:
		ScriptInstanceId m_uInstanceId = 0;
	};
}
