using System;
using System.Text;

namespace VspEngine.Rendering
{
	/// <summary>
	/// Reflection summary of one compiled shader stage: what the pipeline has to
	/// match when it builds its state (the counters come straight from HLSLCC).
	/// </summary>
	public readonly struct ShaderStageReflectionSummary
	{
		/// <summary>Number of interface variables the stage reads.</summary>
		public readonly uint InputCount;

		/// <summary>Number of interface variables the stage writes.</summary>
		public readonly uint OutputCount;

		/// <summary>Number of descriptor bindings the stage declares.</summary>
		public readonly uint ResourceCount;

		/// <summary>Number of members in the push-constant block.</summary>
		public readonly uint PushConstantMemberCount;

		/// <summary>Size of the push-constant block in bytes.</summary>
		public readonly uint PushConstantByteSize;

		public ShaderStageReflectionSummary(uint[] values)
		{
			InputCount = values.Length > 0 ? values[0] : 0;
			OutputCount = values.Length > 1 ? values[1] : 0;
			ResourceCount = values.Length > 2 ? values[2] : 0;
			PushConstantMemberCount = values.Length > 3 ? values[3] : 0;
			PushConstantByteSize = values.Length > 4 ? values[4] : 0;
		}
	}

	/// <summary>
	/// One stage of a compiled HLSL shader: its SPIR-V module, the entry point it
	/// was compiled from and its reflection.
	/// </summary>
	public sealed class CompiledShaderStage
	{
		internal CompiledShaderStage(ShaderStage stage, string entryPointName, byte[] spirvCode, ShaderStageReflectionSummary reflection)
		{
			Stage = stage;
			EntryPointName = entryPointName;
			SpirvCode = spirvCode;
			Reflection = reflection;
		}

		public ShaderStage Stage { get; }

		/// <summary>Entry point function the module was compiled from.</summary>
		public string EntryPointName { get; }

		/// <summary>The SPIR-V module, ready to hand to <see cref="Shader"/>.</summary>
		public byte[] SpirvCode { get; }

		public ShaderStageReflectionSummary Reflection { get; }

		public bool IsValid => SpirvCode.Length > 0;
	}

	/// <summary>
	/// Everything one HLSL file compiled to: one <see cref="CompiledShaderStage"/>
	/// per entry point the file defines, plus the engine's reflection document.
	/// </summary>
	public sealed class CompiledShader
	{
		private readonly CompiledShaderStage?[] stages = new CompiledShaderStage?[StageCount];

		internal const int StageCount = 2;

		internal CompiledShader(string sourceName, string reflectionJson)
		{
			SourceName = sourceName;
			ReflectionJson = reflectionJson;
		}

		/// <summary>Name of the source that was compiled.</summary>
		public string SourceName { get; }

		/// <summary>Reflection of every compiled stage, as a JSON document.</summary>
		public string ReflectionJson { get; }

		public int CompiledStageCount
		{
			get
			{
				int count = 0;
				foreach (CompiledShaderStage? stage in stages)
				{
					count += stage != null ? 1 : 0;
				}
				return count;
			}
		}

		public CompiledShaderStage? GetStage(ShaderStage stage) => stages[(int)stage];

		internal void SetStage(ShaderStage stage, CompiledShaderStage compiledStage) =>
			stages[(int)stage] = compiledStage;
	}

	/// <summary>
	/// Managed front end of the engine's HLSL cross compiler (HLSLCC).
	///
	/// A shader is ONE HLSL file that holds the entry point of every stage -
	/// PassVertex for the vertex stage and PassFragment for the fragment stage by
	/// default. Compiling it produces one SPIR-V module per stage plus reflection
	/// information, exactly like running HLSLCC.exe over the same file offline.
	///
	/// The Vulkan HLSL namespace and the engine's own attribute shorthands are
	/// injected by the compiler, so shader files never include or declare them.
	/// </summary>
	public static class ShaderCompiler
	{
		/// <summary>Name of the vertex entry point a shader defines by default.</summary>
		public const string DefaultVertexEntryPoint = "PassVertex";

		/// <summary>Name of the fragment entry point a shader defines by default.</summary>
		public const string DefaultFragmentEntryPoint = "PassFragment";

