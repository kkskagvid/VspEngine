#include "DxcCompilerApi.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#include "FilePathUtility.h"
#include "ShaderSourceInjector.h"

#if defined(_WIN32)
	#define WIN32_LEAN_AND_MEAN
	#include <Windows.h>
	#include <unknwn.h>

	#include <dxcapi.h>
#endif

namespace Hlslcc
{
	namespace
	{
#if defined(_WIN32)
		// Converts UTF-16 text into UTF-8.
		std::string NarrowText(const std::wstring& sWideText)
		{
			if (sWideText.empty())
			{
				return std::string();
			}

			const int nNarrowLength = ::WideCharToMultiByte(
				CP_UTF8, 0, sWideText.c_str(), static_cast<int>(sWideText.size()), nullptr, 0, nullptr, nullptr);
			if (nNarrowLength <= 0)
			{
				return std::string();
			}

			std::string sNarrowText(static_cast<size_t>(nNarrowLength), '\0');
			::WideCharToMultiByte(
				CP_UTF8, 0, sWideText.c_str(), static_cast<int>(sWideText.size()), sNarrowText.data(), nNarrowLength, nullptr, nullptr);
			return sNarrowText;
		}

		// Converts UTF-8 text into the UTF-16 form the DXC API expects.
		bool WidenText(const std::string& sText, std::wstring& outWideText)
		{
			if (sText.empty())
			{
				outWideText.clear();
				return true;
			}

			const int nWideLength = ::MultiByteToWideChar(
				CP_UTF8, 0, sText.c_str(), static_cast<int>(sText.size()), nullptr, 0);
			if (nWideLength <= 0)
			{
				return false;
			}

			outWideText.resize(static_cast<size_t>(nWideLength));
			::MultiByteToWideChar(
				CP_UTF8, 0, sText.c_str(), static_cast<int>(sText.size()), outWideText.data(), nWideLength);
			return true;
		}

		// ---------------------------------------------------------------------
		// FileIncludeHandler
		// ---------------------------------------------------------------------
		// Resolves the #includes of a shader against the directory the shader
		// lives in and the include directories the caller passed, in that order.
		// The compiler is handed this handler explicitly: without one it has no
		// way to reach the Vulkan HLSL namespace the injector adds to the source.
		// ---------------------------------------------------------------------
		class FileIncludeHandler : public IDxcIncludeHandler
		{
		public:
			FileIncludeHandler(
				IDxcUtils* pUtils,
				const std::string& sSourceDirectory,
				const std::vector<std::string>& sIncludeDirectories)
				: m_pUtils(pUtils)
			{
				if (!sSourceDirectory.empty())
				{
					m_sSearchDirectories.push_back(sSourceDirectory);
				}
				for (const std::string& sIncludeDirectory : sIncludeDirectories)
				{
					if (!sIncludeDirectory.empty())
					{
						m_sSearchDirectories.push_back(sIncludeDirectory);
					}
				}
				if (m_pUtils != nullptr)
				{
					m_pUtils->AddRef();
				}
			}

			// -------- IUnknown --------
			HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppOutObject) override
			{
				if (ppOutObject == nullptr)
				{
					return E_POINTER;
				}
				if (riid == __uuidof(IDxcIncludeHandler) || riid == __uuidof(IUnknown))
				{
					*ppOutObject = static_cast<IDxcIncludeHandler*>(this);
					AddRef();
					return S_OK;
				}
				*ppOutObject = nullptr;
				return E_NOINTERFACE;
			}

			ULONG STDMETHODCALLTYPE AddRef() override
			{
				return ++m_nReferenceCount;
			}

			ULONG STDMETHODCALLTYPE Release() override
			{
				const ULONG nReferenceCount = --m_nReferenceCount;
				if (nReferenceCount == 0)
				{
					delete this;
				}
				return nReferenceCount;
			}

