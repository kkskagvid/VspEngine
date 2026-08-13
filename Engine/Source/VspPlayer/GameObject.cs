using System;
using System.Collections.Generic;
using System.Text;

namespace VspEngine
{
	public abstract class GameObject
	{
		private uint m_InstanceID;

		public uint GetInstanceID()
		{
			return m_InstanceID;
		}
	}
}
