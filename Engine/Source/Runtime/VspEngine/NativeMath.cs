using System.Numerics;
using System.Runtime.InteropServices;

namespace VspEngine
{
	/// <summary>
	/// Raw P/Invoke bindings of the native vector and matrix maths
	/// (VspMath_* in VspCore). See <see cref="NativeMath"/> for the managed
	/// facade that is meant to be used.
	/// </summary>
	internal static class NativeMathApi
	{
		private const string LibraryName = "VspCore";

		// ---- Vector2 ----
		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspMath_Vector2Add([In] float[] left, [In] float[] right, [Out] float[] outValues);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspMath_Vector2Subtract([In] float[] left, [In] float[] right, [Out] float[] outValues);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspMath_Vector2Scale([In] float[] value, float scalar, [Out] float[] outValues);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern float VspMath_Vector2Dot([In] float[] left, [In] float[] right);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern float VspMath_Vector2Length([In] float[] value);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspMath_Vector2Normalize([In] float[] value, [Out] float[] outValues);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspMath_Vector2Lerp([In] float[] from, [In] float[] to, float factor, [Out] float[] outValues);

		// ---- Vector3 ----
		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspMath_Vector3Add([In] float[] left, [In] float[] right, [Out] float[] outValues);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspMath_Vector3Subtract([In] float[] left, [In] float[] right, [Out] float[] outValues);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspMath_Vector3Scale([In] float[] value, float scalar, [Out] float[] outValues);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern float VspMath_Vector3Dot([In] float[] left, [In] float[] right);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspMath_Vector3Cross([In] float[] left, [In] float[] right, [Out] float[] outValues);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern float VspMath_Vector3Length([In] float[] value);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern float VspMath_Vector3Distance([In] float[] left, [In] float[] right);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspMath_Vector3Normalize([In] float[] value, [Out] float[] outValues);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspMath_Vector3Lerp([In] float[] from, [In] float[] to, float factor, [Out] float[] outValues);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspMath_Vector3FromSpherical(float yawRadians, float pitchRadians, [Out] float[] outValues);

		// ---- Vector4 ----
		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspMath_Vector4Add([In] float[] left, [In] float[] right, [Out] float[] outValues);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern float VspMath_Vector4Dot([In] float[] left, [In] float[] right);

		// ---- Matrix4x4 (column-major, 16 floats) ----
		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspMath_Matrix4x4Identity([Out] float[] outValues);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspMath_Matrix4x4Multiply([In] float[] left, [In] float[] right, [Out] float[] outValues);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspMath_Matrix4x4Inverse([In] float[] value, [Out] float[] outValues);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspMath_Matrix4x4Transpose([In] float[] value, [Out] float[] outValues);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern float VspMath_Matrix4x4Determinant([In] float[] value);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspMath_Matrix4x4Trs([In] float[] position, [In] float[] eulerDegrees, [In] float[] scale, [Out] float[] outValues);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspMath_Matrix4x4RotationEuler([In] float[] eulerDegrees, [Out] float[] outValues);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspMath_Matrix4x4LookAt([In] float[] eye, [In] float[] target, [In] float[] up, [Out] float[] outValues);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspMath_Matrix4x4Perspective(float fieldOfViewDegrees, float aspect, float near, float far, [Out] float[] outValues);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspMath_Matrix4x4Orthographic(float halfHeight, float aspect, float near, float far, [Out] float[] outValues);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspMath_Matrix4x4OrthographicPixelSpace(float width, float height, [Out] float[] outValues);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspMath_Matrix4x4TransformPoint([In] float[] matrix, [In] float[] point, [Out] float[] outValues);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspMath_Matrix4x4TransformDirection([In] float[] matrix, [In] float[] direction, [Out] float[] outValues);

		// ---- Quaternion ----
		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspMath_QuaternionFromEuler([In] float[] eulerDegrees, [Out] float[] outValues);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspMath_QuaternionToEuler([In] float[] quaternion, [Out] float[] outValues);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspMath_QuaternionMultiply([In] float[] left, [In] float[] right, [Out] float[] outValues);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspMath_QuaternionRotateVector([In] float[] quaternion, [In] float[] direction, [Out] float[] outValues);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspMath_QuaternionSlerp([In] float[] from, [In] float[] to, float factor, [Out] float[] outValues);
	}

