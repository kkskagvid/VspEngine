#include "RuntimePCH.h"

#include <cstring>

#include "Core/Diagnostics/ErrorHandling.h"
#include "Core/Json/JsonReader.h"
#include "Core/Logging/Log.h"
#include "Core/Templates/ArrayList.h"
#include "Scripting/ScriptExport.h"

// -------------------------------------------------------------------------
// JSON exports consumed by managed code (C# -> C++ direction).
// VspEngine's JsonApi.cs P/Invokes these exact names from VspCore.dll.
//
// JSON is read by the ENGINE (Core/Json/JsonReader), and these exports are how a
// managed caller reads a document through it: one parser, one set of rules, one
// place where a malformed document is reported. Managed code therefore never
// needs a JSON implementation of its own - a shader manifest, a localisation
// catalog, a scene file and a UI layout all go through the same reader.
//
// Handles:
//   * a DOCUMENT handle (>= 1) addresses one parsed file; 0 is invalid;
//   * a NODE handle (>= 1) addresses one value inside it; 0 is invalid.
// A node handle stays valid until its document is released, and an accessor
// takes a node handle rather than a path, so a caller walks the tree the way
// JSON is shaped: ask for the member, then for the member's member.
//
// Every accessor validates its handle, logs what is wrong and returns the empty
// result (0, 0.0, "" or an invalid handle) instead of reading anything it should
// not. Nothing throws; the text of a string comes back as UTF-8, the engine's
// file and ABI encoding.
// -------------------------------------------------------------------------

static constexpr const char* kLogTag = "JsonExports";

namespace
{
	// One node that was handed to managed code: a pointer INTO the tree plus the
	// document it belongs to, so a handle used after its document was released is
	// refused instead of dereferencing freed memory.
	struct JsonNodeRecord
	{
		const Vsp::JsonValue* pValue = nullptr;
		int32 nDocumentHandle = 0;
	};

	// One parsed document. It owns the tree; the tree is never moved, which is
	// what keeps the node pointers valid while the document is alive.
	struct JsonDocumentRecord
	{
		Vsp::JsonValue* pRoot = nullptr;
		Vsp::VspString sFilePath;
		bool bIsActive = false;
	};

	// The store is process-wide because the handles are. It is touched from the
	// thread that parses a document and reads it back - the engine's main thread,
	// which owns the frame (Core/EngineServices.h) - and it holds no service of
	// its own beyond the parsed trees.
	struct JsonStore
	{
		Vsp::ArrayList<JsonDocumentRecord> Documents;
		Vsp::ArrayList<JsonNodeRecord> Nodes;
	};

	// A walk that registers more nodes than this is refused rather than grown
	// without bound: a document is walked by a caller, and a caller that walks one
	// without end has a bug rather than a big file.
	constexpr int32 k_nMaxNodeCount = 65536;

	JsonStore& GetJsonStore()
	{
		static JsonStore s_Store;
		return s_Store;
	}

	// The document behind a handle, or nullptr (with a log line) when the handle
	// was never valid or its document has been released.
	JsonDocumentRecord* ResolveDocument(int32 nDocumentHandle, const char* pCallerName)
	{
		JsonStore& store = GetJsonStore();
		if (nDocumentHandle <= 0 || static_cast<size_t>(nDocumentHandle) > store.Documents.GetSize())
		{
			VSP_LOG_ERROR(kLogTag, "{}: the JSON document handle {} is not live.", pCallerName, nDocumentHandle);
			return nullptr;
		}

		JsonDocumentRecord& record = store.Documents[static_cast<size_t>(nDocumentHandle) - 1u];
		if (!record.bIsActive || record.pRoot == nullptr)
		{
			VSP_LOG_ERROR(kLogTag, "{}: the JSON document behind handle {} was released.", pCallerName, nDocumentHandle);
			return nullptr;
		}
		return &record;
	}

