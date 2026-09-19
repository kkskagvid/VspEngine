#include "VspShaderFile.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "FilePathUtility.h"

namespace Hlslcc
{
	namespace
	{
		// Copies text into a fixed-size name buffer, always terminating it.
		void CopyName(const std::string& sText, char* pOutName, size_t nCapacity)
		{
			if (pOutName == nullptr || nCapacity == 0)
			{
				return;
			}

			const size_t nCopyByteCount = (sText.size() < nCapacity - 1u) ? sText.size() : (nCapacity - 1u);
			if (nCopyByteCount > 0)
			{
				std::memcpy(pOutName, sText.data(), nCopyByteCount);
			}
			pOutName[nCopyByteCount] = '\0';
		}

		bool IsWhitespace(char cCharacter)
		{
			return std::isspace(static_cast<unsigned char>(cCharacter)) != 0;
		}

		bool IsIdentifierCharacter(char cCharacter)
		{
			return std::isalnum(static_cast<unsigned char>(cCharacter)) != 0 || cCharacter == '_';
		}

		// Reads the whole file into a string. Returns false when it cannot be read.
		bool ReadWholeFile(const std::string& sFilePath, std::string& outText)
		{
			std::FILE* pFile = nullptr;
#if defined(_MSC_VER)
			if (fopen_s(&pFile, sFilePath.c_str(), "rb") != 0)
			{
				pFile = nullptr;
			}
#else
			pFile = std::fopen(sFilePath.c_str(), "rb");
#endif
			if (pFile == nullptr)
			{
				return false;
			}

			outText.clear();
			char sBuffer[8192];
			size_t nReadByteCount = 0;
			while ((nReadByteCount = std::fread(sBuffer, 1, sizeof(sBuffer), pFile)) > 0)
			{
				outText.append(sBuffer, nReadByteCount);
			}

			std::fclose(pFile);
			return true;
		}

		// Splits a line on whitespace and on commas.
		void SplitTokens(const std::string& sLine, std::vector<std::string>& outTokens)
		{
			outTokens.clear();
			size_t nIndex = 0;
			while (nIndex < sLine.size())
			{
				while (nIndex < sLine.size() && (IsWhitespace(sLine[nIndex]) || sLine[nIndex] == ','))
				{
					++nIndex;
				}
				if (nIndex >= sLine.size())
				{
					break;
				}

				const size_t nTokenStart = nIndex;
				while (nIndex < sLine.size() && !IsWhitespace(sLine[nIndex]) && sLine[nIndex] != ',')
				{
					++nIndex;
				}
				outTokens.push_back(sLine.substr(nTokenStart, nIndex - nTokenStart));
			}
		}

		// Removes a trailing '\r' and surrounding whitespace.
		std::string TrimLine(const std::string& sLine)
		{
			size_t nStartOffset = 0;
			size_t nEndOffset = sLine.size();
			while (nStartOffset < nEndOffset && IsWhitespace(sLine[nStartOffset]))
			{
				++nStartOffset;
			}
			while (nEndOffset > nStartOffset && IsWhitespace(sLine[nEndOffset - 1]))
			{
				--nEndOffset;
			}
			return sLine.substr(nStartOffset, nEndOffset - nStartOffset);
		}

		// Splits text into lines, keeping the line terminators out.
		void SplitLines(const std::string& sText, std::vector<std::string>& outLines)
		{
			outLines.clear();
			size_t nOffset = 0;
			while (nOffset <= sText.size())
			{
				const size_t nLineEndOffset = sText.find('\n', nOffset);
				if (nLineEndOffset == std::string::npos)
				{
					outLines.push_back(sText.substr(nOffset));
					break;
				}
				outLines.push_back(sText.substr(nOffset, nLineEndOffset - nOffset));
				nOffset = nLineEndOffset + 1;
			}
		}

