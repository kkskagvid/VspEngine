using System;
using System.Runtime.InteropServices;

namespace VspEngine.Rendering
{
	/// <summary>
	/// A vertex buffer. Dynamic buffers stay host visible and are written
	/// directly; static ones live in device-local memory and are filled through
	/// a staging copy the native backend performs.
	/// </summary>
	public sealed class VertexBuffer : IDisposable
	{
		private uint nativeHandle;

		/// <summary>Allocates a vertex buffer of the given size in bytes.</summary>
		public VertexBuffer(uint byteSize, bool isDynamic = false)
		{
			if (byteSize == 0)
			{
				throw new ArgumentOutOfRangeException(nameof(byteSize), "A vertex buffer needs at least one byte.");
			}

			nativeHandle = RhiApi.VspRhi_CreateBuffer(byteSize, (int)BufferUsage.Vertex, isDynamic ? 1 : 0);
		}

		/// <summary>Native buffer handle (0 = invalid).</summary>
		public uint NativeHandle => nativeHandle;

		public bool IsValid => nativeHandle != 0;

		/// <summary>Uploads vertices at the start of the buffer.</summary>
		public bool Update<TVertex>(ReadOnlySpan<TVertex> vertices) where TVertex : struct
		{
			return Update(0, vertices);
		}

		/// <summary>Uploads vertices at the given byte offset of the buffer.</summary>
		public bool Update<TVertex>(uint byteOffset, ReadOnlySpan<TVertex> vertices) where TVertex : struct
		{
			if (nativeHandle == 0 || vertices.Length == 0)
			{
				return false;
			}

			byte[] vertexBytes = MemoryMarshal.AsBytes(vertices).ToArray();
			return RhiApi.VspRhi_UpdateBuffer(nativeHandle, byteOffset, vertexBytes, (uint)vertexBytes.Length) != 0;
		}

		public void Dispose()
		{
			if (nativeHandle != 0)
			{
				RhiApi.VspRhi_DestroyBuffer(nativeHandle);
				nativeHandle = 0;
			}
		}
	}
}