	// The value behind a node handle, or nullptr (with a log line). The document
	// handle the node belongs to comes back through outDocumentHandle.
	const Vsp::JsonValue* ResolveNode(int32 nNodeHandle, const char* pCallerName, int32& outDocumentHandle)
	{
		outDocumentHandle = 0;

		JsonStore& store = GetJsonStore();
		if (nNodeHandle <= 0 || static_cast<size_t>(nNodeHandle) > store.Nodes.GetSize())
		{
			VSP_LOG_ERROR(kLogTag, "{}: the JSON node handle {} is not live.", pCallerName, nNodeHandle);
			return nullptr;
		}

		const JsonNodeRecord& record = store.Nodes[static_cast<size_t>(nNodeHandle) - 1u];
		if (record.pValue == nullptr)
		{
			VSP_LOG_ERROR(kLogTag, "{}: the JSON node behind handle {} has no value.", pCallerName, nNodeHandle);
			return nullptr;
		}

		if (ResolveDocument(record.nDocumentHandle, pCallerName) == nullptr)
		{
			return nullptr;
		}

		outDocumentHandle = record.nDocumentHandle;
		return record.pValue;
	}

	// Registers a value that sits inside a live document and hands back its
	// handle. Called for every step of a walk.
	int32 RegisterNode(const Vsp::JsonValue& Value, int32 nDocumentHandle, const char* pCallerName)
	{
		JsonStore& store = GetJsonStore();
		if (store.Nodes.GetSize() >= static_cast<size_t>(k_nMaxNodeCount))
		{
			VSP_LOG_ERROR(kLogTag,
				"{}: the JSON node table is full ({} node(s)); the lookup is refused. Release the documents that are "
				"no longer used, or read a document once instead of every frame.",
				pCallerName, k_nMaxNodeCount);
			return 0;
		}

		JsonNodeRecord record;
		record.pValue = &Value;
		record.nDocumentHandle = nDocumentHandle;
		store.Nodes.Add(record);
		return static_cast<int32>(store.Nodes.GetSize());
	}

	// Copies text into a caller-owned UTF-8 buffer, always terminating it, and
	// returns the byte count without the terminator. Both a node's text and a
	// member's NAME are VspStrings, so both go through this one copier.
	int32 CopyTextToUtf8Buffer(const Vsp::VspString& sText, char* pBufferUtf8, int32 nBufferCapacityBytes)
	{
		if (pBufferUtf8 == nullptr || nBufferCapacityBytes <= 0)
		{
			VSP_LOG_ERROR(kLogTag, "A UTF-8 buffer of {} byte(s) cannot receive the value.", nBufferCapacityBytes);
			return 0;
		}

		const size_t nTextByteCount = sText.GetByteLength();
		const size_t nMaxCopyByteCount = static_cast<size_t>(nBufferCapacityBytes) - 1u;
		const size_t nCopyByteCount = (nTextByteCount < nMaxCopyByteCount) ? nTextByteCount : nMaxCopyByteCount;

		if (nCopyByteCount > 0)
		{
			memcpy(pBufferUtf8, sText.GetData(), nCopyByteCount);
		}
		pBufferUtf8[nCopyByteCount] = '\0';
		return static_cast<int32>(nCopyByteCount);
	}
}

// -------- Documents --------

CSHARP_EXPORT int32 VspJson_ParseFile(const char* pFilePathUtf8)
{
	if (pFilePathUtf8 == nullptr || *pFilePathUtf8 == '\0')
	{
		VSP_LOG_ERROR(kLogTag, "VspJson_ParseFile was given an empty path.");
		return 0;
	}

	const Vsp::VspString sFilePath(pFilePathUtf8);

	// The tree lives on the heap and is never moved, because the node handles
	// point into it for as long as the document is alive.
	Vsp::JsonValue* pRoot = new Vsp::JsonValue();
	Vsp::VspString sErrorText;
	if (!Vsp::JsonReader::ParseFile(sFilePath, *pRoot, sErrorText))
	{
		VSP_LOG_ERROR(kLogTag, "The JSON document '{}' could not be read: {}",
			sFilePath.GetData(), sErrorText.GetData());
		delete pRoot;
		return 0;
	}

	JsonStore& store = GetJsonStore();
	JsonDocumentRecord documentRecord;
	documentRecord.pRoot = pRoot;
	documentRecord.sFilePath = sFilePath;
	documentRecord.bIsActive = true;
	store.Documents.Add(documentRecord);

	const int32 nDocumentHandle = static_cast<int32>(store.Documents.GetSize());
	LOG_INFO(kLogTag, "JSON document '{}' parsed ({} member(s)).", sFilePath.GetData(), pRoot->GetMemberCount());
	return nDocumentHandle;
}