		// True when the character can start a block keyword at this position.
		bool MatchesKeywordAt(const std::string& sText, size_t nOffset, const char* pKeyword)
		{
			const size_t nKeywordLength = std::strlen(pKeyword);
			if (nOffset + nKeywordLength > sText.size())
			{
				return false;
			}
			if (sText.compare(nOffset, nKeywordLength, pKeyword) != 0)
			{
				return false;
			}

			// The keyword must stand on its own, not as part of a longer word.
			if (nOffset > 0 && IsIdentifierCharacter(sText[nOffset - 1]))
			{
				return false;
			}
			if (nOffset + nKeywordLength < sText.size() &&
				IsIdentifierCharacter(sText[nOffset + nKeywordLength]))
			{
				return false;
			}
			return true;
		}
	}

	HlslccResult ShaderFile::LoadFromFile(const std::string& sFilePath, std::string& outErrorText)
	{
		outErrorText.clear();
		if (sFilePath.empty())
		{
			outErrorText = "the shader file path is empty";
			return HlslccResult::FailInvalidArgument;
		}

		std::string sSourceText;
		if (!ReadWholeFile(sFilePath, sSourceText))
		{
			outErrorText = "cannot read the shader file '" + sFilePath + "'";
			return HlslccResult::FailCannotOpenFile;
		}

		// The absolute path keeps the #include base directory valid no matter
		// which directory the tool was started from.
		m_sFilePath = FilePathUtility::GetAbsolutePath(sFilePath);
		return LoadFromSource(sSourceText, m_sFilePath, outErrorText);
	}

	HlslccResult ShaderFile::LoadFromSource(
		const std::string& sSourceText,
		const std::string& sDisplayName,
		std::string& outErrorText)
	{
		outErrorText.clear();

		m_sSourceText = sSourceText;
		m_sDisplayName = sDisplayName.empty() ? std::string("<memory>") : sDisplayName;
		m_sShaderName = m_sDisplayName;
		m_uRenderQueue = 2000;
		m_bHasShaderBlock = false;
		m_Properties.clear();
		m_KeywordGroups.clear();
		m_Passes.clear();

		ParseBlocks();

		if (m_bHasShaderBlock && m_Passes.empty())
		{
			outErrorText = "the Shader block of '" + m_sDisplayName + "' holds no Pass block";
			if (FindBlockKeywordInFile("Pass"))
			{
				outErrorText += " (a Pass block outside the Shader block is not part of the shader; "
					"Pass blocks belong INSIDE the Shader block)";
			}
			return HlslccResult::FailInvalidArgument;
		}

		if (!m_bHasShaderBlock && FindBlockKeywordInFile("Pass"))
		{
			outErrorText = "the shader file '" + m_sDisplayName +
				"' has a Pass block but no Shader block; Pass blocks belong INSIDE the Shader block";
			return HlslccResult::FailInvalidArgument;
		}

		// Plain HLSL - no Shader block, no Pass block, no Properties block - is the
		// source the engine's runtime compiler hands over; the whole text is the
		// code of one pass.
		if (!m_bHasShaderBlock && m_Passes.empty())
		{
			ShaderPass implicitPass;
			CopyName("Pass0", implicitPass.Name, k_nMaxShaderKeywordLength);
			implicitPass.SourceText = sSourceText;
			implicitPass.VertexEntryPoint = k_sDefaultVertexEntryPoint;
			implicitPass.FragmentEntryPoint = k_sDefaultFragmentEntryPoint;
			m_Passes.push_back(implicitPass);
		}

		// The shader name defaults to the file name without its extension.
		if (m_sShaderName == m_sDisplayName)
		{
			const size_t nSeparatorOffset = m_sDisplayName.find_last_of("\\\\/");
			const std::string sFileName = (nSeparatorOffset == std::string::npos)
				? m_sDisplayName
				: m_sDisplayName.substr(nSeparatorOffset + 1);
			const size_t nExtensionOffset = sFileName.find_last_of('.');
			m_sShaderName = (nExtensionOffset == std::string::npos || nExtensionOffset == 0)
				? sFileName
				: sFileName.substr(0, nExtensionOffset);
		}

		return HlslccResult::Success;
	}

