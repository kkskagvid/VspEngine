namespace VspEngine
{
	/// <summary>Triangle color modes. The T key cycles Red -> Blue -> Green -> MultiColor.</summary>
	public enum ColorMode
	{
		Red = 0,
		Blue = 1,
		Green = 2,
		MultiColor = 3,
	}

	/// <summary>Unity-style Renderer facade forwarded to the native engine.</summary>
	public static class Renderer
	{
		public static void SetColorMode(uint instanceId, ColorMode mode) =>
			NativeApi.VspRenderer_SetColorMode(instanceId, (int)mode);

		public static ColorMode GetColorMode(uint instanceId) =>
			(ColorMode)NativeApi.VspRenderer_GetColorMode(instanceId);
	}
}
