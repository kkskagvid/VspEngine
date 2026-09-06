#pragma once

#include <cstdio>
#include <string>
#include <type_traits>

#include "Core/Core.h"
#include "Core/String/VspString.h"

namespace Vsp
{
	// -------------------------------------------------------------------------
	// VspFormatSpec
	// -------------------------------------------------------------------------
	// The built-in numeric format specifiers recognized by VspFormatter's
	// Parse method. Custom formatters read the raw spec text from the
	// VspFormatContext instead.
	// -------------------------------------------------------------------------
	enum class VspFormatSpec : uint8_t
	{
		Default,
		HexLower,
		HexUpper,
		HexLowerPrefix,
		HexUpperPrefix,
		FixedFloat,
	};

	// -------------------------------------------------------------------------
	// VspFormatContext
	// -------------------------------------------------------------------------
	// Information VspFormat collects while scanning one "{}" placeholder and
	// hands to a VspFormatter: the raw spec text (between ':' and '}', may be
	// empty) plus the parsed built-in numeric spec.
	// -------------------------------------------------------------------------
	struct VspFormatContext
	{
		const char* pSpecBegin = nullptr;                          // Raw spec text start (custom formatters).
		const char* pSpecEnd = nullptr;                            // Raw spec text end.
		VspFormatSpec eSpec = VspFormatSpec::Default;              // Parsed built-in spec.
	};

	// -------------------------------------------------------------------------
	// VspFormatter<Type>
	// -------------------------------------------------------------------------
	// Customization point for VspFormat. Every formatted type gets a
	// specialization with two static methods:
	//
	//   Parse(const char* pSpecBegin, const char* pSpecEnd, VspFormatContext& context)
	//       Interprets the placeholder's ":spec" text and stores whatever the
	//       Format method needs inside the context.
	//
	//   Format(const VspFormatContext& context, VspString& out, const Type& value)
	//       Appends the formatted value to out.
	//
	// The built-in types (strings, booleans, integrals, floats, pointers) are
	// specialized below; engine types add their own specializations next to
	// their declaration. All methods are static and never throw.
	// -------------------------------------------------------------------------
	template <typename Type, typename Enable = void>
	struct VspFormatter;

	// Forward declaration so the public class below can name the helper.
	namespace VspFormatDetail
	{
		template <typename... ArgumentTypes>
		void FormatInto(VspString& out, const char* pFormatString, const ArgumentTypes&... arguments);
	}

	// -------------------------------------------------------------------------
	// VspFormat
	// -------------------------------------------------------------------------
	// Engine-side string formatter producing VspString output:
	//
	//     VspFormat::Format("Hello {}", sName)             -> "Hello world"
	//     VspFormat::Format("{0} + {1} = {}", 2, 3, 5)     -> "2 + 3 = 5"
	//     VspFormat::Format("value {:#x}", 0x2A)           -> "value 0x2a"
	//     VspFormat::Format("hex {:X}", 255)               -> "hex FF"
	//     VspFormat::Format("fixed {:f}", 1.5)             -> "fixed 1.500000"
	//
	// Custom types simply specialize VspFormatter<MyType> and become usable
	// with any specifier their Parse method accepts. The class never throws:
	// out-of-range argument indices simply produce nothing.
	// -------------------------------------------------------------------------
	class VspFormat
	{
	public:
		template <typename... ArgumentTypes>
		static VspString Format(const char* pFormatString, const ArgumentTypes&... arguments)
		{
			VspString result;
			VspFormatDetail::FormatInto(result, pFormatString, arguments...);
			return result;
		}
	};

	// Implementation details are kept inside VspFormatDetail.
	namespace VspFormatDetail
	{
		template <typename Type>
		inline void AppendWithFormatter(
			VspString& out,
			const char* pSpecBegin,
			const char* pSpecEnd,
			const Type& value)
		{
			// decay so string literals (char[N]) become const char* and
			// cv-qualified arguments reach their intended specialization.
			using StoredType = std::decay_t<Type>;

			VspFormatContext context;
			VspFormatter<StoredType>::Parse(pSpecBegin, pSpecEnd, context);
			VspFormatter<StoredType>::Format(context, out, value);
		}

		// Base case (declared first so the recursive overload below can find
		// it during two-phase lookup): the argument index is out of range and
		// nothing is appended (the engine never throws).
		inline void AppendArgumentByIndex(
			VspString& out,
			size_t nTargetIndex,
			size_t nCurrentIndex,
			const char* pSpecBegin,
			const char* pSpecEnd)
		{
		}

