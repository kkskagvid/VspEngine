using System;
using System.Runtime.InteropServices;
using System.Text;

namespace VspEngine
{
	/// <summary>What a JSON node holds, as <c>Core/Json/JsonReader</c> names it.</summary>
	public enum JsonValueType
	{
		/// <summary>The node is not a value at all (an invalid handle).</summary>
		Invalid = -1,
		Null = 0,
		Boolean = 1,
		Number = 2,
		String = 3,
		Array = 4,
		Object = 5,
	}

	/// <summary>
	/// Raw P/Invoke bindings of the engine's JSON reader (VspJson_* in VspCore).
	/// <see cref="JsonDocument"/> is the managed facade meant to be used.
	///
	/// The parser lives natively (<c>Core/Json/JsonReader</c>), so a manifest, a
	/// localisation catalog, a scene file and a UI layout are all read by the same
	/// code - there is exactly one JSON implementation in the engine, and one
	/// place where a malformed document is reported.
	/// </summary>
	internal static class JsonApi
	{
		private const string LibraryName = "VspCore";

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspJson_ParseFile([MarshalAs(UnmanagedType.LPUTF8Str)] string filePathUtf8);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspJson_ReleaseDocument(int documentHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspJson_GetLiveDocumentCount();

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspJson_GetRootNode(int documentHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspJson_GetNodeType(int nodeHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspJson_GetMemberCount(int nodeHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspJson_GetMemberNameUtf8(int nodeHandle, int memberIndex, byte[] bufferUtf8, int bufferCapacityBytes);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspJson_GetMemberNode(int nodeHandle, int memberIndex);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspJson_GetMemberNodeByName(int nodeHandle, [MarshalAs(UnmanagedType.LPUTF8Str)] string memberNameUtf8);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspJson_GetElementCount(int nodeHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspJson_GetElementNode(int nodeHandle, int elementIndex);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspJson_GetStringUtf8(int nodeHandle, byte[] bufferUtf8, int bufferCapacityBytes);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern double VspJson_GetNumber(int nodeHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspJson_GetBoolean(int nodeHandle);
	}

	/// <summary>
	/// One JSON document, read by the engine's native reader:
	///
	///     using JsonDocument layout = JsonDocument.Load("Ui/Hud.json");
	///     JsonNode title = layout.Root.GetMember("title");
	///     string text = title.GetString("(untitled)");
	///
	/// A document is a file that was parsed once; <see cref="Root"/> and every
	/// node below it are handles into that parse, valid until the document is
	/// disposed. Disposing releases the native tree.
	///
	/// Loading never throws: a document that cannot be read comes back as an
	/// invalid document with the reason already written to the engine log.
	/// </summary>
	public sealed class JsonDocument : IDisposable
	{
		// Text a node getter may write into. One page of UTF-8 holds a long string;
		// anything longer is truncated by the native side rather than failing.
		private const int TextBufferByteCount = 4096;

		private readonly int documentHandle;

		private JsonDocument(int handle)
		{
			documentHandle = handle;
		}

		/// <summary>Native document handle (0 = invalid).</summary>
		public int NativeHandle => documentHandle;

		/// <summary>True when a document was parsed and not yet disposed.</summary>
		public bool IsValid => documentHandle != 0;

		/// <summary>The parsed document's root value; an invalid node when there is none.</summary>
		public JsonNode Root => IsValid ? new JsonNode(JsonApi.VspJson_GetRootNode(documentHandle)) : default;

		/// <summary>
		/// Reads and parses a file (a path relative to the working directory or an
		/// absolute one). Returns an invalid document when the file is missing or
		/// is not valid JSON; the native reader reports why in the engine log.
		/// </summary>
		public static JsonDocument Load(string filePath)
		{
			if (string.IsNullOrEmpty(filePath))
			{
				Debug.LogError("JsonDocument: no file path was given.");
				return new JsonDocument(0);
			}

			return new JsonDocument(JsonApi.VspJson_ParseFile(filePath));
		}

		/// <summary>Number of documents the process currently holds open.</summary>
		public static int LiveDocumentCount => JsonApi.VspJson_GetLiveDocumentCount();

		public void Dispose()
		{
			if (documentHandle != 0)
			{
				JsonApi.VspJson_ReleaseDocument(documentHandle);
			}
		}

		/// <summary>Reads a UTF-8 string out of a native node.</summary>
		internal static string ReadString(int nodeHandle)
		{
			byte[] textBuffer = new byte[TextBufferByteCount];
			int byteCount = JsonApi.VspJson_GetStringUtf8(nodeHandle, textBuffer, textBuffer.Length);
			return byteCount > 0 ? Encoding.UTF8.GetString(textBuffer, 0, byteCount) : string.Empty;
		}

		/// <summary>Reads a member's name out of a native object node.</summary>
		internal static string ReadMemberName(int nodeHandle, int memberIndex)
		{
			byte[] textBuffer = new byte[TextBufferByteCount];
			int byteCount = JsonApi.VspJson_GetMemberNameUtf8(nodeHandle, memberIndex, textBuffer, textBuffer.Length);
			return byteCount > 0 ? Encoding.UTF8.GetString(textBuffer, 0, byteCount) : string.Empty;
		}

		internal static JsonValueType ReadType(int nodeHandle) =>
			(JsonValueType)JsonApi.VspJson_GetNodeType(nodeHandle);
	}

	/// <summary>
	/// One value inside a <see cref="JsonDocument"/>: an object, an array or a
	/// scalar. Every accessor takes the fallback it should return when the value
	/// is missing or is of another type, so reading a document never fails and
	/// never throws - a layout with a missing member simply keeps its default.
	/// </summary>
	public readonly struct JsonNode
	{
		private readonly int nodeHandle;

		internal JsonNode(int handle)
		{
			nodeHandle = handle;
		}

		/// <summary>Native node handle (0 = invalid).</summary>
		public int NativeHandle => nodeHandle;

		/// <summary>True when this node addresses a parsed value.</summary>
		public bool IsValid => nodeHandle != 0;

		/// <summary>What the node holds.</summary>
		public JsonValueType Type => IsValid ? JsonDocument.ReadType(nodeHandle) : JsonValueType.Invalid;

		public bool IsObject => Type == JsonValueType.Object;

		public bool IsArray => Type == JsonValueType.Array;

		/// <summary>Number of members of an object (0 for anything else).</summary>
		public int MemberCount => IsValid ? JsonApi.VspJson_GetMemberCount(nodeHandle) : 0;

		/// <summary>Number of elements of an array (0 for anything else).</summary>
		public int ElementCount => IsValid ? JsonApi.VspJson_GetElementCount(nodeHandle) : 0;

		/// <summary>The member with the given name, or an invalid node.</summary>
		public JsonNode GetMember(string memberName)
		{
			if (!IsValid || string.IsNullOrEmpty(memberName))
			{
				return default;
			}
			return new JsonNode(JsonApi.VspJson_GetMemberNodeByName(nodeHandle, memberName));
		}

		/// <summary>The member at the given position of an object, or an invalid node.</summary>
		public JsonNode GetMember(int memberIndex)
		{
			if (!IsValid || memberIndex < 0)
			{
				return default;
			}
			return new JsonNode(JsonApi.VspJson_GetMemberNode(nodeHandle, memberIndex));
		}

		/// <summary>Name of the member at the given position.</summary>
		public string GetMemberName(int memberIndex) =>
			IsValid && memberIndex >= 0 ? JsonDocument.ReadMemberName(nodeHandle, memberIndex) : string.Empty;

		/// <summary>The element at the given position of an array, or an invalid node.</summary>
		public JsonNode GetElement(int elementIndex)
		{
			if (!IsValid || elementIndex < 0)
			{
				return default;
			}
			return new JsonNode(JsonApi.VspJson_GetElementNode(nodeHandle, elementIndex));
		}

		/// <summary>The node's text, or the fallback when it is not a string.</summary>
		public string GetString(string fallback = "")
		{
			if (Type != JsonValueType.String)
			{
				return fallback;
			}
			return JsonDocument.ReadString(nodeHandle);
		}

		// -------- Typed member access with a fallback --------

		public string GetMemberString(string memberName, string fallback = "") =>
			GetMember(memberName).GetString(fallback);

		public float GetMemberFloat(string memberName, float fallback = 0.0f)
		{
			JsonNode member = GetMember(memberName);
			return member.Type == JsonValueType.Number ? (float)JsonApi.VspJson_GetNumber(member.nodeHandle) : fallback;
		}

		public int GetMemberInt(string memberName, int fallback = 0)
		{
			JsonNode member = GetMember(memberName);
			return member.Type == JsonValueType.Number ? (int)JsonApi.VspJson_GetNumber(member.nodeHandle) : fallback;
		}

		public bool GetMemberBool(string memberName, bool fallback = false)
		{
			JsonNode member = GetMember(memberName);
			return member.Type == JsonValueType.Boolean
				? JsonApi.VspJson_GetBoolean(member.nodeHandle) != 0
				: fallback;
		}

		/// <summary>
		/// A member that must be an array of numbers, read into
		/// <paramref name="outValues"/>; missing elements keep the fallback.
		/// </summary>
		public bool TryGetMemberNumberArray(string memberName, float[] outValues, float fallback = 0.0f)
		{
			if (outValues == null)
			{
				return false;
			}

			for (int valueIndex = 0; valueIndex < outValues.Length; ++valueIndex)
			{
				outValues[valueIndex] = fallback;
			}

			JsonNode member = GetMember(memberName);
			if (!member.IsArray)
			{
				return false;
			}

			int valueCount = Math.Min(member.ElementCount, outValues.Length);
			for (int valueIndex = 0; valueIndex < valueCount; ++valueIndex)
			{
				JsonNode element = member.GetElement(valueIndex);
				if (element.Type == JsonValueType.Number)
				{
					outValues[valueIndex] = (float)JsonApi.VspJson_GetNumber(element.nodeHandle);
				}
			}
			return valueCount > 0;
		}
	}
}
