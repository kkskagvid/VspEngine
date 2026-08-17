#pragma once

#include "Engine/Core/Core.h"

namespace Vsp
{
	template <typename Type>
	class ScopedPtr
	{

	};

	template <typename Type>
	class RefPtr
	{
	public:
	private:
		uint64_t m_RefCount;
	};
}