			// -------- IDxcIncludeHandler --------
			HRESULT STDMETHODCALLTYPE LoadSource(LPCWSTR pFileName, IDxcBlob** ppOutIncludeSource) override
			{
				if (ppOutIncludeSource == nullptr || pFileName == nullptr || m_pUtils == nullptr)
				{
					return E_POINTER;
				}
				*ppOutIncludeSource = nullptr;

				// The compiler hands the include path with forward slashes; the
				// engine's files are addressed with backslashes on Windows.
				std::wstring sRequestedName(pFileName);
				for (wchar_t& cCharacter : sRequestedName)
				{
					if (cCharacter == L'/')
					{
						cCharacter = L'\\';
					}
				}

				// The compiler already prefixes a quoted include with the
				// directory of the file that contains it, so the name can arrive
				// as an absolute path, as a path relative to the current
				// directory, or as a bare file name. All three are tried, and the
				// include directories are searched with the full name first and
				// with the bare file name after it.
				const std::string sRequestedNameUtf8 = NarrowText(sRequestedName);

				std::vector<std::string> sCandidatePaths;
				sCandidatePaths.push_back(sRequestedNameUtf8);

				const std::string sBareFileName = FilePathUtility::GetFileName(sRequestedNameUtf8);
				for (const std::string& sSearchDirectory : m_sSearchDirectories)
				{
					sCandidatePaths.push_back(FilePathUtility::Combine(sSearchDirectory, sRequestedNameUtf8));
					if (sBareFileName != sRequestedNameUtf8)
					{
						sCandidatePaths.push_back(FilePathUtility::Combine(sSearchDirectory, sBareFileName));
					}
				}

				for (const std::string& sCandidatePath : sCandidatePaths)
				{
					if (!FilePathUtility::DoesFileExist(sCandidatePath))
					{
						continue;
					}

					// An included file decides, just like the shader itself, whether
					// the pass manages its own bindings. Reading the text here is
					// what makes a binding written inside an include visible to the
					// compiler, which otherwise only sees the .vsf text.
					NoteIncludedSource(sCandidatePath);

					std::wstring sWideCandidatePath;
					if (WidenText(FilePathUtility::GetAbsolutePath(sCandidatePath), sWideCandidatePath))
					{
						return LoadBlobFromWidePath(sWideCandidatePath, ppOutIncludeSource);
					}
				}

				// Not found: the compiler turns this into "file not found".
				return E_FAIL;
			}

			// True when any file resolved through this handler named a descriptor
			// binding of its own.
			bool DidAnyIncludeSpecifyBindings() const { return m_bIncludedSourceSpecifiesBindings; }

		private:
			// Reads an included file and remembers whether it names its own
			// bindings. A file that cannot be read is not reported here: the
			// compiler reports it through its own diagnostics.
			void NoteIncludedSource(const std::string& sFilePath)
			{
				if (m_bIncludedSourceSpecifiesBindings)
				{
					return;
				}

				std::ifstream sourceStream(sFilePath, std::ios::binary);
				if (!sourceStream.is_open())
				{
					return;
				}

				std::ostringstream sourceText;
				sourceText << sourceStream.rdbuf();
				m_bIncludedSourceSpecifiesBindings =
					ShaderSourceInjector::SourceTextSpecifiesBindings(sourceText.str());
			}

			~FileIncludeHandler()
			{
				if (m_pUtils != nullptr)
				{
					m_pUtils->Release();
				}
			}

			HRESULT LoadBlobFromWidePath(const std::wstring& sWidePath, IDxcBlob** ppOutIncludeSource)
			{
				IDxcBlobEncoding* pBlobEncoding = nullptr;
				const HRESULT hLoadResult = m_pUtils->LoadFile(sWidePath.c_str(), nullptr, &pBlobEncoding);
				if (FAILED(hLoadResult) || pBlobEncoding == nullptr)
				{
					return E_FAIL;
				}

				*ppOutIncludeSource = pBlobEncoding;
				return S_OK;
			}