		template <typename ArgumentType, typename... RestTypes>
		inline void AppendArgumentByIndex(
			VspString& out,
			size_t nTargetIndex,
			size_t nCurrentIndex,
			const char* pSpecBegin,
			const char* pSpecEnd,
			const ArgumentType& first,
			const RestTypes&... rest)
		{
			if (nTargetIndex == nCurrentIndex)
			{
				AppendWithFormatter(out, pSpecBegin, pSpecEnd, first);
				return;
			}
			AppendArgumentByIndex(out, nTargetIndex, nCurrentIndex + 1, pSpecBegin, pSpecEnd, rest...);
		}

		// -------- Format string scanning -------------------------------------------

		template <typename... ArgumentTypes>
		inline void FormatInto(VspString& out, const char* pFormatString, const ArgumentTypes&... arguments)
		{
			if (pFormatString == nullptr)
			{
				return;
			}

			size_t nAutoIndex = 0;
			const char* pCursor = pFormatString;

			while (*pCursor != '\0')
			{
				if (*pCursor != '{')
				{
					out.AppendCodePoint(static_cast<char32_t>(static_cast<unsigned char>(*pCursor)));
					++pCursor;
					continue;
				}

				// Skip '{' and parse the placeholder: '{' [index] [':' spec] '}'.
				++pCursor;

				size_t nArgumentIndex = nAutoIndex;
				++nAutoIndex;

				if (*pCursor >= '0' && *pCursor <= '9')
				{
					nArgumentIndex = 0;
					while (*pCursor >= '0' && *pCursor <= '9')
					{
						nArgumentIndex = nArgumentIndex * 10 + static_cast<size_t>(*pCursor - '0');
						++pCursor;
					}
				}

				const char* pSpecBegin = pCursor;
				if (*pCursor == ':')
				{
					pSpecBegin = ++pCursor;
				}

				while (*pCursor != '}' && *pCursor != '\0')
				{
					++pCursor;
				}
				const char* pSpecEnd = pCursor;

				if (*pCursor == '}')
				{
					++pCursor;
				}

				AppendArgumentByIndex(out, nArgumentIndex, 0, pSpecBegin, pSpecEnd, arguments...);
			}
		}
	}

	// =========================================================================
	// Built-in VspFormatter specializations
	// =========================================================================

	// -------- C strings --------------------------------------------------------

	template <>
	struct VspFormatter<const char*>
	{
		static void Parse(const char* pSpecBegin, const char* pSpecEnd, VspFormatContext& context)
		{
		}

		static void Format(const VspFormatContext& context, VspString& out, const char* pValue)
		{
			out.Append(pValue != nullptr ? pValue : "(null)");
		}
	};

	template <>
	struct VspFormatter<char*>
	{
		static void Parse(const char* pSpecBegin, const char* pSpecEnd, VspFormatContext& context)
		{
		}

		static void Format(const VspFormatContext& context, VspString& out, char* pValue)
		{
			out.Append(pValue != nullptr ? pValue : "(null)");
		}
	};

	// -------- std::string / VspString -------------------------------------------

	template <>
	struct VspFormatter<std::string>
	{
		static void Parse(const char* pSpecBegin, const char* pSpecEnd, VspFormatContext& context)
		{
		}

		static void Format(const VspFormatContext& context, VspString& out, const std::string& sValue)
		{
			out.Append(sValue);
		}
	};

	template <>
	struct VspFormatter<VspString>
	{
		static void Parse(const char* pSpecBegin, const char* pSpecEnd, VspFormatContext& context)
		{
		}

		static void Format(const VspFormatContext& context, VspString& out, const VspString& sValue)
		{
			out.Append(sValue);
		}
	};

	// -------- bool --------------------------------------------------------------

	template <>
	struct VspFormatter<bool>
	{
		static void Parse(const char* pSpecBegin, const char* pSpecEnd, VspFormatContext& context)
		{
		}

		static void Format(const VspFormatContext& context, VspString& out, bool bValue)
		{
			out.Append(bValue ? "true" : "false");
		}
	};

	// -------- Floating point -----------------------------------------------------

	namespace VspFormatDetail
	{
		inline void ParseFloatingPointSpec(const char* pSpecBegin, const char* pSpecEnd, VspFormatContext& context)
		{
			context.eSpec = VspFormatSpec::Default;
			if (pSpecBegin != nullptr && pSpecEnd != nullptr && pSpecEnd - pSpecBegin == 1 && *pSpecBegin == 'f')
			{
				context.eSpec = VspFormatSpec::FixedFloat;
			}
		}