	std::string ShaderFile::GetSourceDirectory() const
	{
		const std::string& sPath = m_sFilePath.empty() ? m_sDisplayName : m_sFilePath;
		const size_t nSeparatorOffset = sPath.find_last_of("\\\\/");
		return (nSeparatorOffset == std::string::npos) ? std::string(".") : sPath.substr(0, nSeparatorOffset);
	}

	bool ShaderFile::SourceDeclaresFunction(const std::string& sSourceText, const std::string& sFunctionName)
	{
		if (sFunctionName.empty())
		{
			return false;
		}

		size_t nSearchOffset = 0;
		while (nSearchOffset < sSourceText.size())
		{
			const size_t nNameOffset = sSourceText.find(sFunctionName, nSearchOffset);
			if (nNameOffset == std::string::npos)
			{
				return false;
			}
			nSearchOffset = nNameOffset + sFunctionName.size();

			if (nNameOffset > 0 && IsIdentifierCharacter(sSourceText[nNameOffset - 1]))
			{
				continue;
			}
			if (nSearchOffset < sSourceText.size() && IsIdentifierCharacter(sSourceText[nSearchOffset]))
			{
				continue;
			}

			size_t nCursor = nSearchOffset;
			while (nCursor < sSourceText.size() && IsWhitespace(sSourceText[nCursor]))
			{
				++nCursor;
			}
			if (nCursor < sSourceText.size() && sSourceText[nCursor] == '(')
			{
				return true;
			}
		}
		return false;
	}

	bool ShaderFile::ParsePragmaLine(
		const std::string& sLine,
		const std::string& sPragmaName,
		std::vector<std::string>& outArguments)
	{
		outArguments.clear();

		const std::string sTrimmedLine = TrimLine(sLine);
		if (sTrimmedLine.size() < 8 || sTrimmedLine.compare(0, 7, "#pragma") != 0)
		{
			return false;
		}

		std::vector<std::string> sTokens;
		SplitTokens(sTrimmedLine.substr(7), sTokens);
		if (sTokens.empty() || sTokens[0] != sPragmaName)
		{
			return false;
		}

		for (size_t nTokenIndex = 1; nTokenIndex < sTokens.size(); ++nTokenIndex)
		{
			outArguments.push_back(sTokens[nTokenIndex]);
		}
		return true;
	}

	// -------------------------------------------------------------------------
	// Structural scan
	// -------------------------------------------------------------------------

	namespace
	{
		// Replaces the contents of every comment and string literal with spaces,
		// keeping the byte offsets and the line structure intact. All keyword and
		// brace scanning runs on this copy, so a "Pass" inside a comment or a brace
		// inside a string can never be mistaken for structure.
		std::string BuildMaskedSourceText(const std::string& sSourceText)
		{
			std::string sMaskedText = sSourceText;

			bool bInLineComment = false;
			bool bInBlockComment = false;
			bool bInString = false;
			bool bInCharacter = false;

			for (size_t nIndex = 0; nIndex < sSourceText.size(); ++nIndex)
			{
				const char cCharacter = sSourceText[nIndex];
				const char cNextCharacter = (nIndex + 1 < sSourceText.size()) ? sSourceText[nIndex + 1] : '\0';

				if (bInLineComment)
				{
					if (cCharacter == '\n') { bInLineComment = false; }
					else { sMaskedText[nIndex] = ' '; }
					continue;
				}
				if (bInBlockComment)
				{
					if (cCharacter == '*' && cNextCharacter == '/')
					{
						sMaskedText[nIndex] = ' ';
						sMaskedText[nIndex + 1] = ' ';
						++nIndex;
						bInBlockComment = false;
					}
					else if (cCharacter != '\n')
					{
						sMaskedText[nIndex] = ' ';
					}
					continue;
				}
				if (bInString || bInCharacter)
				{
					if (cCharacter == '\\' && nIndex + 1 < sSourceText.size())
					{
						sMaskedText[nIndex] = ' ';
						sMaskedText[nIndex + 1] = ' ';
						++nIndex;
						continue;
					}

					const bool bIsTerminator = bInString ? (cCharacter == '"') : (cCharacter == '\'');
					if (bIsTerminator)
					{
						bInString = false;
						bInCharacter = false;
						continue;
					}

					if (cCharacter != '\n')
					{
						sMaskedText[nIndex] = ' ';
					}
					continue;
				}

				if (cCharacter == '/' && cNextCharacter == '/') { bInLineComment = true; sMaskedText[nIndex] = ' '; continue; }
				if (cCharacter == '/' && cNextCharacter == '*') { bInBlockComment = true; sMaskedText[nIndex] = ' '; sMaskedText[nIndex + 1] = ' '; ++nIndex; continue; }
				if (cCharacter == '"') { bInString = true; continue; }
				if (cCharacter == '\'') { bInCharacter = true; continue; }
			}

			return sMaskedText;
		}