			std::atomic<ULONG> m_nReferenceCount{ 1 };
			IDxcUtils* m_pUtils = nullptr;
			std::vector<std::string> m_sSearchDirectories;
			bool m_bIncludedSourceSpecifiesBindings = false;
		};
#endif
	}

	DxcCompilerApi::~DxcCompilerApi()
	{
		Shutdown();
	}

	DxcCompilerApi& DxcCompilerApi::Get()
	{
		// Deliberately never destroyed: the compiler objects live inside
		// dxcompiler.dll, and the order in which that library and this module are
		// torn down at process exit is not defined. Releasing them from a static
		// destructor could call into an already unloaded module, so the singleton
		// lives exactly as long as the process does.
		static DxcCompilerApi* s_pInstance = new DxcCompilerApi();
		return *s_pInstance;
	}

	void DxcCompilerApi::CollectLibraryCandidates(std::vector<std::string>& outCandidatePaths)
	{
		outCandidatePaths.clear();

		// 1. Next to the executable: a staged run directory carries the compiler.
		const std::string sExecutableDirectory = FilePathUtility::GetExecutableDirectory();
		if (!sExecutableDirectory.empty())
		{
			outCandidatePaths.push_back(FilePathUtility::Combine(sExecutableDirectory, "dxcompiler.dll"));
		}

		// 2. Environment overrides.
		const std::string sDxcRoot = FilePathUtility::GetEnvironmentValue("VSP_DXC_ROOT");
		if (!sDxcRoot.empty())
		{
			outCandidatePaths.push_back(FilePathUtility::Combine(sDxcRoot, "bin\\dxcompiler.dll"));
			outCandidatePaths.push_back(FilePathUtility::Combine(sDxcRoot, "dxcompiler.dll"));
		}
		const std::string sDxcRootAlternative = FilePathUtility::GetEnvironmentValue("DXC_ROOT");
		if (!sDxcRootAlternative.empty())
		{
			outCandidatePaths.push_back(FilePathUtility::Combine(sDxcRootAlternative, "bin\\dxcompiler.dll"));
			outCandidatePaths.push_back(FilePathUtility::Combine(sDxcRootAlternative, "dxcompiler.dll"));
		}

		// 3. The Vulkan SDK ships DXC in its Bin directory.
		const std::string sVulkanSdkRoot = FilePathUtility::GetEnvironmentValue("VULKAN_SDK");
		if (!sVulkanSdkRoot.empty())
		{
			outCandidatePaths.push_back(FilePathUtility::Combine(sVulkanSdkRoot, "Bin\\dxcompiler.dll"));
		}

		// 4. The copy vendored in the repository, reached from the executable of
		//    whichever module is asking (HLSLCC.exe or the engine).
		if (!sExecutableDirectory.empty())
		{
			outCandidatePaths.push_back(FilePathUtility::Combine(
				sExecutableDirectory, "..\\..\\..\\Source\\Thirdparty\\dxc\\bin\\dxcompiler.dll"));
			outCandidatePaths.push_back(FilePathUtility::Combine(
				sExecutableDirectory, "..\\Thirdparty\\dxc\\bin\\dxcompiler.dll"));
		}
	}

	HlslccResult DxcCompilerApi::Initialize(std::string& outErrorText)
	{
		outErrorText.clear();

		if (m_pCompiler != nullptr)
		{
			return HlslccResult::Success;
		}

#if !defined(_WIN32)
		outErrorText = "the DirectX Shader Compiler is only available on Windows";
		return HlslccResult::FailCannotLoadCompiler;
#else
		std::vector<std::string> sLibraryCandidates;
		CollectLibraryCandidates(sLibraryCandidates);

		HMODULE hCompilerLibrary = nullptr;
		std::string sLoadedPath;
		for (const std::string& sCandidatePath : sLibraryCandidates)
		{
			if (!FilePathUtility::DoesFileExist(sCandidatePath))
			{
				continue;
			}

			hCompilerLibrary = ::LoadLibraryA(sCandidatePath.c_str());
			if (hCompilerLibrary != nullptr)
			{
				sLoadedPath = sCandidatePath;
				break;
			}
		}

		if (hCompilerLibrary == nullptr)
		{
			outErrorText = "dxcompiler.dll was not found. Install the Vulkan SDK (it ships DXC), set "
				"VSP_DXC_ROOT, or place dxcompiler.dll next to the executable. Searched:";
			for (const std::string& sCandidatePath : sLibraryCandidates)
			{
				outErrorText += "\n  " + sCandidatePath;
			}
			return HlslccResult::FailCannotLoadCompiler;
		}

		m_sLoadedLibraryDirectory = FilePathUtility::GetDirectory(sLoadedPath);

		auto pCreateInstance = reinterpret_cast<DxcCreateInstanceProc>(
			::GetProcAddress(hCompilerLibrary, "DxcCreateInstance"));
		if (pCreateInstance == nullptr)
		{
			::FreeLibrary(hCompilerLibrary);
			m_sLoadedLibraryDirectory.clear();
			outErrorText = "dxcompiler.dll does not export DxcCreateInstance: " + sLoadedPath;
			return HlslccResult::FailCannotLoadCompiler;
		}

		IDxcUtils* pUtils = nullptr;
		if (FAILED(pCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&pUtils))) || pUtils == nullptr)
		{
			::FreeLibrary(hCompilerLibrary);
			m_sLoadedLibraryDirectory.clear();
			outErrorText = "DxcCreateInstance(CLSID_DxcUtils) failed.";
			return HlslccResult::FailCannotLoadCompiler;
		}

		IDxcCompiler3* pCompiler = nullptr;
		if (FAILED(pCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&pCompiler))) || pCompiler == nullptr)
		{
			pUtils->Release();
			::FreeLibrary(hCompilerLibrary);
			m_sLoadedLibraryDirectory.clear();
			outErrorText = "DxcCreateInstance(CLSID_DxcCompiler) failed.";
			return HlslccResult::FailCannotLoadCompiler;
		}

		m_pCompilerLibrary = hCompilerLibrary;
		m_pUtils = pUtils;
		m_pCompiler = pCompiler;
		return HlslccResult::Success;
