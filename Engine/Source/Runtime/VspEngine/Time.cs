namespace VspEngine
{
	/// <summary>
	/// The engine clock, as a script reads it. Every member forwards to the one
	/// native clock (<c>VspCore</c>'s <c>Classes/Time</c>), so a script, the render
	/// pipeline and the engine's own diagnostics always read the SAME frame time.
	///
	/// The clock runs in engine time: <see cref="TimeScale"/> stretches or stops
	/// it, and <see cref="DeltaTime"/> follows, while the unscaled members report
	/// what the machine really did.
	/// </summary>
	public static class Time
	{
		/// <summary>Seconds elapsed since the previous frame, in engine time.</summary>
		public static float DeltaTime => NativeApi.VspTime_GetDeltaTime();

		/// <summary>
		/// Seconds the previous frame really took. It equals
		/// <see cref="DeltaTime"/> while the time scale is 1, and stays the real
		/// step while a fixed-step run drives the clock.
		/// </summary>
		public static float UnscaledDeltaTime => NativeApi.VspTime_GetUnscaledDeltaTime();

		/// <summary>Seconds elapsed since the engine started, in engine time.</summary>
		public static float ElapsedTime => NativeApi.VspTime_GetElapsedTime();

		/// <summary>Seconds elapsed since the engine started, as the machine measured them.</summary>
		public static float UnscaledElapsedTime => NativeApi.VspTime_GetUnscaledElapsedTime();

		/// <summary>Frames the engine has advanced so far.</summary>
		public static long FrameCount => NativeApi.VspTime_GetFrameCount();

		/// <summary>Smoothed frames per second, so a readout does not flicker.</summary>
		public static float FramesPerSecond => NativeApi.VspTime_GetFramesPerSecond();

		/// <summary>
		/// How fast engine time runs: 1 = real time, 0 = paused, 2 = twice as
		/// fast. Negative values are clamped to 0 - the clock never runs
		/// backwards.
		/// </summary>
		public static float TimeScale
		{
			get => NativeApi.VspTime_GetTimeScale();
			set => NativeApi.VspTime_SetTimeScale(value);
		}

		/// <summary>
		/// True while the engine advances its clock by
		/// <see cref="FixedDeltaTime"/> every frame instead of by the wall clock,
		/// which is what <c>--fixed-delta-time</c> asks for: an automated run then
		/// happens at the same engine time on every machine.
		/// </summary>
		public static bool IsFixedTimeStep => NativeApi.VspTime_IsFixedTimeStep() != 0;

		/// <summary>The fixed step a reproducible run advances by, in seconds (0 = wall clock).</summary>
		public static float FixedDeltaTime => NativeApi.VspTime_GetFixedDeltaTime();
	}
}
