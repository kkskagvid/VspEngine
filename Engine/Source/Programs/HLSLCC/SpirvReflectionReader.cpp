#include "SpirvReflectionReader.h"

#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Hlslcc
{
	namespace
	{
		// ---------------------------------------------------------------------
		// SPIR-V constants used while reading a module
		// ---------------------------------------------------------------------
		enum class SpvOp : uint32_t
		{
			OpName = 5,
			OpMemberName = 6,
			OpEntryPoint = 15,
			OpTypeVoid = 19,
			OpTypeBool = 20,
			OpTypeInt = 21,
			OpTypeFloat = 22,
			OpTypeVector = 23,
			OpTypeMatrix = 24,
			OpTypeImage = 25,
			OpTypeSampler = 26,
			OpTypeSampledImage = 27,
			OpTypeArray = 28,
			OpTypeRuntimeArray = 29,
			OpTypeStruct = 30,
			OpTypePointer = 32,
			OpTypeFunction = 33,
			OpConstant = 43,
			OpVariable = 59,
			OpDecorate = 71,
			OpMemberDecorate = 72,
		};

		enum class SpvDecoration : uint32_t
		{
			Block = 2,
			BufferBlock = 3,
			RowMajor = 4,
			ColMajor = 5,
			ArrayStride = 6,
			MatrixStride = 7,
			BuiltIn = 11,
			Location = 30,
			Binding = 33,
			DescriptorSet = 34,
			Offset = 35,
		};

		enum class SpvStorageClass : uint32_t
		{
			UniformConstant = 0,
			Input = 1,
			Uniform = 2,
			Output = 3,
			Workgroup = 4,
			Private = 6,
			Function = 7,
			PushConstant = 9,
			StorageBuffer = 12,
		};

		enum class SpvExecutionModel : uint32_t
		{
			Vertex = 0,
			Fragment = 4,
		};

		// Number of operands a type instruction may carry. A struct keeps one
		// operand per member PLUS its result id, so the limit has to be larger
		// than the largest struct the reflection reports - otherwise the last
		// members of a big block would silently disappear from the reflection.
		static constexpr uint32_t k_nMaxTypeOperandCount = 64;

		// One type instruction, kept as it was read.
		struct TypeRecord
		{
			uint32_t uOpcode = 0;
			uint32_t uOperands[k_nMaxTypeOperandCount] = {};
			uint32_t uOperandCount = 0;

			bool IsValid() const { return uOpcode != 0; }
		};

		// One global variable of the module.
		struct VariableRecord
		{
			uint32_t uPointerTypeId = 0;
			SpvStorageClass eStorageClass = SpvStorageClass::Function;
		};

		// Builds the lookup tables one module read needs.
		class ModuleReader
		{
		public:
			explicit ModuleReader(const std::vector<uint32_t>& spirvWords) : m_SpirvWords(spirvWords) {}

			bool ReadHeader(std::string& outErrorText);
			void CollectInstructions();

			// -------- Tables filled while reading --------
			std::unordered_map<uint32_t, std::string> Names;
			std::unordered_map<uint64_t, std::string> MemberNames;
			std::unordered_map<uint32_t, TypeRecord> Types;
			std::unordered_map<uint32_t, VariableRecord> Variables;
			std::unordered_map<uint32_t, uint32_t> Constants;

			std::unordered_map<uint32_t, uint32_t> LocationByTarget;
			std::unordered_map<uint32_t, uint32_t> BindingByTarget;
			std::unordered_map<uint32_t, uint32_t> DescriptorSetByTarget;

			// Word of the module that holds the decoration's value, so a caller can
			// rewrite the binding of a variable in place.
			std::unordered_map<uint32_t, uint32_t> BindingWordByTarget;
			std::unordered_map<uint32_t, uint32_t> DescriptorSetWordByTarget;
			std::unordered_map<uint32_t, uint32_t> BuiltInByTarget;
			std::unordered_map<uint32_t, uint32_t> ArrayStrideByTarget;
			std::unordered_map<uint32_t, uint32_t> MatrixStrideByTarget;

			// Member decorations, keyed by (type id, member index).
			std::unordered_map<uint64_t, uint32_t> MemberOffset;
			std::unordered_map<uint64_t, uint32_t> MemberMatrixStride;

			// One OpEntryPoint of the module.
			struct EntryPointRecord
			{
				SpvExecutionModel eExecutionModel = SpvExecutionModel::Vertex;
				uint32_t uFunctionId = 0;
				std::string Name;
				std::vector<uint32_t> InterfaceIds;
			};
			std::vector<EntryPointRecord> EntryPoints;

			// -------- Helpers used by the reflection builder --------
			static uint64_t MakeMemberKey(uint32_t uTypeId, uint32_t uMemberIndex)
			{
				return (static_cast<uint64_t>(uTypeId) << 32) | uMemberIndex;
			}

			// Byte size of one value of the given type. ArrayStride/MatrixStride
			// decorations refine the result so blocks match the Vulkan layout.
			uint32_t ComputeTypeByteSize(uint32_t uTypeId, uint32_t uDepth = 0) const;
			uint32_t ComputeArrayElementCount(uint32_t uTypeId) const;

			// Follows OpTypePointer to the type it points at.
			uint32_t ResolvePointeeType(uint32_t uPointerTypeId) const;

			// Kind of descriptor the pointee type maps onto.
			ShaderResourceKind ClassifyResourceKind(uint32_t uPointeeTypeId, ShaderVariableClass& outVariableClass) const;

			const std::string& FindName(uint32_t uId) const;

		private:
			const std::vector<uint32_t>& m_SpirvWords;

			// Reads the literal string starting at the given instruction operand.
			std::string ReadLiteralString(size_t nWordIndex, uint32_t uWordCount) const;
		};

		bool ModuleReader::ReadHeader(std::string& outErrorText)
		{
			if (m_SpirvWords.size() < 5)
			{
				outErrorText = "the SPIR-V module is shorter than its header";
				return false;
			}
			if (m_SpirvWords[0] != 0x07230203u)
			{
				outErrorText = "the module does not start with the SPIR-V magic number";
				return false;
			}
			return true;
		}

		std::string ModuleReader::ReadLiteralString(size_t nWordIndex, uint32_t uWordCount) const
		{
			std::string sText;
			const char* pBytes = reinterpret_cast<const char*>(m_SpirvWords.data() + nWordIndex);
			const size_t nByteCount = static_cast<size_t>(uWordCount) * sizeof(uint32_t);
			for (size_t nByteIndex = 0; nByteIndex < nByteCount; ++nByteIndex)
			{
				if (pBytes[nByteIndex] == '\0')
				{
					break;
				}
				sText.push_back(pBytes[nByteIndex]);
			}
			return sText;
		}

		void ModuleReader::CollectInstructions()
		{
			size_t nWordIndex = 5;
			while (nWordIndex < m_SpirvWords.size())
			{
				const uint32_t uFirstWord = m_SpirvWords[nWordIndex];
				const uint32_t uWordCount = uFirstWord >> 16;
				const uint32_t uOpcode = uFirstWord & 0xFFFFu;
				if (uWordCount == 0 || nWordIndex + uWordCount > m_SpirvWords.size())
				{
					break;   // Truncated module: stop at the last complete instruction.
				}

				const uint32_t* pOperands = m_SpirvWords.data() + nWordIndex + 1;
				const uint32_t uOperandCount = uWordCount - 1;

				switch (static_cast<SpvOp>(uOpcode))
				{
				case SpvOp::OpName:
					if (uOperandCount >= 2)
					{
						Names[pOperands[0]] = ReadLiteralString(nWordIndex + 2, uOperandCount - 1);
					}
					break;

				case SpvOp::OpMemberName:
					if (uOperandCount >= 3)
					{
						MemberNames[MakeMemberKey(pOperands[0], pOperands[1])] =
							ReadLiteralString(nWordIndex + 3, uOperandCount - 2);
					}
					break;

				case SpvOp::OpEntryPoint:
					if (uOperandCount >= 3)
					{
						EntryPointRecord entryPoint;
						entryPoint.eExecutionModel = static_cast<SpvExecutionModel>(pOperands[0]);
						entryPoint.uFunctionId = pOperands[1];

						// The name is a literal string packed in the following words.
						size_t nNameWordIndex = nWordIndex + 3;
						uint32_t uNameWordCount = 0;
						while (nNameWordIndex + uNameWordCount < nWordIndex + uWordCount)
						{
							const char* pBytes = reinterpret_cast<const char*>(m_SpirvWords.data() + nNameWordIndex + uNameWordCount);
							++uNameWordCount;
							if (pBytes[0] == '\0' || pBytes[1] == '\0' || pBytes[2] == '\0' || pBytes[3] == '\0')
							{
								break;
							}
						}
						entryPoint.Name = ReadLiteralString(nNameWordIndex, uNameWordCount);

						for (size_t nInterfaceIndex = nNameWordIndex + uNameWordCount;
							nInterfaceIndex < nWordIndex + uWordCount; ++nInterfaceIndex)
						{
							entryPoint.InterfaceIds.push_back(m_SpirvWords[nInterfaceIndex]);
						}
						EntryPoints.push_back(std::move(entryPoint));
					}
					break;

				case SpvOp::OpTypeInt:
				case SpvOp::OpTypeFloat:
				case SpvOp::OpTypeVector:
				case SpvOp::OpTypeMatrix:
				case SpvOp::OpTypeImage:
				case SpvOp::OpTypeSampledImage:
				case SpvOp::OpTypeArray:
				case SpvOp::OpTypeRuntimeArray:
				case SpvOp::OpTypeStruct:
				case SpvOp::OpTypePointer:
				case SpvOp::OpTypeSampler:
				case SpvOp::OpTypeVoid:
				case SpvOp::OpTypeBool:
				case SpvOp::OpTypeFunction:
				{
					if (uOperandCount == 0)
					{
						break;
					}
					// Every type instruction starts with its result id.
					const uint32_t uResultId = pOperands[0];
					TypeRecord& record = Types[uResultId];
					record.uOpcode = uOpcode;
					record.uOperandCount = (uOperandCount < k_nMaxTypeOperandCount)
						? uOperandCount
						: k_nMaxTypeOperandCount;
					for (uint32_t uOperandIndex = 0; uOperandIndex < record.uOperandCount; ++uOperandIndex)
					{
						record.uOperands[uOperandIndex] = pOperands[uOperandIndex];
					}
					break;
				}

				case SpvOp::OpConstant:
					if (uOperandCount >= 3)
					{
						Constants[pOperands[1]] = pOperands[2];
					}
					break;

				case SpvOp::OpVariable:
					if (uOperandCount >= 3)
					{
						VariableRecord variable;
						variable.uPointerTypeId = pOperands[0];
						variable.eStorageClass = static_cast<SpvStorageClass>(pOperands[2]);
						Variables[pOperands[1]] = variable;
					}
					break;

				case SpvOp::OpDecorate:
					if (uOperandCount >= 2)
					{
						const uint32_t uTarget = pOperands[0];
						const SpvDecoration eDecoration = static_cast<SpvDecoration>(pOperands[1]);
						const uint32_t uValue = (uOperandCount >= 3) ? pOperands[2] : 0;
						// OpDecorate: word 0 is the instruction header, then the
						// target, the decoration and the value.
						const uint32_t uValueWordIndex = static_cast<uint32_t>(nWordIndex + 3);

						switch (eDecoration)
						{
						case SpvDecoration::Location:      LocationByTarget[uTarget] = uValue; break;
						case SpvDecoration::Binding:
							BindingByTarget[uTarget] = uValue;
							BindingWordByTarget[uTarget] = uValueWordIndex;
							break;
						case SpvDecoration::DescriptorSet:
							DescriptorSetByTarget[uTarget] = uValue;
							DescriptorSetWordByTarget[uTarget] = uValueWordIndex;
							break;
						case SpvDecoration::BuiltIn:       BuiltInByTarget[uTarget] = uValue; break;
						case SpvDecoration::ArrayStride:   ArrayStrideByTarget[uTarget] = uValue; break;
						case SpvDecoration::MatrixStride:  MatrixStrideByTarget[uTarget] = uValue; break;
						default: break;
						}
					}
					break;

				case SpvOp::OpMemberDecorate:
					if (uOperandCount >= 3)
					{
						const uint64_t uMemberKey = MakeMemberKey(pOperands[0], pOperands[1]);
						const SpvDecoration eDecoration = static_cast<SpvDecoration>(pOperands[2]);
						const uint32_t uValue = (uOperandCount >= 4) ? pOperands[3] : 0;
						switch (eDecoration)
						{
						case SpvDecoration::Offset:       MemberOffset[uMemberKey] = uValue; break;
						case SpvDecoration::MatrixStride: MemberMatrixStride[uMemberKey] = uValue; break;
						default: break;
						}
					}
					break;

				default:
					break;
				}

				nWordIndex += uWordCount;
			}
		}

		const std::string& ModuleReader::FindName(uint32_t uId) const
		{
			static const std::string s_EmptyName;
			const auto nameIterator = Names.find(uId);
			return nameIterator != Names.end() ? nameIterator->second : s_EmptyName;
		}

		uint32_t ModuleReader::ResolvePointeeType(uint32_t uPointerTypeId) const
		{
			const auto typeIterator = Types.find(uPointerTypeId);
			if (typeIterator == Types.end() || typeIterator->second.uOpcode != static_cast<uint32_t>(SpvOp::OpTypePointer))
			{
				return 0;
			}
			// OpTypePointer: result id, storage class, pointee type.
			return typeIterator->second.uOperandCount >= 3 ? typeIterator->second.uOperands[2] : 0;
		}

		uint32_t ModuleReader::ComputeArrayElementCount(uint32_t uTypeId) const
		{
			const auto typeIterator = Types.find(uTypeId);
			if (typeIterator == Types.end() || typeIterator->second.uOpcode != static_cast<uint32_t>(SpvOp::OpTypeArray))
			{
				return 0;   // Runtime array or not an array at all.
			}
			// OpTypeArray: result id, element type, length constant id.
			if (typeIterator->second.uOperandCount < 3)
			{
				return 0;
			}
			const auto constantIterator = Constants.find(typeIterator->second.uOperands[2]);
			return constantIterator != Constants.end() ? constantIterator->second : 0;
		}

		uint32_t ModuleReader::ComputeTypeByteSize(uint32_t uTypeId, uint32_t uDepth) const
		{
			if (uTypeId == 0 || uDepth > 8)
			{
				return 0;
			}

			const auto typeIterator = Types.find(uTypeId);
			if (typeIterator == Types.end())
			{
				return 0;
			}

			const TypeRecord& type = typeIterator->second;
			switch (static_cast<SpvOp>(type.uOpcode))
			{
			case SpvOp::OpTypeBool:
				return 4;

			case SpvOp::OpTypeInt:
			case SpvOp::OpTypeFloat:
				// Operands: result id, width in bits.
				return type.uOperandCount >= 2 ? type.uOperands[1] / 8u : 0;

			case SpvOp::OpTypeVector:
			{
				// Operands: result id, component type, component count.
				if (type.uOperandCount < 3)
				{
					return 0;
				}
				return ComputeTypeByteSize(type.uOperands[1], uDepth + 1) * type.uOperands[2];
			}

			case SpvOp::OpTypeMatrix:
			{
				// Operands: result id, column type, column count.
				if (type.uOperandCount < 3)
				{
					return 0;
				}
				const uint32_t uColumnByteSize = ComputeTypeByteSize(type.uOperands[1], uDepth + 1);
				const uint32_t uColumnCount = type.uOperands[2];

				// A matrix in a block is laid out with a stride between columns
				// (or rows when it is row major).
				const auto strideIterator = MatrixStrideByTarget.find(uTypeId);
				if (strideIterator != MatrixStrideByTarget.end() && uColumnCount > 0)
				{
					return strideIterator->second * (uColumnCount - 1u) + uColumnByteSize;
				}
				return uColumnByteSize * uColumnCount;
			}

			case SpvOp::OpTypeArray:
			{
				if (type.uOperandCount < 3)
				{
					return 0;
				}
				const uint32_t uElementByteSize = ComputeTypeByteSize(type.uOperands[1], uDepth + 1);
				const uint32_t uElementCount = ComputeArrayElementCount(uTypeId);
				const auto strideIterator = ArrayStrideByTarget.find(uTypeId);
				if (strideIterator != ArrayStrideByTarget.end() && uElementCount > 0)
				{
					return strideIterator->second * (uElementCount - 1u) + uElementByteSize;
				}
				return uElementByteSize * uElementCount;
			}

			case SpvOp::OpTypeRuntimeArray:
				// Operands: result id, element type. The length is only known to
				// the shader, so only the element size is reported.
				return type.uOperandCount >= 2 ? ComputeTypeByteSize(type.uOperands[1], uDepth + 1) : 0;

			case SpvOp::OpTypeStruct:
			{
				// Operands: result id followed by the member type ids.
				uint32_t uBlockByteSize = 0;
				for (uint32_t uMemberIndex = 1; uMemberIndex < type.uOperandCount; ++uMemberIndex)
				{
					const uint32_t uMemberByteSize = ComputeTypeByteSize(type.uOperands[uMemberIndex], uDepth + 1);
					const auto offsetIterator = MemberOffset.find(MakeMemberKey(uTypeId, uMemberIndex - 1u));
					const uint32_t uMemberOffset = (offsetIterator != MemberOffset.end())
						? offsetIterator->second
						: uBlockByteSize;

					const uint32_t uMemberEnd = uMemberOffset + uMemberByteSize;
					if (uMemberEnd > uBlockByteSize)
					{
						uBlockByteSize = uMemberEnd;
					}
				}
				return uBlockByteSize;
			}

			default:
				return 0;
			}
		}

		ShaderResourceKind ModuleReader::ClassifyResourceKind(
			uint32_t uPointeeTypeId,
			ShaderVariableClass& outVariableClass) const
		{
			outVariableClass = ShaderVariableClass::Unknown;

			const auto typeIterator = Types.find(uPointeeTypeId);
			if (typeIterator == Types.end())
			{
				return ShaderResourceKind::Unknown;
			}

			const TypeRecord& type = typeIterator->second;
			switch (static_cast<SpvOp>(type.uOpcode))
			{
			case SpvOp::OpTypeStruct:
			{
				outVariableClass = ShaderVariableClass::Struct;
				// A struct that is a Block is a uniform buffer; a BufferBlock is a
				// storage buffer. Both are reported as the buffer they describe.
				return ShaderResourceKind::UniformBuffer;
			}

			case SpvOp::OpTypeSampledImage:
				outVariableClass = ShaderVariableClass::Image;
				return ShaderResourceKind::CombinedImageSampler;

			case SpvOp::OpTypeImage:
			{
				outVariableClass = ShaderVariableClass::Image;
				// Operands: result id, sampled type, dim, depth, arrayed,
				// multisampled, sampled, format, access qualifier.
				const uint32_t uSampled = (type.uOperandCount >= 7) ? type.uOperands[6] : 1u;
				return (uSampled == 2u) ? ShaderResourceKind::StorageImage : ShaderResourceKind::SampledImage;
			}

			case SpvOp::OpTypeSampler:
				outVariableClass = ShaderVariableClass::Sampler;
				return ShaderResourceKind::Sampler;

			case SpvOp::OpTypeArray:
			case SpvOp::OpTypeRuntimeArray:
				return ClassifyResourceKind(type.uOperandCount >= 2 ? type.uOperands[1] : 0, outVariableClass);

			default:
				return ShaderResourceKind::Unknown;
			}
		}

		// ---------------------------------------------------------------------
		// Building the reflection from the tables
		// ---------------------------------------------------------------------

		void CopyTextToName(const std::string& sText, char (&outName)[k_nMaxShaderVariableNameLength])
		{
			const size_t nCopyByteCount =
				sText.size() < (k_nMaxShaderVariableNameLength - 1u) ? sText.size() : (k_nMaxShaderVariableNameLength - 1u);
			if (nCopyByteCount > 0)
			{
				std::memcpy(outName, sText.data(), nCopyByteCount);
			}
			outName[nCopyByteCount] = '\0';
		}

		// Fills the type description of one value type.
		void DescribeValueType(const ModuleReader& reader, uint32_t uTypeId, ShaderTypeInfo& outType)
		{
			outType = ShaderTypeInfo();
			outType.uByteSize = reader.ComputeTypeByteSize(uTypeId);

			const auto typeIterator = reader.Types.find(uTypeId);
			if (typeIterator == reader.Types.end())
			{
				outType.eClass = ShaderVariableClass::Unknown;
				return;
			}

			const TypeRecord& type = typeIterator->second;
			switch (static_cast<SpvOp>(type.uOpcode))
			{
			case SpvOp::OpTypeFloat:
			case SpvOp::OpTypeInt:
				outType.eClass = ShaderVariableClass::Scalar;
				outType.uComponentCount = 1;
				break;

			case SpvOp::OpTypeVector:
				outType.eClass = ShaderVariableClass::Vector;
				outType.uComponentCount = (type.uOperandCount >= 3) ? type.uOperands[2] : 0;
				break;

			case SpvOp::OpTypeMatrix:
				outType.eClass = ShaderVariableClass::Matrix;
				outType.uComponentCount = (type.uOperandCount >= 3) ? type.uOperands[2] : 0;
				outType.uRowCount = 0;
				// Rows come from the column type when it is a vector.
				if (type.uOperandCount >= 2)
				{
					const auto columnIterator = reader.Types.find(type.uOperands[1]);
					if (columnIterator != reader.Types.end() &&
						static_cast<SpvOp>(columnIterator->second.uOpcode) == SpvOp::OpTypeVector &&
						columnIterator->second.uOperandCount >= 3)
					{
						outType.uRowCount = columnIterator->second.uOperands[2];
					}
				}
				break;

			case SpvOp::OpTypeStruct:
				outType.eClass = ShaderVariableClass::Struct;
				break;

			case SpvOp::OpTypeImage:
			case SpvOp::OpTypeSampledImage:
				outType.eClass = ShaderVariableClass::Image;
				break;

			case SpvOp::OpTypeSampler:
				outType.eClass = ShaderVariableClass::Sampler;
				break;

			case SpvOp::OpTypeArray:
			case SpvOp::OpTypeRuntimeArray:
				// Report the element's shape; the array itself is described by the
				// resource's descriptor count.
				DescribeValueType(reader, type.uOperandCount >= 2 ? type.uOperands[1] : 0, outType);
				break;

			default:
				outType.eClass = ShaderVariableClass::Unknown;
				break;
			}
		}
	}

	HlslccResult SpirvReflectionReader::CollectResources(
		const std::vector<uint32_t>& spirvWords,
		std::vector<SpirvResourceVariable>& outResources,
		std::string& outErrorText)
	{
		outErrorText.clear();
		outResources.clear();

		ModuleReader reader(spirvWords);
		if (!reader.ReadHeader(outErrorText))
		{
			return HlslccResult::FailReflection;
		}
		reader.CollectInstructions();

		// Declaration order is what the engine's binding rules number by, so the
		// variables are sorted by their result id instead of iterating the map.
		std::vector<std::pair<uint32_t, const VariableRecord*>> resourceVariables;
		for (const auto& variableEntry : reader.Variables)
		{
			const VariableRecord& variable = variableEntry.second;
			if (variable.eStorageClass == SpvStorageClass::UniformConstant ||
				variable.eStorageClass == SpvStorageClass::Uniform ||
				variable.eStorageClass == SpvStorageClass::StorageBuffer)
			{
				resourceVariables.emplace_back(variableEntry.first, &variable);
			}
		}

		std::sort(
			resourceVariables.begin(),
			resourceVariables.end(),
			[](const std::pair<uint32_t, const VariableRecord*>& left,
				const std::pair<uint32_t, const VariableRecord*>& right)
			{
				return left.first < right.first;
			});

		for (const auto& variableEntry : resourceVariables)
		{
			const uint32_t uVariableId = variableEntry.first;
			const VariableRecord& variable = *variableEntry.second;
			const uint32_t uPointeeTypeId = reader.ResolvePointeeType(variable.uPointerTypeId);

			SpirvResourceVariable resource;
			resource.uVariableId = uVariableId;

			const std::string& sResourceName = reader.FindName(uVariableId);
			const size_t nNameByteCount = sResourceName.size() < (k_nMaxShaderVariableNameLength - 1u)
				? sResourceName.size()
				: (k_nMaxShaderVariableNameLength - 1u);
			if (nNameByteCount > 0)
			{
				std::memcpy(resource.Name, sResourceName.data(), nNameByteCount);
			}
			resource.Name[nNameByteCount] = '\0';

			ShaderVariableClass eVariableClass = ShaderVariableClass::Unknown;
			resource.eKind = reader.ClassifyResourceKind(uPointeeTypeId, eVariableClass);

			const auto setWordIterator = reader.DescriptorSetWordByTarget.find(uVariableId);
			if (setWordIterator != reader.DescriptorSetWordByTarget.end())
			{
				resource.uDescriptorSetWordIndex = setWordIterator->second;
			}
			const auto bindingWordIterator = reader.BindingWordByTarget.find(uVariableId);
			if (bindingWordIterator != reader.BindingWordByTarget.end())
			{
				resource.uBindingWordIndex = bindingWordIterator->second;
			}

			outResources.push_back(resource);
		}

		return HlslccResult::Success;
	}

	HlslccResult SpirvReflectionReader::Read(
		const std::vector<uint32_t>& spirvWords,
		ShaderStage eStage,
		ShaderStageReflection& outReflection,
		std::string& outErrorText)
	{
		outErrorText.clear();
		outReflection = ShaderStageReflection();
		outReflection.eStage = eStage;

		ModuleReader reader(spirvWords);
		if (!reader.ReadHeader(outErrorText))
		{
			return HlslccResult::FailReflection;
		}
		reader.CollectInstructions();

		// ---- Entry point of the requested stage ----
		const SpvExecutionModel eWantedExecutionModel = (eStage == ShaderStage::Vertex)
			? SpvExecutionModel::Vertex
			: SpvExecutionModel::Fragment;

		const ModuleReader::EntryPointRecord* pEntryPoint = nullptr;
		for (const ModuleReader::EntryPointRecord& entryPoint : reader.EntryPoints)
		{
			if (entryPoint.eExecutionModel == eWantedExecutionModel)
			{
				pEntryPoint = &entryPoint;
				break;
			}
		}

		if (pEntryPoint == nullptr)
		{
			outErrorText = std::string("the SPIR-V module has no ") + ToStageName(eStage) + " entry point";
			return HlslccResult::FailReflection;
		}

		CopyTextToName(pEntryPoint->Name, outReflection.EntryPointName);

		// The interface list of the entry point restricts which Input/Output
		// variables belong to it. Older SPIR-V versions list only those, newer
		// ones list every global - both are handled.
		auto IsInInterface = [pEntryPoint](uint32_t uVariableId) -> bool
		{
			if (pEntryPoint->InterfaceIds.empty())
			{
				return true;
			}
			for (uint32_t uInterfaceId : pEntryPoint->InterfaceIds)
			{
				if (uInterfaceId == uVariableId)
				{
					return true;
				}
			}
			return false;
		};

		// Collect the variables of each storage class once.
		std::vector<std::pair<uint32_t, const VariableRecord*>> inputVariables;
		std::vector<std::pair<uint32_t, const VariableRecord*>> outputVariables;
		std::vector<std::pair<uint32_t, const VariableRecord*>> resourceVariables;
		uint32_t uPushConstantVariableId = 0;
		uint32_t uPushConstantPointeeTypeId = 0;

		for (const auto& variableEntry : reader.Variables)
		{
			const uint32_t uVariableId = variableEntry.first;
			const VariableRecord& variable = variableEntry.second;

			switch (variable.eStorageClass)
			{
			case SpvStorageClass::Input:
				if (IsInInterface(uVariableId))
				{
					inputVariables.emplace_back(uVariableId, &variable);
				}
				break;

			case SpvStorageClass::Output:
				if (IsInInterface(uVariableId))
				{
					outputVariables.emplace_back(uVariableId, &variable);
				}
				break;

			case SpvStorageClass::UniformConstant:
			case SpvStorageClass::Uniform:
			case SpvStorageClass::StorageBuffer:
				resourceVariables.emplace_back(uVariableId, &variable);
				break;

			case SpvStorageClass::PushConstant:
				uPushConstantVariableId = uVariableId;
				uPushConstantPointeeTypeId = reader.ResolvePointeeType(variable.uPointerTypeId);
				break;

			default:
				break;
			}
		}

		// Stable output: order the interface variables by location.
		auto SortByLocation = [&reader](std::vector<std::pair<uint32_t, const VariableRecord*>>& variables)
		{
			std::sort(
				variables.begin(),
				variables.end(),
				[&reader](const std::pair<uint32_t, const VariableRecord*>& left,
					const std::pair<uint32_t, const VariableRecord*>& right)
				{
					const auto leftLocation = reader.LocationByTarget.find(left.first);
					const auto rightLocation = reader.LocationByTarget.find(right.first);
					const uint32_t uLeftLocation = (leftLocation != reader.LocationByTarget.end()) ? leftLocation->second : 0xFFFFFFFFu;
					const uint32_t uRightLocation = (rightLocation != reader.LocationByTarget.end()) ? rightLocation->second : 0xFFFFFFFFu;
					return uLeftLocation < uRightLocation;
				});
		};
		SortByLocation(inputVariables);
		SortByLocation(outputVariables);

		auto FillInterfaceVariables =
			[](ModuleReader& reader,
				const std::vector<std::pair<uint32_t, const VariableRecord*>>& variables,
				ShaderVariable* pOutVariables,
				uint32_t uCapacity,
				uint32_t& outCount)
		{
			outCount = 0;
			for (const auto& variableEntry : variables)
			{
				if (outCount >= uCapacity)
				{
					break;
				}

				const uint32_t uVariableId = variableEntry.first;
				const VariableRecord& variable = *variableEntry.second;

				ShaderVariable& outVariable = pOutVariables[outCount];
				CopyTextToName(reader.FindName(uVariableId), outVariable.Name);

				const auto locationIterator = reader.LocationByTarget.find(uVariableId);
				outVariable.uLocation = (locationIterator != reader.LocationByTarget.end()) ? locationIterator->second : 0;

				const auto builtInIterator = reader.BuiltInByTarget.find(uVariableId);
				outVariable.bIsBuiltIn = builtInIterator != reader.BuiltInByTarget.end();

				DescribeValueType(reader, reader.ResolvePointeeType(variable.uPointerTypeId), outVariable.Type);
				++outCount;
			}
		};

		FillInterfaceVariables(reader, inputVariables, outReflection.Inputs, k_nMaxShaderInputCount, outReflection.uInputCount);
		FillInterfaceVariables(reader, outputVariables, outReflection.Outputs, k_nMaxShaderOutputCount, outReflection.uOutputCount);

		// ---- Resources ----
		for (const auto& variableEntry : resourceVariables)
		{
			if (outReflection.uResourceCount >= k_nMaxShaderResourceCount)
			{
				break;
			}

			const uint32_t uVariableId = variableEntry.first;
			const VariableRecord& variable = *variableEntry.second;
			const uint32_t uPointeeTypeId = reader.ResolvePointeeType(variable.uPointerTypeId);

			ShaderResourceBinding& outBinding = outReflection.Resources[outReflection.uResourceCount];
			CopyTextToName(reader.FindName(uVariableId), outBinding.Name);

			ShaderVariableClass eVariableClass = ShaderVariableClass::Unknown;
			outBinding.eKind = reader.ClassifyResourceKind(uPointeeTypeId, eVariableClass);

			const auto setIterator = reader.DescriptorSetByTarget.find(uVariableId);
			outBinding.uDescriptorSet = (setIterator != reader.DescriptorSetByTarget.end()) ? setIterator->second : 0;
			const auto bindingIterator = reader.BindingByTarget.find(uVariableId);
			outBinding.uBinding = (bindingIterator != reader.BindingByTarget.end()) ? bindingIterator->second : 0;

			outBinding.uElementByteSize = reader.ComputeTypeByteSize(uPointeeTypeId);

			// Runtime arrays report 0 elements: the descriptor count is only
			// known to the pipeline that binds them.
			const auto typeIterator = reader.Types.find(uPointeeTypeId);
			if (typeIterator != reader.Types.end() &&
				static_cast<SpvOp>(typeIterator->second.uOpcode) == SpvOp::OpTypeRuntimeArray)
			{
				outBinding.uDescriptorCount = 0;
				outBinding.uElementByteSize = reader.ComputeTypeByteSize(
					typeIterator->second.uOperandCount >= 2 ? typeIterator->second.uOperands[1] : 0);
			}
			else if (typeIterator != reader.Types.end() &&
				static_cast<SpvOp>(typeIterator->second.uOpcode) == SpvOp::OpTypeArray)
			{
				outBinding.uDescriptorCount = reader.ComputeArrayElementCount(uPointeeTypeId);
				outBinding.uElementByteSize = reader.ComputeTypeByteSize(
					typeIterator->second.uOperandCount >= 2 ? typeIterator->second.uOperands[1] : 0);
			}
			else
			{
				outBinding.uDescriptorCount = 1;
			}

			++outReflection.uResourceCount;
		}

		// ---- Push constants ----
		if (uPushConstantVariableId != 0 && uPushConstantPointeeTypeId != 0)
		{
			const auto structIterator = reader.Types.find(uPushConstantPointeeTypeId);
			if (structIterator != reader.Types.end() &&
				static_cast<SpvOp>(structIterator->second.uOpcode) == SpvOp::OpTypeStruct)
			{
				const TypeRecord& structType = structIterator->second;
				for (uint32_t uMemberIndex = 1; uMemberIndex < structType.uOperandCount; ++uMemberIndex)
				{
					if (outReflection.uPushConstantMemberCount >= k_nMaxShaderPushConstantMemberCount)
					{
						break;
					}

					const uint32_t uMemberTypeId = structType.uOperands[uMemberIndex];
					const uint64_t uMemberKey = ModuleReader::MakeMemberKey(uPushConstantPointeeTypeId, uMemberIndex - 1u);

					ShaderPushConstantMember& outMember =
						outReflection.PushConstantMembers[outReflection.uPushConstantMemberCount];
					CopyTextToName(reader.MemberNames.count(uMemberKey) > 0 ? reader.MemberNames[uMemberKey] : std::string(), outMember.Name);

					const auto offsetIterator = reader.MemberOffset.find(uMemberKey);
					outMember.uByteOffset = (offsetIterator != reader.MemberOffset.end()) ? offsetIterator->second : 0;
					outMember.uByteSize = reader.ComputeTypeByteSize(uMemberTypeId);

					++outReflection.uPushConstantMemberCount;
				}

				outReflection.uPushConstantByteSize = reader.ComputeTypeByteSize(uPushConstantPointeeTypeId);

				// Vulkan requires the range to be a multiple of 4.
				outReflection.uPushConstantByteSize = (outReflection.uPushConstantByteSize + 3u) & ~3u;
			}
		}

		return HlslccResult::Success;
	}
}
