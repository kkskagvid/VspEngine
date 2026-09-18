using System;
using System.Text;

using VspEngine.Rendering;

namespace VspEngine
{
	/// <summary>What a shader property holds.</summary>
	public enum ShaderPropertyType
	{
		Float = 0,
		Vector = 1,
		Color = 2,
		Texture = 3,
	}

	/// <summary>How a shader keyword group treats the variants nobody uses.</summary>
	public enum ShaderKeywordKind
	{
		/// <summary>Strippable, global keyword (#pragma variant).</summary>
		Variant = 0,

		/// <summary>Strippable, local keyword (#pragma variant_local).</summary>
		VariantLocal = 1,

		/// <summary>Always kept, global keyword (#pragma multi_variant).</summary>
		MultiVariant = 2,

		/// <summary>Always kept, local keyword (#pragma multi_variant_local).</summary>
		MultiVariantLocal = 3,
	}

	/// <summary>One value a material can set on a shader.</summary>
	public readonly struct ShaderPropertyInfo
	{
		public readonly string Name;
		public readonly string DisplayName;
		public readonly ShaderPropertyType Type;
		public readonly float[] DefaultValues;

		internal ShaderPropertyInfo(string name, string displayName, ShaderPropertyType type, float[] defaultValues)
		{
			Name = name;
			DisplayName = displayName;
			Type = type;
			DefaultValues = defaultValues;
		}
	}

	/// <summary>One keyword group of a shader and the states it can be in.</summary>
	public readonly struct ShaderKeywordGroupInfo
	{
		public readonly string Name;
		public readonly ShaderKeywordKind Kind;
		public readonly bool IsStrippable;
		public readonly string[] KeywordStates;

		internal ShaderKeywordGroupInfo(string name, ShaderKeywordKind kind, bool isStrippable, string[] keywordStates)
		{
			Name = name;
			Kind = kind;
			IsStrippable = isStrippable;
			KeywordStates = keywordStates;
		}
	}

	/// <summary>
	/// Reference handle for a native shader asset.
	///
	/// A shader is what HLSLCC produced from a .vsf file: one SPIR-V module per
	/// stage, for every variant the build kept. <see cref="Load"/> reads the
	/// manifest and the modules the compiler wrote next to the executable, so the
	/// engine never compiles HLSL at runtime unless a game asks it to.
	/// </summary>
	public sealed class Shader : Object
	{
		internal Shader(uint nativeHandle)
		{
			NativeHandle = nativeHandle;
		}

		/// <summary>Name of the shader inside its .vsf file ("Vsp/Triangle2D").</summary>
		public string ShaderName =>
			ReadText((buffer, capacity) => NativeApi.VspShaderAsset_GetShaderName(NativeHandle, buffer, capacity));

		/// <summary>Render queue the shader belongs to.</summary>
		public int RenderQueue => NativeApi.VspShaderAsset_GetRenderQueue(NativeHandle);

		/// <summary>Number of variants the build kept for this shader.</summary>
		public int VariantCount => NativeApi.VspShaderAsset_GetVariantCount(NativeHandle);

		/// <summary>Number of values a material can set on this shader.</summary>
		public int PropertyCount => NativeApi.VspShaderAsset_GetPropertyCount(NativeHandle);

		/// <summary>Number of keyword groups that select one of its variants.</summary>
		public int KeywordGroupCount => NativeApi.VspShaderAsset_GetKeywordGroupCount(NativeHandle);

		/// <summary>
		/// Loads a compiled shader by name: "&lt;name&gt;.shader.json" and the
		/// SPIR-V modules it names, from the engine's shader directory.
		/// </summary>
		public static Shader? Load(string shaderName)
		{
			if (string.IsNullOrEmpty(shaderName))
			{
				Debug.LogError("Shader.Load: the shader name is empty.");
				return null;
			}

			byte[] errorBuffer = new byte[1024];
			uint nativeHandle = NativeApi.VspShaderAsset_Load(shaderName, errorBuffer, errorBuffer.Length);
			if (nativeHandle == 0)
			{
				Debug.LogError("Shader.Load('" + shaderName + "'): " + ReadText(errorBuffer));
				return null;
			}
			return new Shader(nativeHandle);
		}

		/// <summary>Overrides the directory compiled shaders are loaded from.</summary>
		public static void SetShaderDirectory(string? directory) =>
			NativeApi.VspShaderLibrary_SetShaderDirectory(directory);

