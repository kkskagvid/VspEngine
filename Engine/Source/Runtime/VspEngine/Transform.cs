using System.Numerics;

namespace VspEngine
{
	/// <summary>
	/// Unity-style Transform facade. Every call is forwarded to the native
	/// engine and keyed by the owning script's InstanceID.
	/// </summary>
	public sealed class Transform
	{
		private readonly uint instanceId;

		internal Transform(uint instanceId)
		{
			this.instanceId = instanceId;
		}

		/// <summary>Position in world space (screen center is the origin).</summary>
		public Vector2 Position
		{
			get => new Vector2(
				NativeApi.VspTransform_GetPositionX(instanceId),
				NativeApi.VspTransform_GetPositionY(instanceId));
			set => NativeApi.VspTransform_SetPosition(instanceId, value.X, value.Y);
		}
	}
}
