namespace VspEngine.Rendering
{
	/// <summary>
	/// Reflection summary of one compiled shader stage: what a pipeline has to
	/// match when it builds its state. HLSLCC reports these counters next to the
	/// SPIR-V modules it writes, and <see cref="VspEngine.Shader"/> reads them back
	/// when the engine loads that shader.
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

		/// <summary>
		/// Builds a summary from the values VspShaderAsset_GetStageReflectionSummary
		/// wrote: input count, output count, resource count, push-constant member
		/// count, push-constant size.
		/// </summary>
		public ShaderStageReflectionSummary(uint[] values)
		{
			InputCount = values.Length > 0 ? values[0] : 0;
			OutputCount = values.Length > 1 ? values[1] : 0;
			ResourceCount = values.Length > 2 ? values[2] : 0;
			PushConstantMemberCount = values.Length > 3 ? values[3] : 0;
			PushConstantByteSize = values.Length > 4 ? values[4] : 0;
		}
	}
}