		/// <summary>Keyword names of one variant, joined by '+' (empty for the default variant).</summary>
		public string GetVariantKey(int variantIndex) =>
			ReadText((buffer, capacity) => NativeApi.VspShaderAsset_GetVariantKey(NativeHandle, (uint)variantIndex, buffer, capacity));

		/// <summary>Entry point the given stage of a variant was compiled from.</summary>
		public string GetEntryPointName(int variantIndex, Rendering.ShaderStage stage) =>
			ReadText((buffer, capacity) =>
				NativeApi.VspShaderAsset_GetEntryPointName(NativeHandle, (uint)variantIndex, (int)stage, buffer, capacity));

		/// <summary>Size of the SPIR-V module of one stage in bytes.</summary>
		public int GetStageSpirvSize(int variantIndex, Rendering.ShaderStage stage) =>
			NativeApi.VspShaderAsset_GetStageSpirvByteCount(NativeHandle, (uint)variantIndex, (int)stage);

		/// <summary>Copies the SPIR-V module of one stage into the given buffer.</summary>
		public bool CopyStageSpirv(int variantIndex, Rendering.ShaderStage stage, byte[] buffer)
		{
			if (buffer == null)
			{
				throw new ArgumentNullException(nameof(buffer));
			}
			return NativeApi.VspShaderAsset_CopyStageSpirv(
				NativeHandle, (uint)variantIndex, (int)stage, buffer, (uint)buffer.Length) > 0;
		}

		/// <summary>Reflection counters of one stage, as HLSLCC reported them.</summary>
		public ShaderStageReflectionSummary GetStageReflection(int variantIndex, Rendering.ShaderStage stage)
		{
			uint[] values = new uint[5];
			NativeApi.VspShaderAsset_GetStageReflectionSummary(NativeHandle, (uint)variantIndex, (int)stage, values);
			return new ShaderStageReflectionSummary(values);
		}

		/// <summary>Describes one of the shader's properties.</summary>
		public ShaderPropertyInfo? GetProperty(int propertyIndex)
		{
			byte[] nameBuffer = new byte[128];
			byte[] displayNameBuffer = new byte[256];
			int[] typeValue = new int[1];
			float[] defaultValues = new float[4];

			int found = NativeApi.VspShaderAsset_GetProperty(
				NativeHandle, (uint)propertyIndex, nameBuffer, nameBuffer.Length,
				displayNameBuffer, displayNameBuffer.Length, typeValue, defaultValues);
			if (found == 0)
			{
				return null;
			}

			return new ShaderPropertyInfo(
				ReadText(nameBuffer), ReadText(displayNameBuffer), (ShaderPropertyType)typeValue[0], defaultValues);
		}

		/// <summary>Describes one of the shader's keyword groups.</summary>
		public ShaderKeywordGroupInfo? GetKeywordGroup(int groupIndex)
		{
			byte[] nameBuffer = new byte[128];
			int[] kindValue = new int[1];
			int[] strippableValue = new int[1];

			int stateCount = NativeApi.VspShaderAsset_GetKeywordGroup(
				NativeHandle, (uint)groupIndex, nameBuffer, nameBuffer.Length, kindValue, strippableValue);
			if (stateCount <= 0)
			{
				return null;
			}

			string[] keywordStates = new string[stateCount];
			for (int stateIndex = 0; stateIndex < stateCount; ++stateIndex)
			{
				byte[] stateBuffer = new byte[128];
				NativeApi.VspShaderAsset_GetKeywordGroupState(
					NativeHandle, (uint)groupIndex, (uint)stateIndex, stateBuffer, stateBuffer.Length);
				keywordStates[stateIndex] = ReadText(stateBuffer);
			}

			return new ShaderKeywordGroupInfo(
				ReadText(nameBuffer), (ShaderKeywordKind)kindValue[0], strippableValue[0] != 0, keywordStates);
		}

		internal delegate int TextReader(byte[] buffer, int capacity);

		internal static string ReadText(TextReader reader)
		{
			byte[] buffer = new byte[512];
			int byteCount = reader(buffer, buffer.Length);
			return ReadText(buffer, byteCount);
		}

		internal static string ReadText(byte[] utf8Buffer)
		{
			return ReadText(utf8Buffer, -1);
		}

		internal static string ReadText(byte[] utf8Buffer, int byteCount)
		{
			if (byteCount < 0)
			{
				byteCount = Array.IndexOf(utf8Buffer, (byte)0);
				if (byteCount < 0)
				{
					byteCount = utf8Buffer.Length;
				}
			}
			return byteCount > 0 ? Encoding.UTF8.GetString(utf8Buffer, 0, byteCount) : string.Empty;
		}
	}
}