		// True when only whitespace stands between the start of the line and
		// nOffset, so a block keyword written inside an expression never counts.
		bool IsFirstTokenOnLine(const std::string& sText, size_t nOffset)
		{
			size_t nCursor = nOffset;
			while (nCursor > 0)
			{
				const char cCharacter = sText[nCursor - 1];
				if (cCharacter == '\n')
				{
					return true;
				}
				if (!IsWhitespace(cCharacter))
				{
					return false;
				}
				--nCursor;
			}
			return true;
		}

		// Finds the next occurrence of a block keyword that starts its own line and
		// opens a block ("{", or a name followed by one).
		size_t FindBlockKeyword(const std::string& sMaskedText, size_t nStartOffset, const char* pKeyword)
		{
			const size_t nKeywordLength = std::strlen(pKeyword);
			size_t nSearchOffset = nStartOffset;

			while (nSearchOffset < sMaskedText.size())
			{
				const size_t nFoundOffset = sMaskedText.find(pKeyword, nSearchOffset);
				if (nFoundOffset == std::string::npos)
				{
					return std::string::npos;
				}
				nSearchOffset = nFoundOffset + 1;

				if (!MatchesKeywordAt(sMaskedText, nFoundOffset, pKeyword))
				{
					continue;
				}
				if (!IsFirstTokenOnLine(sMaskedText, nFoundOffset))
				{
					continue;
				}

				size_t nCursor = nFoundOffset + nKeywordLength;
				while (nCursor < sMaskedText.size() && IsWhitespace(sMaskedText[nCursor]))
				{
					++nCursor;
				}
				if (nCursor >= sMaskedText.size())
				{
					continue;
				}
				if (sMaskedText[nCursor] == '{' || sMaskedText[nCursor] == '"' ||
					IsIdentifierCharacter(sMaskedText[nCursor]))
				{
					return nFoundOffset;
				}
			}
			return std::string::npos;
		}

		// Reads the optional name after a block keyword: "Shader \"Name\"",
		// "Pass Second". The name is read from the ORIGINAL text, because the
		// masked copy has string contents blanked out.
		std::string ReadBlockName(const std::string& sSourceText, size_t nAfterKeywordOffset)
		{
			size_t nCursor = nAfterKeywordOffset;
			while (nCursor < sSourceText.size() && IsWhitespace(sSourceText[nCursor]))
			{
				++nCursor;
			}
			if (nCursor >= sSourceText.size())
			{
				return std::string();
			}

			if (sSourceText[nCursor] == '"')
			{
				const size_t nNameEndOffset = sSourceText.find('"', nCursor + 1);
				return (nNameEndOffset == std::string::npos)
					? std::string()
					: sSourceText.substr(nCursor + 1, nNameEndOffset - nCursor - 1);
			}

			if (!IsIdentifierCharacter(sSourceText[nCursor]))
			{
				return std::string();
			}

			size_t nNameEndOffset = nCursor;
			while (nNameEndOffset < sSourceText.size() && IsIdentifierCharacter(sSourceText[nNameEndOffset]))
			{
				++nNameEndOffset;
			}
			return sSourceText.substr(nCursor, nNameEndOffset - nCursor);
		}

