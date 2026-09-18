using System;
using System.Numerics;

namespace VspEngine
{
	/// <summary>
	/// Reference handle for a native material: the shader a drawable renders with,
	/// the values it gives that shader's properties and the keywords it turns on.
	///
	/// One shader can be drawn many different ways, so a material is what a
	/// renderable component actually points at.
	/// </summary>
	public sealed class Material : Object
	{
		/// <summary>Property the demo scripts use to pick the triangle's color.</summary>
		public const string ColorModePropertyName = "_ColorMode";

		/// <summary>Property that tints whatever the shader outputs.</summary>
		public const string TintPropertyName = "_Tint";

		internal Material(uint nativeHandle)
		{
			NativeHandle = nativeHandle;
		}

		/// <summary>Creates a material for a shader; its values start as the shader's defaults.</summary>
		public static Material? Create(Shader shader)
		{
			if (shader == null)
			{
				throw new ArgumentNullException(nameof(shader));
			}

			byte[] errorBuffer = new byte[512];
			uint nativeHandle = NativeApi.VspMaterial_Create(shader.NativeHandle, errorBuffer, errorBuffer.Length);
			if (nativeHandle == 0)
			{
				Debug.LogError("Material.Create: " + Shader.ReadText(errorBuffer));
				return null;
			}
			return new Material(nativeHandle);
		}

		/// <summary>The shader this material draws with.</summary>
		public Shader? Shader
		{
			get
			{
				uint shaderHandle = NativeApi.VspMaterial_GetShader(NativeHandle);
				return shaderHandle != 0 ? new Shader(shaderHandle) : null;
			}
			set
			{
				if (value == null)
				{
					throw new ArgumentNullException(nameof(value));
				}
				NativeApi.VspMaterial_SetShader(NativeHandle, value.NativeHandle);
			}
		}

		/// <summary>Number of values this material carries.</summary>
		public int PropertyCount => NativeApi.VspMaterial_GetPropertyCount(NativeHandle);

		/// <summary>Name of the value at the given index.</summary>
		public string GetPropertyName(int valueIndex)
		{
			byte[] buffer = new byte[128];
			NativeApi.VspMaterial_GetPropertyName(NativeHandle, (uint)valueIndex, buffer, buffer.Length);
			return Shader.ReadText(buffer);
		}

		/// <summary>Sets a float property; false when the shader has no such property.</summary>
		public bool SetFloat(string propertyName, float value) =>
			NativeApi.VspMaterial_SetFloat(NativeHandle, propertyName, value) != 0;

		/// <summary>Reads a float property.</summary>
		public float GetFloat(string propertyName, float fallback = 0.0f)
		{
			float[] value = new float[1];
			return NativeApi.VspMaterial_GetFloat(NativeHandle, propertyName, value) != 0 ? value[0] : fallback;
		}

		/// <summary>Sets a vector or color property.</summary>
		public bool SetVector(string propertyName, Vector4 value) =>
			NativeApi.VspMaterial_SetVector(NativeHandle, propertyName, value.X, value.Y, value.Z, value.W) != 0;

		/// <summary>Reads a vector or color property.</summary>
		public Vector4 GetVector(string propertyName, Vector4 fallback = default)
		{
			float[] values = new float[4];
			if (NativeApi.VspMaterial_GetVector(NativeHandle, propertyName, values) == 0)
			{
				return fallback;
			}
			return new Vector4(values[0], values[1], values[2], values[3]);
		}

		/// <summary>Turns one of the shader's keywords on or off, selecting a variant.</summary>
		public bool SetKeywordEnabled(string keyword, bool isEnabled) =>
			NativeApi.VspMaterial_SetKeywordEnabled(NativeHandle, keyword, isEnabled ? 1 : 0) != 0;

		/// <summary>True while the keyword is turned on.</summary>
		public bool IsKeywordEnabled(string keyword) =>
			NativeApi.VspMaterial_IsKeywordEnabled(NativeHandle, keyword) != 0;

		/// <summary>Index of the shader variant this material's keywords select (-1 = none).</summary>
		public int ResolveVariantIndex() => NativeApi.VspMaterial_ResolveVariantIndex(NativeHandle);

		/// <summary>Releases the native material.</summary>
		public void Destroy()
		{
			NativeApi.VspMaterial_Destroy(NativeHandle);
			NativeHandle = 0;
		}
	}
}
