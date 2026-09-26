using System.Numerics;
using System.Runtime.InteropServices;

namespace Assembly.Rendering
{
	/// <summary>
	/// The push-constant block the demo's 3D shader declares: the draw's world
	/// matrix, the material colour, the light and the ambient fill.
	///
	/// The layout and the explicit offsets must stay identical to the
	/// LitCubePushConstants block in Assembly/Shaders/LitCubeCommon.hlsl - the
	/// reflection HLSLCC produced for that block is what the pipeline reads back
	/// from the loaded shader, and what it sizes its pipeline with.
	/// </summary>
	[StructLayout(LayoutKind.Sequential, Pack = 4)]
	internal struct LitCubePushConstants
	{
		public Matrix4x4 WorldMatrix;    // offset 0   (row_major in the shader)
		public Vector4 BaseColor;        // offset 64
		public Vector4 LightDirection;   // offset 80  (xyz = travel direction, w = intensity)
		public Vector4 AmbientColor;     // offset 96  (rgb = ambient fill, a = unused)
	}
}