#endif
	}

	void DxcCompilerApi::Shutdown()
	{
		if (m_pCompiler != nullptr)
		{
			static_cast<IDxcCompiler3*>(m_pCompiler)->Release();
			m_pCompiler = nullptr;
		}
		if (m_pUtils != nullptr)
		{
			static_cast<IDxcUtils*>(m_pUtils)->Release();
			m_pUtils = nullptr;
		}
		if (m_pCompilerLibrary != nullptr)
		{
#if defined(_WIN32)
			::FreeLibrary(static_cast<HMODULE>(m_pCompilerLibrary));
#endif
			m_pCompilerLibrary = nullptr;
		}
		m_sLoadedLibraryDirectory.clear();
	}

	bool DxcCompilerApi::IsAvailable()
	{
		std::string sErrorText;
		return Initialize(sErrorText) == HlslccResult::Success;
	}

	bool DxcCompilerApi::DescribeSpirvModule(const std::vector<uint32_t>& spirvWords, std::string& outDescription)
	{
		outDescription.clear();
		if (spirvWords.size() < 5 || spirvWords[0] != k_uSpirvMagicNumber)
		{
			return false;
		}

		char sBuffer[160] = {};
		std::snprintf(
			sBuffer, sizeof(sBuffer),
			"magic=0x%08X version=0x%08X generator=0x%08X bound=%u words=%u",
			spirvWords[0], spirvWords[1], spirvWords[2], spirvWords[3], static_cast<uint32_t>(spirvWords.size()));
		outDescription = sBuffer;
		return true;
	}

	HlslccResult DxcCompilerApi::CompileToSpirv(
		const CompileRequest& request,
		CompileOutput& outOutput,
		std::string& outErrorText)
	{
		outErrorText.clear();
		outOutput = CompileOutput();

		const HlslccResult eInitializeResult = Initialize(outErrorText);
		if (eInitializeResult != HlslccResult::Success)
		{
			return eInitializeResult;
		}

		if (request.SourceText.empty() || request.EntryPoint.empty())
		{
			outErrorText = "the compile request needs source text and an entry point";
			return HlslccResult::FailInvalidArgument;
		}

#if !defined(_WIN32)
		outErrorText = "the DirectX Shader Compiler is only available on Windows";
		return HlslccResult::FailCannotLoadCompiler;
#else
		IDxcCompiler3* pCompiler = static_cast<IDxcCompiler3*>(m_pCompiler);

		// ---- Source buffer ----
		DxcBuffer sourceBuffer = {};
		sourceBuffer.Ptr = request.SourceText.data();
		sourceBuffer.Size = request.SourceText.size();
		sourceBuffer.Encoding = DXC_CP_UTF8;

		// ---- Arguments ----
		std::vector<std::wstring> wideArguments;
		auto AddArgument = [&wideArguments](const std::string& sArgument) -> void
		{
			std::wstring sWideArgument;
			if (WidenText(sArgument, sWideArgument))
			{
				wideArguments.push_back(std::move(sWideArgument));
			}
		};

		AddArgument(request.SourceName);                                    // Used in diagnostics.
		AddArgument("-T");
		AddArgument(ToProfileName(request.eStage));
		AddArgument("-E");
		AddArgument(request.EntryPoint);
		AddArgument("-spirv");
		AddArgument("-fspv-target-env=" + request.TargetEnvironment);
		AddArgument(request.bDebugInfo ? "-Od" : "-O3");

		for (const std::string& sIncludeDirectory : request.IncludeDirectories)
		{
			AddArgument("-I");
			AddArgument(sIncludeDirectory);
		}
		for (const std::string& sDefine : request.Defines)
		{
			AddArgument("-D" + sDefine);
		}

		std::vector<LPCWSTR> pArgumentPointers;
		pArgumentPointers.reserve(wideArguments.size());
		for (const std::wstring& sWideArgument : wideArguments)
		{
			pArgumentPointers.push_back(sWideArgument.c_str());
		}

		// ---- Include resolution ----
		// The handler searches the shader's own directory first and the caller's
		// include directories after it; it is handed to the compiler explicitly
		// so the injected Vulkan namespace #include always resolves.
		FileIncludeHandler* pIncludeHandler = new FileIncludeHandler(
			static_cast<IDxcUtils*>(m_pUtils),
			FilePathUtility::GetDirectory(request.SourceName),
			request.IncludeDirectories);

		// ---- Compile ----
		IDxcResult* pResult = nullptr;
		const HRESULT hCompileResult = pCompiler->Compile(
			&sourceBuffer,
			pArgumentPointers.data(),
			static_cast<UINT32>(pArgumentPointers.size()),
			pIncludeHandler,
			IID_PPV_ARGS(&pResult));

		outOutput.bSourceSpecifiesBindings = pIncludeHandler->DidAnyIncludeSpecifyBindings();
		pIncludeHandler->Release();

		if (FAILED(hCompileResult) || pResult == nullptr)
		{
			char sBuffer[64] = {};
			std::snprintf(sBuffer, sizeof(sBuffer), "0x%08X", static_cast<unsigned>(hCompileResult));
			outErrorText = std::string("IDxcCompiler3::Compile failed with HRESULT ") + sBuffer;
			return HlslccResult::FailCompilation;
		}

		// ---- Diagnostics ----
		IDxcBlobUtf8* pErrors = nullptr;
		if (SUCCEEDED(pResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&pErrors), nullptr)) && pErrors != nullptr)
		{
			if (pErrors->GetStringLength() > 0)
			{
				outOutput.Diagnostics.assign(pErrors->GetStringPointer(), pErrors->GetStringLength());
			}
			pErrors->Release();
		}

		HRESULT hStatus = S_OK;
		pResult->GetStatus(&hStatus);
		if (FAILED(hStatus))
		{
			outErrorText = outOutput.Diagnostics.empty()
				? std::string("the HLSL compiler rejected the shader (no diagnostics were produced)")
				: outOutput.Diagnostics;
			pResult->Release();
			return HlslccResult::FailCompilation;
		}

		// ---- SPIR-V object ----
		IDxcBlob* pObject = nullptr;
		if (FAILED(pResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&pObject), nullptr)) || pObject == nullptr)
		{
			outErrorText = "the HLSL compiler produced no output object";
			pResult->Release();
			return HlslccResult::FailCompilation;
		}

		const size_t nByteCount = pObject->GetBufferSize();
		if (nByteCount < sizeof(uint32_t) * 5 || (nByteCount % sizeof(uint32_t)) != 0)
		{
			outErrorText = "the HLSL compiler produced a truncated SPIR-V module";
			pObject->Release();
			pResult->Release();
			return HlslccResult::FailCompilation;
		}

		outOutput.SpirvWords.resize(nByteCount / sizeof(uint32_t));
		std::memcpy(outOutput.SpirvWords.data(), pObject->GetBufferPointer(), nByteCount);

		pObject->Release();
		pResult->Release();

		if (outOutput.SpirvWords[0] != k_uSpirvMagicNumber)
		{
			outErrorText = "the produced module does not start with the SPIR-V magic number";
			return HlslccResult::FailCompilation;
		}
		return HlslccResult::Success;
#endif
	}
}