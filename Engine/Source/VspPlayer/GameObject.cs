using System;

namespace VspEngine
{
	public abstract class GameObject
	{
		/// <summary>Non-negative integer handle used by the C++ host to address this object (0 = invalid).</summary>
		public uint InstanceID { get; internal set; }

		public uint GetInstanceID()
		{
			return InstanceID;
		}
	}
}