		// Finds the "{ ... }" body that follows nStartOffset. The masked text has
		// no comments and no string contents left, so plain brace counting is exact.
		bool FindBlockBody(
			const std::string& sMaskedText,
			size_t nStartOffset,
			size_t& outBodyOffset,
			size_t& outBodyEndOffset)
		{
			const size_t nOpenOffset = sMaskedText.find('{', nStartOffset);
			if (nOpenOffset == std::string::npos)
			{
				return false;
			}
			outBodyOffset = nOpenOffset + 1;

			uint32_t uDepth = 1;
			for (size_t nCursor = outBodyOffset; nCursor < sMaskedText.size(); ++nCursor)
			{
				if (sMaskedText[nCursor] == '{')
				{
					++uDepth;
				}
				else if (sMaskedText[nCursor] == '}')
				{
					--uDepth;
					if (uDepth == 0)
					{
						outBodyEndOffset = nCursor;
						return true;
					}
				}
			}
			return false;
		}
	}

	void ShaderFile::ParseBlocks()
	{
		// Every structural decision is made on the masked copy; the blocks
		// themselves are cut out of the original text, so the HLSL keeps its
		// comments, its strings and its #include directives.
		const std::string sMaskedSourceText = BuildMaskedSourceText(m_sSourceText);

		size_t nOffset = 0;
		while (nOffset < sMaskedSourceText.size())
		{
			// The top level holds the Properties block and the one Shader block;
			// the Pass blocks live inside the Shader block.
			size_t nKeywordOffset = std::string::npos;
			const char* pKeyword = nullptr;

			for (const char* pCandidate : { "Properties", "Shader" })
			{
				const size_t nFoundOffset = FindBlockKeyword(sMaskedSourceText, nOffset, pCandidate);
				if (nFoundOffset != std::string::npos &&
					(nKeywordOffset == std::string::npos || nFoundOffset < nKeywordOffset))
				{
					nKeywordOffset = nFoundOffset;
					pKeyword = pCandidate;
				}
			}

			if (pKeyword == nullptr)
			{
				break;
			}

			const std::string sBlockName =
				ReadBlockName(m_sSourceText, nKeywordOffset + std::strlen(pKeyword));

			size_t nBodyOffset = 0;
			size_t nBodyEndOffset = 0;
			if (!FindBlockBody(sMaskedSourceText, nKeywordOffset, nBodyOffset, nBodyEndOffset))
			{
				break;
			}

			if (std::strcmp(pKeyword, "Properties") == 0)
			{
				ParsePropertiesBlock(m_sSourceText.substr(nBodyOffset, nBodyEndOffset - nBodyOffset));
			}
			else
			{
				m_bHasShaderBlock = true;
				if (!sBlockName.empty())
				{
					m_sShaderName = sBlockName;
				}
				ParseShaderBlock(sMaskedSourceText, nBodyOffset, nBodyEndOffset);
			}

			nOffset = nBodyEndOffset + 1;
		}
	}

