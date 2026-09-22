using System;
using System.Runtime.InteropServices;

namespace VspEngine.Rendering
{
	/// <summary>
	/// An index buffer: the list of vertex numbers an indexed draw walks. A
	/// 3D mesh is normally drawn this way, because a cube's eight corners are
	/// shared by its twelve triangles.
	///
	/// Indices are 32-bit, which is what the backend binds
	/// (<c>VK_INDEX_TYPE_UINT32</c>), so any vertex count fits.
	/// </summary>
	public sealed class IndexBuffer : IDisposable
	{
		private uint nativeHandle;

		/// <summary>Allocates an index buffer holding the given number of indices.</summary>
		public IndexBuffer(uint indexCount, bool isDynamic = false)
		{
			if (indexCount == 0)
			{
				throw new ArgumentOutOfRangeException(nameof(indexCount), "An index buffer needs at least one index.");
			}

			nativeHandle = RhiApi.VspRhi_CreateBuffer(indexCount * sizeof(uint), (int)BufferUsage.Index, isDynamic ? 1 : 0);
		}

		/// <summary>Native buffer handle (0 = invalid).</summary>
		public uint NativeHandle => nativeHandle;

		public bool IsValid => nativeHandle != 0;

		/// <summary>Uploads indices at the start of the buffer.</summary>
		public bool Update(ReadOnlySpan<uint> indices) => Update(0, indices);

		/// <summary>Uploads indices at the given byte offset of the buffer.</summary>
		public bool Update(uint byteOffset, ReadOnlySpan<uint> indices)
		{
			if (nativeHandle == 0 || indices.Length == 0)
			{
				return false;
			}

			byte[] indexBytes = MemoryMarshal.AsBytes(indices).ToArray();
			return RhiApi.VspRhi_UpdateBuffer(nativeHandle, byteOffset, indexBytes, (uint)indexBytes.Length) != 0;
		}

		/// <summary>Uploads a whole triangle list (three indices per triangle).</summary>
		public bool Update(int[] indices)
		{
			if (indices == null)
			{
				throw new ArgumentNullException(nameof(indices));
			}
			return Update(new ReadOnlySpan<uint>(ToUInt32(indices)));
		}

		public void Dispose()
		{
			if (nativeHandle != 0)
			{
				RhiApi.VspRhi_DestroyBuffer(nativeHandle);
				nativeHandle = 0;
			}
		}

		private static uint[] ToUInt32(int[] indices)
		{
			uint[] converted = new uint[indices.Length];
			for (int index = 0; index < indices.Length; ++index)
			{
				converted[index] = (uint)indices[index];
			}
			return converted;
		}
	}
}