		inline void AppendFloatingPoint(VspString& out, double dValue, VspFormatSpec eSpec)
		{
			char sBuffer[64];
			if (eSpec == VspFormatSpec::FixedFloat)
			{
				snprintf(sBuffer, sizeof(sBuffer), "%f", dValue);
			}
			else
			{
				snprintf(sBuffer, sizeof(sBuffer), "%g", dValue);
			}
			out.Append(sBuffer);
		}
	}

	template <>
	struct VspFormatter<float>
	{
		static void Parse(const char* pSpecBegin, const char* pSpecEnd, VspFormatContext& context)
		{
			VspFormatDetail::ParseFloatingPointSpec(pSpecBegin, pSpecEnd, context);
		}

		static void Format(const VspFormatContext& context, VspString& out, float fValue)
		{
			VspFormatDetail::AppendFloatingPoint(out, static_cast<double>(fValue), context.eSpec);
		}
	};

	template <>
	struct VspFormatter<double>
	{
		static void Parse(const char* pSpecBegin, const char* pSpecEnd, VspFormatContext& context)
		{
			VspFormatDetail::ParseFloatingPointSpec(pSpecBegin, pSpecEnd, context);
		}

		static void Format(const VspFormatContext& context, VspString& out, double dValue)
		{
			VspFormatDetail::AppendFloatingPoint(out, dValue, context.eSpec);
		}
	};

	// -------- Integral types ------------------------------------------------------

	namespace VspFormatDetail
	{
		inline void ParseIntegralSpec(const char* pSpecBegin, const char* pSpecEnd, VspFormatContext& context)
		{
			context.eSpec = VspFormatSpec::Default;

			const char* pCursor = pSpecBegin;
			bool bHasPrefix = false;
			if (pCursor != nullptr && pCursor < pSpecEnd && *pCursor == '#')
			{
				bHasPrefix = true;
				++pCursor;
			}

			if (pCursor != nullptr && pSpecEnd != nullptr && pSpecEnd - pCursor == 1)
			{
				switch (*pCursor)
				{
				case 'x': context.eSpec = bHasPrefix ? VspFormatSpec::HexLowerPrefix : VspFormatSpec::HexLower; break;
				case 'X': context.eSpec = bHasPrefix ? VspFormatSpec::HexUpperPrefix : VspFormatSpec::HexUpper; break;
				default: break;
				}
			}
		}

		inline const char* SelectIntegralFormat(VspFormatSpec eSpec, bool bIsSigned)
		{
			switch (eSpec)
			{
			case VspFormatSpec::HexLower:       return "%llx";
			case VspFormatSpec::HexUpper:       return "%llX";
			case VspFormatSpec::HexLowerPrefix: return "0x%llx";
			case VspFormatSpec::HexUpperPrefix: return "0X%llX";
			default:                            return bIsSigned ? "%lld" : "%llu";
			}
		}

		template <typename IntegralType>
		inline void AppendIntegral(VspString& out, IntegralType value, VspFormatSpec eSpec)
		{
			char sBuffer[48];
			const char* pFormat = SelectIntegralFormat(eSpec, std::is_signed_v<IntegralType>);
			if (std::is_signed_v<IntegralType>)
			{
				snprintf(sBuffer, sizeof(sBuffer), pFormat, static_cast<long long>(value));
			}
			else
			{
				snprintf(sBuffer, sizeof(sBuffer), pFormat, static_cast<unsigned long long>(value));
			}
			out.Append(sBuffer);
		}
	}

	template <typename IntegralType>
	struct VspFormatter<IntegralType, std::enable_if_t<
		std::is_integral_v<IntegralType> && !std::is_same_v<IntegralType, bool>>>
	{
		static void Parse(const char* pSpecBegin, const char* pSpecEnd, VspFormatContext& context)
		{
			VspFormatDetail::ParseIntegralSpec(pSpecBegin, pSpecEnd, context);
		}

		static void Format(const VspFormatContext& context, VspString& out, IntegralType value)
		{
			VspFormatDetail::AppendIntegral(out, value, context.eSpec);
		}
	};

	// -------- Pointers -------------------------------------------------------------

	template <>
	struct VspFormatter<const void*>
	{
		static void Parse(const char* pSpecBegin, const char* pSpecEnd, VspFormatContext& context)
		{
		}

		static void Format(const VspFormatContext& context, VspString& out, const void* pValue)
		{
			char sBuffer[32];
			snprintf(sBuffer, sizeof(sBuffer), "%p", pValue);
			out.Append(sBuffer);
		}
	};
}