	void ShaderFile::ParseShaderBlock(
		const std::string& sMaskedSourceText,
		size_t nBodyOffset,
		size_t nBodyEndOffset)
	{
		// Walk the Shader block from top to bottom: the text between two nested
		// blocks holds its settings, and every nested block is either a Pass or a
		// Properties block.
		size_t nOffset = nBodyOffset;
		while (nOffset < nBodyEndOffset)
		{
			size_t nNextKeywordOffset = std::string::npos;
			const char* pKeyword = nullptr;

			for (const char* pCandidate : { "Pass", "Properties" })
			{
				const size_t nFoundOffset = FindBlockKeyword(sMaskedSourceText, nOffset, pCandidate);
				if (nFoundOffset != std::string::npos && nFoundOffset < nBodyEndOffset &&
					(nNextKeywordOffset == std::string::npos || nFoundOffset < nNextKeywordOffset))
				{
					nNextKeywordOffset = nFoundOffset;
					pKeyword = pCandidate;
				}
			}

			if (pKeyword == nullptr)
			{
				ParseShaderSettingLines(m_sSourceText.substr(nOffset, nBodyEndOffset - nOffset));
				break;
			}

			// Everything in front of the nested block is a setting line.
			ParseShaderSettingLines(m_sSourceText.substr(nOffset, nNextKeywordOffset - nOffset));

			const std::string sBlockName =
				ReadBlockName(m_sSourceText, nNextKeywordOffset + std::strlen(pKeyword));

			size_t nInnerBodyOffset = 0;
			size_t nInnerBodyEndOffset = 0;
			if (!FindBlockBody(sMaskedSourceText, nNextKeywordOffset, nInnerBodyOffset, nInnerBodyEndOffset) ||
				nInnerBodyEndOffset > nBodyEndOffset)
			{
				break;
			}

			const std::string sInnerBody =
				m_sSourceText.substr(nInnerBodyOffset, nInnerBodyEndOffset - nInnerBodyOffset);

			if (std::strcmp(pKeyword, "Pass") == 0)
			{
				ParsePassBlock(sBlockName, sInnerBody);
			}
			else
			{
				ParsePropertiesBlock(sInnerBody);
			}

			nOffset = nInnerBodyEndOffset + 1;
		}
	}

	bool ShaderFile::FindBlockKeywordInFile(const char* pKeyword) const
	{
		const std::string sMaskedSourceText = BuildMaskedSourceText(m_sSourceText);
		return FindBlockKeyword(sMaskedSourceText, 0, pKeyword) != std::string::npos;
	}

	void ShaderFile::ParseShaderSettingLines(const std::string& sText)
	{
		std::vector<std::string> sLines;
		SplitLines(sText, sLines);

		for (const std::string& sRawLine : sLines)
		{
			// A line that only holds a brace or a comment carries no setting.
			const std::string sLine = TrimLine(sRawLine);
			if (sLine.empty() || sLine[0] == '/' || sLine[0] == '#' || sLine[0] == '{' || sLine[0] == '}')
			{
				continue;
			}

			std::vector<std::string> sTokens;
			SplitTokens(sLine, sTokens);
			if (sTokens.empty())
			{
				continue;
			}

			// "Queue = 2000" and "Queue = \"Geometry\""
			if (sTokens[0] == "Queue")
			{
				for (size_t nTokenIndex = 1; nTokenIndex < sTokens.size(); ++nTokenIndex)
				{
					if (sTokens[nTokenIndex] == "=")
					{
						continue;
					}

					std::string sQueueValue = sTokens[nTokenIndex];
					if (sQueueValue.size() >= 2 && sQueueValue.front() == '"' && sQueueValue.back() == '"')
					{
						const std::string sQueueName = sQueueValue.substr(1, sQueueValue.size() - 2);
						if (sQueueName == "Background")       { m_uRenderQueue = 1000; }
						else if (sQueueName == "Geometry")    { m_uRenderQueue = 2000; }
						else if (sQueueName == "AlphaTest")   { m_uRenderQueue = 2450; }
						else if (sQueueName == "Transparent") { m_uRenderQueue = 3000; }
						else if (sQueueName == "Overlay")     { m_uRenderQueue = 4000; }
						else { m_uRenderQueue = static_cast<uint32_t>(std::atoi(sQueueName.c_str())); }
					}
					else
					{
						m_uRenderQueue = static_cast<uint32_t>(std::atoi(sQueueValue.c_str()));
					}
					break;
				}
				continue;
			}

			// "Variant _A", "VariantLocal = _A _B", ...
			ShaderKeywordKind eKeywordKind;
			if (!ParseKeywordKind(sTokens[0], eKeywordKind))
			{
				continue;
			}

			std::vector<std::string> sKeywordNames;
			for (size_t nTokenIndex = 1; nTokenIndex < sTokens.size(); ++nTokenIndex)
			{
				if (sTokens[nTokenIndex] == "=")
				{
					continue;
				}
				sKeywordNames.push_back(sTokens[nTokenIndex]);
			}

			if (!sKeywordNames.empty())
			{
				AddOrReplaceKeywordGroup(BuildKeywordGroup(sKeywordNames, eKeywordKind));
			}
		}
	}