		/// <summary>True when the machine can compile HLSL (dxcompiler.dll available).</summary>
		public static bool IsAvailable(out string? errorText)
		{
			byte[] errorBuffer = new byte[1024];
			int isAvailable = NativeApi.VspShader_IsCompilerAvailable(errorBuffer, errorBuffer.Length);
			errorText = isAvailable != 0 ? null : ReadText(errorBuffer);
			return isAvailable != 0;
		}

		/// <summary>Compiles HLSL source the caller holds in memory.</summary>
		public static CompiledShader? CompileSource(string sourceText, string sourceName)
		{
			if (string.IsNullOrEmpty(sourceText))
			{
				Debug.LogError("Rendering.ShaderCompiler: the shader source is empty.");
				return null;
			}

			byte[] errorBuffer = new byte[4096];
			int compiled = NativeApi.VspShader_CompileFromSource(sourceText, sourceName, errorBuffer, errorBuffer.Length);
			if (compiled == 0)
			{
				Debug.LogError("Rendering.ShaderCompiler: " + ReadText(errorBuffer));
				return null;
			}
			return CollectResult(sourceName);
		}

		/// <summary>Reads a shader file and compiles it.</summary>
		public static CompiledShader? CompileFile(string filePath)
		{
			if (string.IsNullOrEmpty(filePath))
			{
				Debug.LogError("Rendering.ShaderCompiler: the shader file path is empty.");
				return null;
			}

			byte[] errorBuffer = new byte[4096];
			int compiled = NativeApi.VspShader_CompileFromFile(filePath, errorBuffer, errorBuffer.Length);
			if (compiled == 0)
			{
				Debug.LogError("Rendering.ShaderCompiler: " + ReadText(errorBuffer));
				return null;
			}
			return CollectResult(filePath);
		}

		/// <summary>
		/// Picks the result of the last compilation up stage by stage. The native
		/// side keeps it alive until the next compile, so it is copied out here.
		/// </summary>
		private static CompiledShader? CollectResult(string sourceName)
		{
			byte[] reflectionBuffer = new byte[64 * 1024];
			NativeApi.VspShader_GetReflectionJson(reflectionBuffer, reflectionBuffer.Length);

			CompiledShader compiledShader = new CompiledShader(sourceName, ReadText(reflectionBuffer));

			int stageCount = NativeApi.VspShader_GetCompiledStageCount();
			if (stageCount <= 0)
			{
				Debug.LogError("Rendering.ShaderCompiler: the compiler produced no stage for '" + sourceName + "'.");
				return null;
			}

			foreach (ShaderStage stage in new[] { ShaderStage.Vertex, ShaderStage.Fragment })
			{
				int byteCount = NativeApi.VspShader_GetStageSpirvByteCount((int)stage);
				if (byteCount <= 0)
				{
					continue;
				}

				byte[] spirvCode = new byte[byteCount];
				int copiedByteCount = NativeApi.VspShader_CopyStageSpirv((int)stage, spirvCode, (uint)byteCount);
				if (copiedByteCount != byteCount)
				{
					Debug.LogError("Rendering.ShaderCompiler: the " + stage + " module could not be copied out.");
					continue;
				}

				byte[] entryPointBuffer = new byte[256];
				NativeApi.VspShader_GetStageEntryPointName((int)stage, entryPointBuffer, entryPointBuffer.Length);
				string entryPointName = ReadText(entryPointBuffer);

				uint[] summaryValues = new uint[6];
				NativeApi.VspShader_GetStageReflectionSummary((int)stage, summaryValues);

				compiledShader.SetStage(
					stage,
					new CompiledShaderStage(stage, entryPointName, spirvCode, new ShaderStageReflectionSummary(summaryValues)));
			}

			return compiledShader;
		}

		private static string ReadText(byte[] utf8Buffer)
		{
			int byteCount = Array.IndexOf(utf8Buffer, (byte)0);
			if (byteCount < 0)
			{
				byteCount = utf8Buffer.Length;
			}
			return byteCount > 0 ? Encoding.UTF8.GetString(utf8Buffer, 0, byteCount) : string.Empty;
		}
	}
}
