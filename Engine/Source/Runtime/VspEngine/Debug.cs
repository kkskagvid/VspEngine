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
	}
}