	void ShaderFile::ParsePropertiesBlock(const std::string& sBody)
	{
		std::vector<std::string> sLines;
		SplitLines(sBody, sLines);

		for (const std::string& sRawLine : sLines)
		{
			const std::string sLine = TrimLine(sRawLine);
			if (sLine.empty() || sLine[0] == '/' || sLine[0] == '#')
			{
				continue;
			}
			if (m_Properties.size() >= k_nMaxShaderPropertyCount)
			{
				break;
			}

			// _Name ("Display", Type) = values
			ShaderProperty property;

			const size_t nOpenParenOffset = sLine.find('(');
			const size_t nCloseParenOffset = sLine.find(')', nOpenParenOffset == std::string::npos ? 0 : nOpenParenOffset);
			if (nOpenParenOffset == std::string::npos || nCloseParenOffset == std::string::npos)
			{
				continue;
			}

			const std::string sName = TrimLine(sLine.substr(0, nOpenParenOffset));
			CopyName(sName, property.Name, k_nMaxShaderKeywordLength);

			// The display name is a quoted string and may contain spaces, so it is
			// read between the quotes instead of being split into tokens.
			const std::string sDeclaration =
				sLine.substr(nOpenParenOffset + 1, nCloseParenOffset - nOpenParenOffset - 1);

			size_t nTypeSearchOffset = 0;
			const size_t nDisplayNameStart = sDeclaration.find('"');
			if (nDisplayNameStart != std::string::npos)
			{
				const size_t nDisplayNameEnd = sDeclaration.find('"', nDisplayNameStart + 1);
				if (nDisplayNameEnd != std::string::npos)
				{
					CopyName(sDeclaration.substr(nDisplayNameStart + 1, nDisplayNameEnd - nDisplayNameStart - 1),
						property.DisplayName, k_nMaxShaderNameLength);
					nTypeSearchOffset = nDisplayNameEnd + 1;
				}
			}

			std::vector<std::string> sTypeTokens;
			SplitTokens(sDeclaration.substr(nTypeSearchOffset), sTypeTokens);
			if (!sTypeTokens.empty())
			{
				ParsePropertyType(sTypeTokens[0], property.eType);
			}

			// Default values after '='.
			const size_t nEqualsOffset = sLine.find('=', nCloseParenOffset);
			if (nEqualsOffset != std::string::npos)
			{
				std::vector<std::string> sValueTokens;
				SplitTokens(sLine.substr(nEqualsOffset + 1), sValueTokens);

				uint32_t uValueIndex = 0;
				for (const std::string& sValueToken : sValueTokens)
				{
					std::string sValue = sValueToken;
					while (!sValue.empty() && (sValue.front() == '(' || sValue.front() == '"'))
					{
						sValue.erase(sValue.begin());
					}
					while (!sValue.empty() && (sValue.back() == ')' || sValue.back() == '"'))
					{
						sValue.pop_back();
					}
					if (sValue.empty() || uValueIndex >= 4)
					{
						continue;
					}

					property.fDefaultValues[uValueIndex] = static_cast<float>(std::atof(sValue.c_str()));
					++uValueIndex;
				}
			}

			if (property.Name[0] != '\0')
			{
				m_Properties.push_back(property);
			}
		}
	}

