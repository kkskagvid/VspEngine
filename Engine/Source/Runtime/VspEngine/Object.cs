using System;
using System.Text;

namespace VspEngine
{
	/// <summary>
	/// Base class of every managed engine object.
	///
	/// A managed engine object is a REFERENCE HANDLE: it stores nothing but the
	/// handle of the native object it addresses, and every property access is
	/// forwarded to the native scene (Classes/Scene in VspCore). Destroying the
	/// native object makes the handle stop resolving, which <see cref="IsValid"/>
	/// reports.
	/// </summary>
	public abstract class Object
	{
		/// <summary>
		/// Handle of the native object this managed object refers to (0 = invalid).
		/// </summary>
		public uint NativeHandle { get; internal set; }

		/// <summary>True while the handle still resolves to a live native object.</summary>
		public bool IsValid => NativeHandle != 0;

		/// <summary>Name stored on the native object.</summary>
		public string Name
		{
			get => ReadNativeName(NativeHandle);
			set => NativeApi.VspObject_SetName(NativeHandle, value);
		}

		public override string ToString()
		{
			return GetType().Name + "(handle=" + NativeHandle + ", name=\"" + ReadNativeName(NativeHandle) + "\")";
		}

		/// <summary>
		/// Reads the native object's name as UTF-8. The native side copies at
		/// most the buffer's capacity minus one byte and always terminates it.
		/// </summary>
		private static string ReadNativeName(uint nativeHandle)
		{
			if (nativeHandle == 0)
			{
				return string.Empty;
			}

			byte[] utf8Buffer = new byte[256];
			int byteCount = NativeApi.VspObject_GetName(nativeHandle, utf8Buffer, utf8Buffer.Length);
			return byteCount > 0 ? Encoding.UTF8.GetString(utf8Buffer, 0, byteCount) : string.Empty;
		}
	}
}