CSHARP_EXPORT void VspJson_ReleaseDocument(int32 nDocumentHandle)
{
	JsonDocumentRecord* pDocument = ResolveDocument(nDocumentHandle, "VspJson_ReleaseDocument");
	if (pDocument == nullptr)
	{
		return;
	}

	delete pDocument->pRoot;
	pDocument->pRoot = nullptr;
	pDocument->bIsActive = false;

	// Every node handle of that document is refused from here on: a node carries
	// the document it belongs to, and the document is no longer active.
	LOG_INFO(kLogTag, "JSON document '{}' released.", pDocument->sFilePath.GetData());
}

CSHARP_EXPORT int32 VspJson_GetLiveDocumentCount()
{
	JsonStore& store = GetJsonStore();

	int32 nLiveDocumentCount = 0;
	for (size_t nDocumentIndex = 0; nDocumentIndex < store.Documents.GetSize(); ++nDocumentIndex)
	{
		if (store.Documents[nDocumentIndex].bIsActive)
		{
			++nLiveDocumentCount;
		}
	}
	return nLiveDocumentCount;
}

// -------- Nodes --------

CSHARP_EXPORT int32 VspJson_GetRootNode(int32 nDocumentHandle)
{
	JsonDocumentRecord* pDocument = ResolveDocument(nDocumentHandle, "VspJson_GetRootNode");
	if (pDocument == nullptr)
	{
		return 0;
	}
	return RegisterNode(*pDocument->pRoot, nDocumentHandle, "VspJson_GetRootNode");
}

CSHARP_EXPORT int32 VspJson_GetNodeType(int32 nNodeHandle)
{
	int32 nDocumentHandle = 0;
	const Vsp::JsonValue* pValue = ResolveNode(nNodeHandle, "VspJson_GetNodeType", nDocumentHandle);
	if (pValue == nullptr)
	{
		return -1;
	}
	return static_cast<int32>(pValue->GetType());
}

// -------- Object members --------

CSHARP_EXPORT int32 VspJson_GetMemberCount(int32 nNodeHandle)
{
	int32 nDocumentHandle = 0;
	const Vsp::JsonValue* pValue = ResolveNode(nNodeHandle, "VspJson_GetMemberCount", nDocumentHandle);
	if (pValue == nullptr)
	{
		return 0;
	}
	return static_cast<int32>(pValue->GetMemberCount());
}

CSHARP_EXPORT int32 VspJson_GetMemberNameUtf8(int32 nNodeHandle, int32 nMemberIndex, char* pBufferUtf8, int32 nBufferCapacityBytes)
{
	int32 nDocumentHandle = 0;
	const Vsp::JsonValue* pValue = ResolveNode(nNodeHandle, "VspJson_GetMemberNameUtf8", nDocumentHandle);
	if (pValue == nullptr)
	{
		return 0;
	}
	if (nMemberIndex < 0 || static_cast<uint32>(nMemberIndex) >= pValue->GetMemberCount())
	{
		VSP_LOG_ERROR(kLogTag, "VspJson_GetMemberNameUtf8: member {} is out of the {}-member object.",
			nMemberIndex, pValue->GetMemberCount());
		return 0;
	}

	return CopyTextToUtf8Buffer(pValue->GetMemberName(static_cast<uint32>(nMemberIndex)), pBufferUtf8, nBufferCapacityBytes);
}

