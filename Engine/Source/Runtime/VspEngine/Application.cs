using System.Text;

namespace VspEngine
{
	/// <summary>
	/// Things a game asks the running application about itself: where its files
	/// live and which scene it has.
	///
	/// The engine owns the process (and the window, the graphics backend and the
	/// scene), so a game reads these instead of querying the operating system.
	/// </summary>
	public static class Application
	{
		/// <summary>
		/// Directory the running executable sits in: where a game's own files
		/// (a saved scene, a log, a texture) belong, and where the engine looks
		/// for compiled shaders.
		/// </summary>
		public static string ExecutableDirectory
		{
			get
			{
				byte[] directoryBuffer = new byte[1024];
				int byteCount = NativeApi.VspPlatform_GetExecutableDirectoryUtf8(directoryBuffer, directoryBuffer.Length);
				return byteCount > 0 ? Encoding.UTF8.GetString(directoryBuffer, 0, byteCount) : string.Empty;
			}
		}

		/// <summary>Combines the executable directory with a relative file name.</summary>
		public static string GetExecutableFilePath(string fileName) =>
			System.IO.Path.Combine(ExecutableDirectory, fileName);

		/// <summary>Game objects the scene currently holds.</summary>
		public static int GameObjectCount => (int)NativeApi.VspScene_GetLiveGameObjectCount();

		/// <summary>Cameras the scene currently holds.</summary>
		public static int CameraCount => (int)NativeApi.VspScene_GetLiveCameraCount();
	}
}