	void ShaderFile::ParsePassBlock(const std::string& sPassName, const std::string& sBody)
	{
		if (m_Passes.size() >= k_nMaxShaderPassCount)
		{
			return;
		}

		ShaderPass pass;
		CopyName(sPassName.empty() ? ("Pass" + std::to_string(m_Passes.size())) : sPassName,
			pass.Name, k_nMaxShaderKeywordLength);
		pass.VertexEntryPoint = k_sDefaultVertexEntryPoint;
		pass.FragmentEntryPoint = k_sDefaultFragmentEntryPoint;

		// Walk the body line by line: the shader pragmas are read here and left
		// out of the HLSL handed to the compiler (replaced by empty lines so the
		// compiler's line numbers still match the file).
		std::vector<std::string> sLines;
		SplitLines(sBody, sLines);

		for (const std::string& sRawLine : sLines)
		{
			std::vector<std::string> sArguments;

			if (ParsePragmaLine(sRawLine, "vertex", sArguments) && !sArguments.empty())
			{
				pass.VertexEntryPoint = sArguments[0];
				pass.SourceText += "\n";
				continue;
			}
			if (ParsePragmaLine(sRawLine, "fragment", sArguments) && !sArguments.empty())
			{
				pass.FragmentEntryPoint = sArguments[0];
				pass.SourceText += "\n";
				continue;
			}

			// The variant pragmas: "#pragma <kind> <keyword> <keyword> ..."
			bool bIsVariantPragma = false;
			for (const char* pKindName : { "variant", "variant_local", "multi_variant", "multi_variant_local" })
			{
				if (!ParsePragmaLine(sRawLine, pKindName, sArguments))
				{
					continue;
				}

				ShaderKeywordKind eKeywordKind;
				if (ParseKeywordKind(pKindName, eKeywordKind) && !sArguments.empty())
				{
					AddOrReplaceKeywordGroup(BuildKeywordGroup(sArguments, eKeywordKind));
				}
				bIsVariantPragma = true;
				break;
			}
			if (bIsVariantPragma)
			{
				pass.SourceText += "\n";
				continue;
			}

			pass.SourceText += sRawLine;
			pass.SourceText += "\n";
		}

		m_Passes.push_back(pass);
	}

	void ShaderFile::AddOrReplaceKeywordGroup(const ShaderKeywordGroup& keywordGroup)
	{
		// A group declared twice (once in the Shader block, once as a pragma)
		// keeps the strongest declaration: the one that is never stripped.
		for (ShaderKeywordGroup& existingGroup : m_KeywordGroups)
		{
			if (std::strcmp(existingGroup.Name, keywordGroup.Name) != 0)
			{
				continue;
			}

			if (existingGroup.IsStrippable() && !keywordGroup.IsStrippable())
			{
				existingGroup = keywordGroup;
			}
			return;
		}

		if (m_KeywordGroups.size() < k_nMaxShaderKeywordGroupCount)
		{
			m_KeywordGroups.push_back(keywordGroup);
		}
	}

	void ShaderFile::SetEntryPoint(ShaderStage eStage, const std::string& sEntryPointName)
	{
		if (sEntryPointName.empty())
		{
			return;
		}

		for (ShaderPass& pass : m_Passes)
		{
			if (eStage == ShaderStage::Vertex)
			{
				pass.VertexEntryPoint = sEntryPointName;
			}
			else
			{
				pass.FragmentEntryPoint = sEntryPointName;
			}
		}
	}

	bool ShaderFile::HasEntryPoint(ShaderStage eStage) const
	{
		for (const ShaderPass& pass : m_Passes)
		{
			const std::string& sEntryPointName =
				(eStage == ShaderStage::Vertex) ? pass.VertexEntryPoint : pass.FragmentEntryPoint;
			if (SourceDeclaresFunction(pass.SourceText, sEntryPointName))
			{
				return true;
			}
		}
		return false;
	}
}