CSHARP_EXPORT int32 VspJson_GetMemberNode(int32 nNodeHandle, int32 nMemberIndex)
{
	int32 nDocumentHandle = 0;
	const Vsp::JsonValue* pValue = ResolveNode(nNodeHandle, "VspJson_GetMemberNode", nDocumentHandle);
	if (pValue == nullptr)
	{
		return 0;
	}
	if (nMemberIndex < 0 || static_cast<uint32>(nMemberIndex) >= pValue->GetMemberCount())
	{
		VSP_LOG_ERROR(kLogTag, "VspJson_GetMemberNode: member {} is out of the {}-member object.",
			nMemberIndex, pValue->GetMemberCount());
		return 0;
	}

	return RegisterNode(pValue->GetMember(static_cast<uint32>(nMemberIndex)), nDocumentHandle, "VspJson_GetMemberNode");
}

CSHARP_EXPORT int32 VspJson_GetMemberNodeByName(int32 nNodeHandle, const char* pMemberNameUtf8)
{
	if (pMemberNameUtf8 == nullptr)
	{
		VSP_LOG_ERROR(kLogTag, "VspJson_GetMemberNodeByName was given a null member name.");
		return 0;
	}

	int32 nDocumentHandle = 0;
	const Vsp::JsonValue* pValue = ResolveNode(nNodeHandle, "VspJson_GetMemberNodeByName", nDocumentHandle);
	if (pValue == nullptr)
	{
		return 0;
	}

	// A member that is not there is not an error: the caller decides what a
	// missing value means, which is why this returns 0 without a log line.
	const Vsp::JsonValue& memberValue = (*pValue)[pMemberNameUtf8];
	if (memberValue.IsNull())
	{
		return 0;
	}
	return RegisterNode(memberValue, nDocumentHandle, "VspJson_GetMemberNodeByName");
}

// -------- Array elements --------

CSHARP_EXPORT int32 VspJson_GetElementCount(int32 nNodeHandle)
{
	int32 nDocumentHandle = 0;
	const Vsp::JsonValue* pValue = ResolveNode(nNodeHandle, "VspJson_GetElementCount", nDocumentHandle);
	if (pValue == nullptr)
	{
		return 0;
	}
	return static_cast<int32>(pValue->GetElementCount());
}

CSHARP_EXPORT int32 VspJson_GetElementNode(int32 nNodeHandle, int32 nElementIndex)
{
	int32 nDocumentHandle = 0;
	const Vsp::JsonValue* pValue = ResolveNode(nNodeHandle, "VspJson_GetElementNode", nDocumentHandle);
	if (pValue == nullptr)
	{
		return 0;
	}
	if (nElementIndex < 0 || static_cast<uint32>(nElementIndex) >= pValue->GetElementCount())
	{
		VSP_LOG_ERROR(kLogTag, "VspJson_GetElementNode: element {} is out of the {}-element array.",
			nElementIndex, pValue->GetElementCount());
		return 0;
	}

	return RegisterNode(pValue->GetElement(static_cast<uint32>(nElementIndex)), nDocumentHandle, "VspJson_GetElementNode");
}

// -------- Scalars --------

CSHARP_EXPORT int32 VspJson_GetStringUtf8(int32 nNodeHandle, char* pBufferUtf8, int32 nBufferCapacityBytes)
{
	int32 nDocumentHandle = 0;
	const Vsp::JsonValue* pValue = ResolveNode(nNodeHandle, "VspJson_GetStringUtf8", nDocumentHandle);
	if (pValue == nullptr)
	{
		return 0;
	}
	return CopyTextToUtf8Buffer(pValue->GetString(), pBufferUtf8, nBufferCapacityBytes);
}

CSHARP_EXPORT double VspJson_GetNumber(int32 nNodeHandle)
{
	int32 nDocumentHandle = 0;
	const Vsp::JsonValue* pValue = ResolveNode(nNodeHandle, "VspJson_GetNumber", nDocumentHandle);
	if (pValue == nullptr)
	{
		return 0.0;
	}
	return pValue->GetNumber();
}

CSHARP_EXPORT int32 VspJson_GetBoolean(int32 nNodeHandle)
{
	int32 nDocumentHandle = 0;
	const Vsp::JsonValue* pValue = ResolveNode(nNodeHandle, "VspJson_GetBoolean", nDocumentHandle);
	if (pValue == nullptr)
	{
		return 0;
	}
	return pValue->GetBoolean() ? 1 : 0;
}