	/// <summary>
	/// Managed facade over the engine's NATIVE vector and matrix maths
	/// (Core/Math in VspCore). The maths lives in C++ so the engine has one
	/// implementation of its conventions - right-handed, Y up, an object looking
	/// down its local -Z, column-major matrices, Euler angles in degrees applied
	/// Z then Y then X, Vulkan clip space - instead of a second one that could
	/// drift away from it.
	///
	/// Use this where the engine's convention matters (a projection the engine
	/// will compare against, a pixel-space UI transform, an axis conversion).
	/// Plain everyday arithmetic on <see cref="Vector3"/> stays cheaper in
	/// <c>System.Numerics</c>, which is why the camera rig mixes the two.
	///
	/// Every method is a thin call; nothing here allocates beyond the small
	/// scratch arrays the interop needs.
	/// </summary>
	public static class NativeMath
	{
		private static readonly float[] scratch3A = new float[3];
		private static readonly float[] scratch3B = new float[3];
		private static readonly float[] scratch3C = new float[3];
		private static readonly float[] scratch4A = new float[4];
		private static readonly float[] scratch4B = new float[4];
		private static readonly float[] scratch16A = new float[16];
		private static readonly float[] scratch16B = new float[16];
		private static readonly float[] scratch16C = new float[16];

		/// <summary>Turns an angle in degrees into radians.</summary>
		public const float DegreesToRadians = 3.14159265358979f / 180.0f;

		/// <summary>Turns an angle in radians into degrees.</summary>
		public const float RadiansToDegrees = 180.0f / 3.14159265358979f;

		// -----------------------------------------------------------------
		// Vector3
		// -----------------------------------------------------------------

		public static Vector3 Add(Vector3 left, Vector3 right)
		{
			Write(left, scratch3A);
			Write(right, scratch3B);
			NativeMathApi.VspMath_Vector3Add(scratch3A, scratch3B, scratch3C);
			return Read(scratch3C);
		}

		public static Vector3 Subtract(Vector3 left, Vector3 right)
		{
			Write(left, scratch3A);
			Write(right, scratch3B);
			NativeMathApi.VspMath_Vector3Subtract(scratch3A, scratch3B, scratch3C);
			return Read(scratch3C);
		}

		public static Vector3 Scale(Vector3 value, float scalar)
		{
			Write(value, scratch3A);
			NativeMathApi.VspMath_Vector3Scale(scratch3A, scalar, scratch3C);
			return Read(scratch3C);
		}

		public static float Dot(Vector3 left, Vector3 right)
		{
			Write(left, scratch3A);
			Write(right, scratch3B);
			return NativeMathApi.VspMath_Vector3Dot(scratch3A, scratch3B);
		}

		public static Vector3 Cross(Vector3 left, Vector3 right)
		{
			Write(left, scratch3A);
			Write(right, scratch3B);
			NativeMathApi.VspMath_Vector3Cross(scratch3A, scratch3B, scratch3C);
			return Read(scratch3C);
		}

		public static float Length(Vector3 value)
		{
			Write(value, scratch3A);
			return NativeMathApi.VspMath_Vector3Length(scratch3A);
		}

		public static float Distance(Vector3 left, Vector3 right)
		{
			Write(left, scratch3A);
			Write(right, scratch3B);
			return NativeMathApi.VspMath_Vector3Distance(scratch3A, scratch3B);
		}

		/// <summary>
		/// Unit vector in the same direction. A zero-length vector has no
		/// direction, so the native empty value - (0, 0, 0) - comes back.
		/// </summary>
		public static Vector3 Normalize(Vector3 value)
		{
			Write(value, scratch3A);
			NativeMathApi.VspMath_Vector3Normalize(scratch3A, scratch3C);
			return Read(scratch3C);
		}

		public static Vector3 Lerp(Vector3 from, Vector3 to, float factor)
		{
			Write(from, scratch3A);
			Write(to, scratch3B);
			NativeMathApi.VspMath_Vector3Lerp(scratch3A, scratch3B, factor, scratch3C);
			return Read(scratch3C);
		}

