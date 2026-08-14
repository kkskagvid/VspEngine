#pragma once

#include "Engine/Core/Utils/ArrayList.h"

namespace Vsp
{
	template <typename StringType>
	class VspStringWrapper
	{
	public:
		VspStringWrapper() {}

	private:
		ArrayList<StringType> m_Data;
	};

	class VspString
	{

	};
}
