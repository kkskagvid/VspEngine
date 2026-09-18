using System;

namespace VspEngine.Rendering
{
	/// <summary>
	/// A compiled shader module. The SPIR-V blob is handed to the native backend
	/// once; the returned handle is what pipelines and draw commands address.
	/// Shaders are graphics resources, so Dispose() releases the native module.
	/// </summary>
	public sealed class Shader : IDisposable
	{
		private uint nativeHandle;

		/// <summary>Creates a shader from a SPIR-V blob (4-byte aligned, non-empty).</summary>
		public Shader(ShaderStage stage, byte[] spirvCode)
		{
			if (spirvCode == null || spirvCode.Length == 0)
			{
				throw new ArgumentException("A shader needs a non-empty SPIR-V blob.", nameof(spirvCode));
			}

			nativeHandle = RhiApi.VspRhi_CreateShader((int)stage, spirvCode, (uint)spirvCode.Length);
		}

		private Shader(uint handle)
		{
			nativeHandle = handle;
		}

		/// <summary>Native shader handle (0 = invalid).</summary>
		public uint NativeHandle => nativeHandle;

		public bool IsValid => nativeHandle != 0;

		/// <summary>
		/// Creates a shader from one of the SPIR-V blobs the engine ships with.
		/// The bytes make the round trip through the wrapped graphics API, so a
		/// pipeline with its own SPIR-V uses exactly the same path.
		/// </summary>
		public static Shader? CreateFromEmbedded(EmbeddedShader embeddedShader, ShaderStage stage)
		{
			int byteCount = RhiApi.VspRhi_GetEmbeddedShaderByteCount((int)embeddedShader);
			if (byteCount <= 0 || (byteCount % 4) != 0)
			{
				Debug.LogError("Rendering.Shader: embedded shader " + embeddedShader + " is unavailable.");
				return null;
			}

			byte[] spirvCode = new byte[byteCount];
			int writtenByteCount = RhiApi.VspRhi_GetEmbeddedShaderBytes((int)embeddedShader, spirvCode, (uint)byteCount);
			if (writtenByteCount != byteCount)
			{
				Debug.LogError("Rendering.Shader: embedded shader " + embeddedShader + " could not be copied.");
				return null;
			}

			uint handle = RhiApi.VspRhi_CreateShader((int)stage, spirvCode, (uint)byteCount);
			return handle != 0 ? new Shader(handle) : null;
		}

		public void Dispose()
		{
			if (nativeHandle != 0)
			{
				RhiApi.VspRhi_DestroyShader(nativeHandle);
				nativeHandle = 0;
			}
		}
	}
}