		/// <summary>
		/// The direction a yaw/pitch pair points along, both in radians, with
		/// yaw 0 looking down the engine's forward (-Z) and positive pitch
		/// rising above the horizon.
		/// </summary>
		public static Vector3 FromSpherical(float yawRadians, float pitchRadians)
		{
			NativeMathApi.VspMath_Vector3FromSpherical(yawRadians, pitchRadians, scratch3C);
			return Read(scratch3C);
		}

		// -----------------------------------------------------------------
		// Matrix4x4
		// -----------------------------------------------------------------

		/// <summary>
		/// The projection a UI layout draws through: pixel coordinates with the
		/// origin at the top-left corner and +Y pointing down, at depth 0.
		/// </summary>
		public static Matrix4x4 BuildOrthographicPixelSpace(float width, float height)
		{
			NativeMathApi.VspMath_Matrix4x4OrthographicPixelSpace(width, height, scratch16A);
			return ToMatrix4x4(scratch16A);
		}

		/// <summary>The engine's perspective projection (Vulkan clip space).</summary>
		public static Matrix4x4 BuildPerspective(float fieldOfViewDegrees, float aspect, float near, float far)
		{
			NativeMathApi.VspMath_Matrix4x4Perspective(fieldOfViewDegrees, aspect, near, far, scratch16A);
			return ToMatrix4x4(scratch16A);
		}

		/// <summary>The engine's orthographic projection (Vulkan clip space).</summary>
		public static Matrix4x4 BuildOrthographic(float halfHeight, float aspect, float near, float far)
		{
			NativeMathApi.VspMath_Matrix4x4Orthographic(halfHeight, aspect, near, far, scratch16A);
			return ToMatrix4x4(scratch16A);
		}

		/// <summary>
		/// A view matrix that looks from <paramref name="eye"/> towards
		/// <paramref name="target"/> - what a camera rig that always keeps its
		/// subject centred builds from.
		/// </summary>
		public static Matrix4x4 BuildLookAt(Vector3 eye, Vector3 target, Vector3 up)
		{
			Write(eye, scratch3A);
			Write(target, scratch3B);
			Write(up, scratch3C);
			NativeMathApi.VspMath_Matrix4x4LookAt(scratch3A, scratch3B, scratch3C, scratch16A);
			return ToMatrix4x4(scratch16A);
		}

		/// <summary>Translation * rotation * scale, the local transform of an object.</summary>
		public static Matrix4x4 BuildTrs(Vector3 position, Vector3 eulerDegrees, Vector3 scale)
		{
			Write(position, scratch3A);
			Write(eulerDegrees, scratch3B);
			Write(scale, scratch3C);
			NativeMathApi.VspMath_Matrix4x4Trs(scratch3A, scratch3B, scratch3C, scratch16A);
			return ToMatrix4x4(scratch16A);
		}

		public static Matrix4x4 RotationEuler(Vector3 eulerDegrees)
		{
			Write(eulerDegrees, scratch3A);
			NativeMathApi.VspMath_Matrix4x4RotationEuler(scratch3A, scratch16A);
			return ToMatrix4x4(scratch16A);
		}

		public static Matrix4x4 Multiply(Matrix4x4 left, Matrix4x4 right)
		{
			Write(left, scratch16A);
			Write(right, scratch16B);
			NativeMathApi.VspMath_Matrix4x4Multiply(scratch16A, scratch16B, scratch16C);
			return ToMatrix4x4(scratch16C);
		}

		/// <summary>
		/// The inverse, or a zero matrix when the input is singular - the native
		/// empty value - in which case the return value is false.
		/// </summary>
		public static bool TryInvert(Matrix4x4 value, out Matrix4x4 inverse)
		{
			Write(value, scratch16A);
			int succeeded = NativeMathApi.VspMath_Matrix4x4Inverse(scratch16A, scratch16C);
			inverse = ToMatrix4x4(scratch16C);
			return succeeded != 0;
		}

		public static Matrix4x4 Transpose(Matrix4x4 value)
		{
			Write(value, scratch16A);
			NativeMathApi.VspMath_Matrix4x4Transpose(scratch16A, scratch16C);
			return ToMatrix4x4(scratch16C);
		}

		public static float Determinant(Matrix4x4 value)
		{
			Write(value, scratch16A);
			return NativeMathApi.VspMath_Matrix4x4Determinant(scratch16A);
		}

