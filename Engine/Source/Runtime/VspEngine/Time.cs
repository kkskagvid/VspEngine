namespace VspEngine
{
	/// <summary>Unity-style time accessors backed by the native engine clock.</summary>
	public static class Time
	{
		/// <summary>Seconds elapsed since the previous frame.</summary>
		public static float DeltaTime => NativeApi.VspTime_GetDeltaTime();

		/// <summary>Seconds elapsed since the engine started.</summary>
		public static float ElapsedTime => NativeApi.VspTime_GetElapsedTime();
	}
}
