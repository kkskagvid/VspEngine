namespace VspEngine
{
	/// <summary>Unity-style debug logging forwarded to the native engine log.</summary>
	public static class Debug
	{
		/// <summary>Writes the message into the native engine log.</summary>
		public static void Log(string message) => NativeApi.VspLog_Message(message);

		/// <summary>Formats and writes the message into the native engine log.</summary>
		public static void Log(string format, params object?[] args) =>
			NativeApi.VspLog_Message(string.Format(format, args));

		/// <summary> Writes the message into the native engine log with a debug severity level. </summary>
		public static void LogDebug(string message) => NativeApi.VspLog_Debug(message);

		/// <summary> Formats and writes the message into the native engine log with a debug severity level. </summary>
		public static void LogDebug(string format, params object?[] args) =>
			NativeApi.VspLog_Debug(string.Format(format, args));

		/// <summary> Writes the message into the native engine log with an info severity level. </summary>
		public static void LogInfo(string message) => NativeApi.VspLog_Info(message);

		/// <summary> Formats and writes the message into the native engine log with an info severity level. </summary>
		public static void LogInfo(string format, params object?[] args) =>
			NativeApi.VspLog_Info(string.Format(format, args));

		/// <summary> Writes the message into the native engine log with a warning severity level. </summary>
		public static void LogWarning(string message) => NativeApi.VspLog_Warning(message);

		/// <summary> Formats and writes the message into the native engine log with a warning severity level. </summary>
		public static void LogWarning(string format, params object?[] args) =>
			NativeApi.VspLog_Warning(string.Format(format, args));
		
		/// <summary> Writes the message into the native engine log with an error severity level. </summary>
		public static void LogError(string message) => NativeApi.VspLog_Error(message);

		/// <summary> Formats and writes the message into the native engine log with an error severity level. </summary>
		public static void LogError(string format, params object?[] args) =>
			NativeApi.VspLog_Error(string.Format(format, args));
	}
}