		public static Vector3 TransformPoint(Matrix4x4 matrix, Vector3 point)
		{
			Write(matrix, scratch16A);
			Write(point, scratch3A);
			NativeMathApi.VspMath_Matrix4x4TransformPoint(scratch16A, scratch3A, scratch3C);
			return Read(scratch3C);
		}

		public static Vector3 TransformDirection(Matrix4x4 matrix, Vector3 direction)
		{
			Write(matrix, scratch16A);
			Write(direction, scratch3A);
			NativeMathApi.VspMath_Matrix4x4TransformDirection(scratch16A, scratch3A, scratch3C);
			return Read(scratch3C);
		}

		// -----------------------------------------------------------------
		// Quaternion
		// -----------------------------------------------------------------

		public static Quaternion QuaternionFromEuler(Vector3 eulerDegrees)
		{
			Write(eulerDegrees, scratch3A);
			NativeMathApi.VspMath_QuaternionFromEuler(scratch3A, scratch4A);
			return new Quaternion(scratch4A[0], scratch4A[1], scratch4A[2], scratch4A[3]);
		}

		public static Vector3 QuaternionToEuler(Quaternion value)
		{
			scratch4A[0] = value.X;
			scratch4A[1] = value.Y;
			scratch4A[2] = value.Z;
			scratch4A[3] = value.W;
			NativeMathApi.VspMath_QuaternionToEuler(scratch4A, scratch3C);
			return Read(scratch3C);
		}

		/// <summary>Takes a direction through a rotation.</summary>
		public static Vector3 QuaternionRotateVector(Quaternion rotation, Vector3 direction)
		{
			WriteQuaternion(rotation, scratch4A);
			Write(direction, scratch3A);
			NativeMathApi.VspMath_QuaternionRotateVector(scratch4A, scratch3A, scratch3C);
			return Read(scratch3C);
		}

		/// <summary>The shortest turn between two rotations.</summary>
		public static Quaternion QuaternionSlerp(Quaternion from, Quaternion to, float factor)
		{
			WriteQuaternion(from, scratch4A);
			WriteQuaternion(to, scratch4B);
			NativeMathApi.VspMath_QuaternionSlerp(scratch4A, scratch4B, factor, scratch4A);
			return new Quaternion(scratch4A[0], scratch4A[1], scratch4A[2], scratch4A[3]);
		}

		private static void WriteQuaternion(Quaternion value, float[] destination)
		{
			destination[0] = value.X;
			destination[1] = value.Y;
			destination[2] = value.Z;
			destination[3] = value.W;
		}

		// -----------------------------------------------------------------
		// Marshalling helpers
		// -----------------------------------------------------------------

		private static void Write(Vector3 value, float[] destination)
		{
			destination[0] = value.X;
			destination[1] = value.Y;
			destination[2] = value.Z;
		}

		private static Vector3 Read(float[] source) => new Vector3(source[0], source[1], source[2]);

		/// <summary>
		/// Copies a matrix into the interop buffer in COLUMN-MAJOR order, which
		/// is what the native side stores and what Vulkan reads.
		/// <see cref="Matrix4x4"/> stores rows, so element (row, column) moves to
		/// the slot column * 4 + row.
		/// </summary>
		private static void Write(Matrix4x4 value, float[] destination)
		{
			destination[0] = value.M11; destination[1] = value.M21; destination[2] = value.M31; destination[3] = value.M41;
			destination[4] = value.M12; destination[5] = value.M22; destination[6] = value.M32; destination[7] = value.M42;
			destination[8] = value.M13; destination[9] = value.M23; destination[10] = value.M33; destination[11] = value.M43;
			destination[12] = value.M14; destination[13] = value.M24; destination[14] = value.M34; destination[15] = value.M44;
		}

		private static Matrix4x4 ToMatrix4x4(float[] columnMajorValues)
		{
			return new Matrix4x4(
				columnMajorValues[0], columnMajorValues[4], columnMajorValues[8], columnMajorValues[12],
				columnMajorValues[1], columnMajorValues[5], columnMajorValues[9], columnMajorValues[13],
				columnMajorValues[2], columnMajorValues[6], columnMajorValues[10], columnMajorValues[14],
				columnMajorValues[3], columnMajorValues[7], columnMajorValues[11], columnMajorValues[15]);
		}
	}
}
