using System;

namespace VspEngine.Rendering
{
	/// <summary>
	/// One compiled shader module handed to the graphics backend: the SPIR-V blob
	/// and the name of the entry point inside it the stage runs - HLSL shaders
	/// name theirs PassVertex / PassFragment.
	///
	/// This is the low-level building block a pipeline is made of; the shader
	/// ASSET a game loads and a material points at is <see cref="VspEngine.Shader"/>.
	/// Shader modules are graphics resources, so Dispose() releases the native one.
	/// </summary>
	public sealed class ShaderModule : IDisposable
	{
		private readonly string entryPointName = string.Empty;
		private uint nativeHandle;

		/// <summary>Creates a module from a SPIR-V blob (4-byte aligned, non-empty).</summary>
		public ShaderModule(ShaderStage stage, string entryPointName, byte[] spirvCode)
		{
			if (string.IsNullOrEmpty(entryPointName))
			{
				throw new ArgumentException("A shader module needs the name of its entry point.", nameof(entryPointName));
			}
			if (spirvCode == null || spirvCode.Length == 0)
			{
				throw new ArgumentException("A shader module needs a non-empty SPIR-V blob.", nameof(spirvCode));
			}

			this.entryPointName = entryPointName;
			nativeHandle = RhiApi.VspRhi_CreateShader((int)stage, entryPointName, spirvCode, (uint)spirvCode.Length);
		}

		/// <summary>Entry point function inside the module.</summary>
		public string EntryPointName => entryPointName;

		/// <summary>Native module handle (0 = invalid).</summary>
		public uint NativeHandle => nativeHandle;

		public bool IsValid => nativeHandle != 0;

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
