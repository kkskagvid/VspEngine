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

		// Finds the block name that starts at nOffset, then its '{' and matching
		// '}'. Braces inside strings and comments are ignored, which is what lets a
		// Pass block hold arbitrary HLSL.
		bool FindBlock(
			const std::string& sText,
			size_t nStartOffset,
			size_t& outBodyOffset,
			size_t& outBodyEndOffset)
		{
			size_t nIndex = nStartOffset;
			while (nIndex < sText.size() && sText[nIndex] != '{')
			{
				++nIndex;
			}
			if (nIndex >= sText.size())
			{
				return false;
			}
			outBodyOffset = nIndex + 1;

			uint32_t uDepth = 1;
			bool bInLineComment = false;
			bool bInBlockComment = false;
			bool bInString = false;
			bool bInCharacter = false;

			for (size_t nCursor = outBodyOffset; nCursor < sText.size(); ++nCursor)
			{
				const char cCharacter = sText[nCursor];
				const char cNextCharacter = (nCursor + 1 < sText.size()) ? sText[nCursor + 1] : '\0';

				if (bInLineComment)
				{
					if (cCharacter == '\n')
					{
						bInLineComment = false;
					}
					continue;
				}
				if (bInBlockComment)
				{
					if (cCharacter == '*' && cNextCharacter == '/')
					{
						bInBlockComment = false;
						++nCursor;
					}
					continue;
				}
				if (bInString)
				{
					if (cCharacter == '\\')
					{
						++nCursor;
						continue;
					}
					if (cCharacter == '"')
					{
						bInString = false;
					}
					continue;
				}
				if (bInCharacter)
				{
					if (cCharacter == '\\')
					{
						++nCursor;
						continue;
					}
					if (cCharacter == '\'')
					{
						bInCharacter = false;
					}
					continue;
				}

				if (cCharacter == '/' && cNextCharacter == '/') { bInLineComment = true; ++nCursor; continue; }
				if (cCharacter == '/' && cNextCharacter == '*') { bInBlockComment = true; ++nCursor; continue; }
				if (cCharacter == '"') { bInString = true; continue; }
				if (cCharacter == '\'') { bInCharacter = true; continue; }

				if (cCharacter == '{') { ++uDepth; continue; }
				if (cCharacter == '}')
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
		return LoadFromSource(sSourceText, m_sFilePath);
	}

	HlslccResult ShaderFile::LoadFromSource(const std::string& sSourceText, const std::string& sDisplayName)
	{
		m_sSourceText = sSourceText;
		m_sDisplayName = sDisplayName.empty() ? std::string("<memory>") : sDisplayName;
		m_sShaderName = m_sDisplayName;
		m_uRenderQueue = 2000;
		m_Properties.clear();
		m_KeywordGroups.clear();
		m_Passes.clear();

		ParseBlocks();

		// A file without any Pass block is plain HLSL: the whole text is the code.
		if (m_Passes.empty())
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

	void ShaderFile::ParseBlocks()
	{
		static const char* const k_pBlockKeywords[] = { "Properties", "Shader", "Pass" };

		size_t nOffset = 0;
		while (nOffset < m_sSourceText.size())
		{
			// Find the next block keyword at the start of a token.
			size_t nKeywordOffset = std::string::npos;
			const char* pKeyword = nullptr;
			for (const char* pCandidate : k_pBlockKeywords)
			{
				size_t nSearchOffset = nOffset;
				while (nSearchOffset < m_sSourceText.size())
				{
					const size_t nFoundOffset = m_sSourceText.find(pCandidate, nSearchOffset);
					if (nFoundOffset == std::string::npos)
					{
						break;
					}
					if (!MatchesKeywordAt(m_sSourceText, nFoundOffset, pCandidate))
					{
						nSearchOffset = nFoundOffset + 1;
						continue;
					}

					// A keyword outside a block only counts when it opens one.
					size_t nLookAhead = nFoundOffset + std::strlen(pCandidate);
					while (nLookAhead < m_sSourceText.size() && IsWhitespace(m_sSourceText[nLookAhead]))
					{
						++nLookAhead;
					}
					if (nLookAhead < m_sSourceText.size() &&
						(m_sSourceText[nLookAhead] == '{' || m_sSourceText[nLookAhead] == '"' ||
							IsIdentifierCharacter(m_sSourceText[nLookAhead])))
					{
						if (nFoundOffset < nKeywordOffset)
						{
							nKeywordOffset = nFoundOffset;
							pKeyword = pCandidate;
						}
					}
					break;
				}
			}

			if (pKeyword == nullptr || nKeywordOffset == std::string::npos)
			{
				break;
			}

			// Optional name after the keyword ("Shader \"Name\"", "Pass Name").
			const size_t nAfterKeywordOffset = nKeywordOffset + std::strlen(pKeyword);
			std::string sBlockName;
			{
				size_t nCursor = nAfterKeywordOffset;
				while (nCursor < m_sSourceText.size() && IsWhitespace(m_sSourceText[nCursor]))
				{
					++nCursor;
				}
				if (nCursor < m_sSourceText.size() && m_sSourceText[nCursor] == '"')
				{
					const size_t nNameEndOffset = m_sSourceText.find('"', nCursor + 1);
					if (nNameEndOffset != std::string::npos)
					{
						sBlockName = m_sSourceText.substr(nCursor + 1, nNameEndOffset - nCursor - 1);
					}
				}
				else if (nCursor < m_sSourceText.size() && IsIdentifierCharacter(m_sSourceText[nCursor]))
				{
					size_t nNameEndOffset = nCursor;
					while (nNameEndOffset < m_sSourceText.size() && IsIdentifierCharacter(m_sSourceText[nNameEndOffset]))
					{
						++nNameEndOffset;
					}
					sBlockName = m_sSourceText.substr(nCursor, nNameEndOffset - nCursor);
				}
			}

			size_t nBodyOffset = 0;
			size_t nBodyEndOffset = 0;
			if (!FindBlock(m_sSourceText, nAfterKeywordOffset, nBodyOffset, nBodyEndOffset))
			{
				break;
			}

			const std::string sBody = m_sSourceText.substr(nBodyOffset, nBodyEndOffset - nBodyOffset);
			const std::string sKeyword(pKeyword);

			if (sKeyword == "Properties")      { ParsePropertiesBlock(sBody); }
			else if (sKeyword == "Shader")     { ParseShaderBlock(sBody); if (!sBlockName.empty()) { m_sShaderName = sBlockName; } }
			else if (sKeyword == "Pass")       { ParsePassBlock(sBlockName, sBody); }

			nOffset = nBodyEndOffset + 1;
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

	void ShaderFile::ParseShaderBlock(const std::string& sBody)
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
