#pragma once

#include <memory>
#include <type_traits>
#include <utility>

#include "Engine/Core/Core.h"
#include "Engine/Core/Templates/Allocator.h"
#include "Engine/Core/Templates/VspObject.h"

namespace Vsp
{
	// -------------------------------------------------------------------------
	// Callback<ReturnType(ArgumentTypes...)>
	// -------------------------------------------------------------------------
	// Type-erased callback that can bind a free/static function, a functor or
	// lambda, or a member function together with its object:
	//
	//     Callback<int(double)> C1([](double Value) { return (int)Value; });
	//     Callback<int(int)>  C2(&SomeFunction);
	//     Callback<int()>     C3(&SomeObject, &ObjectType::GetValue);
	//
	// Constraint (TARGET): when the return type is a class/struct it MUST
	// derive from Vsp::Object; void, fundamental types (int, double, bool, ...),
	// enums and pointers need no base class. The check is enforced at compile
	// time with a static_assert.
	//
	// Every invoker is carved out of the callback's private Allocator (OS
	// layout), so no C++ exceptions are involved anywhere in this class.
	//
	// The object of a member-function callback is borrowed, not owned: it must
	// outlive the callback. Invoking an unbound callback is misuse
	// (DEBUG_BREAK, then a default-constructed return value is produced).
	// Copying a callback deep-copies the bound callable; binding a move-only
	// callable makes copying fall back to an unbound callback after
	// DEBUG_BREAK().
	// -------------------------------------------------------------------------

	namespace CallbackDetail
	{
		// Dependent-false helper so the primary template's static_assert only
		// fires when the invalid form is actually instantiated.
		template <typename Type>
		inline constexpr bool bIsInvalidCallbackType = false;

		// Carves one invoker out of the given allocator and constructs it.
		template <typename InvokerType, typename... ConstructorArgumentTypes>
		InvokerType* AllocateInvoker(Allocator& Allocator, ConstructorArgumentTypes&&... ConstructorArguments)
		{
			void* pMemory = Allocator.Allocate(sizeof(InvokerType));
			if (pMemory == nullptr)
			{
				DEBUG_BREAK();
				return nullptr;
			}
			return std::construct_at(static_cast<InvokerType*>(pMemory), std::forward<ConstructorArgumentTypes>(ConstructorArguments)...);
		}

		// Destroys an invoker and returns its memory to the allocator.
		template <typename InvokerBaseType>
		void DeleteInvoker(Allocator& Allocator, InvokerBaseType* pInvoker)
		{
			if (pInvoker == nullptr)
			{
				return;
			}
			std::destroy_at(pInvoker);
			Allocator.Free(pInvoker);
		}

		// Type-erased invocation target.
		template <typename ReturnType, typename... ArgumentTypes>
		class InvokerBase
		{
		public:
			virtual ~InvokerBase() {}
			virtual InvokerBase* Clone(Allocator& Allocator) = 0;
			virtual ReturnType Invoke(ArgumentTypes... Arguments) = 0;
		};

		// Invokes a callable (function pointer, functor or lambda).
		template <typename FunctorType, typename ReturnType, typename... ArgumentTypes>
		class FunctorInvoker : public InvokerBase<ReturnType, ArgumentTypes...>
		{
		public:
			explicit FunctorInvoker(FunctorType Functor)
				: m_Functor(std::move(Functor))
			{
			}

			InvokerBase<ReturnType, ArgumentTypes...>* Clone(Allocator& Allocator) override
			{
				if constexpr (std::is_copy_constructible_v<FunctorType>)
				{
					return AllocateInvoker<FunctorInvoker>(Allocator, m_Functor);
				}
				else
				{
					DEBUG_BREAK();   // Move-only callables cannot be copied.
					return nullptr;
				}
			}

			ReturnType Invoke(ArgumentTypes... Arguments) override
			{
				return m_Functor(Arguments...);
			}

		private:
			FunctorType m_Functor;
		};

		// Invokes a member function on a borrowed object pointer.
		template <typename ObjectType, typename MemberFunctionType, typename ReturnType, typename... ArgumentTypes>
		class MemberFunctionInvoker : public InvokerBase<ReturnType, ArgumentTypes...>
		{
		public:
			MemberFunctionInvoker(ObjectType* pObject, MemberFunctionType MemberFunction)
				: m_pObject(pObject)
				, m_MemberFunction(MemberFunction)
			{
			}

			InvokerBase<ReturnType, ArgumentTypes...>* Clone(Allocator& Allocator) override
			{
				return AllocateInvoker<MemberFunctionInvoker>(Allocator, m_pObject, m_MemberFunction);
			}

			ReturnType Invoke(ArgumentTypes... Arguments) override
			{
				return (m_pObject->*m_MemberFunction)(Arguments...);
			}

		private:
			ObjectType* m_pObject;
			MemberFunctionType m_MemberFunction;
		};
	}

	// Primary template: only the function-signature specialization below is
	// usable. Instantiating Callback with anything else fails at compile time.
	template <typename Callable>
	class Callback
	{
		static_assert(CallbackDetail::bIsInvalidCallbackType<Callable>,
			"Callback must be instantiated with a function signature, e.g. Callback<int(double)>.");
	};

	// Specialization for function signature types.
	template <typename ReturnType, typename... ArgumentTypes>
	class Callback<ReturnType(ArgumentTypes...)>
	{
	public:
		using InvokerBaseType = CallbackDetail::InvokerBase<ReturnType, ArgumentTypes...>;

		// TARGET: a class/struct return type must derive from Object; void,
		// fundamental types, enums and pointers need no base class.
		static_assert(std::is_void_v<ReturnType> || std::is_fundamental_v<ReturnType> ||
			std::is_enum_v<ReturnType> || std::is_pointer_v<ReturnType> || std::is_base_of_v<Object, ReturnType>,
			"Callback: a class/struct return type must inherit from Vsp::Object; fundamental types (int, double, ...) need no base class.");

		// -------- Construction --------
		Callback() {}
		Callback(std::nullptr_t) {}

		// Binds a free/static function with the exact signature.
		Callback(ReturnType (*pFunctionPointer)(ArgumentTypes...))
		{
			m_pInvoker = CallbackDetail::AllocateInvoker<
				CallbackDetail::FunctorInvoker<ReturnType (*)(ArgumentTypes...), ReturnType, ArgumentTypes...>>(
				m_Allocator, pFunctionPointer);
		}

		// Binds a functor or lambda.
		template <typename FunctorType>
			requires (!std::is_same_v<std::remove_cvref_t<FunctorType>, Callback> &&
				std::is_invocable_r_v<ReturnType, FunctorType&, ArgumentTypes...>)
		explicit Callback(FunctorType&& Functor)
		{
			using StoredFunctorType = std::remove_cvref_t<FunctorType>;
			m_pInvoker = CallbackDetail::AllocateInvoker<
				CallbackDetail::FunctorInvoker<StoredFunctorType, ReturnType, ArgumentTypes...>>(
				m_Allocator, std::forward<FunctorType>(Functor));
		}

		// Binds a member function to a borrowed object pointer (the object must
		// outlive the callback). Works for both const and non-const members.
		template <typename ObjectType, typename MemberFunctionType>
			requires std::is_invocable_r_v<ReturnType, MemberFunctionType, ObjectType*, ArgumentTypes...>
		Callback(ObjectType* pObject, MemberFunctionType pMemberFunction)
		{
			m_pInvoker = CallbackDetail::AllocateInvoker<
				CallbackDetail::MemberFunctionInvoker<ObjectType, MemberFunctionType, ReturnType, ArgumentTypes...>>(
				m_Allocator, pObject, pMemberFunction);
		}

		Callback(const Callback& Other)
		{
			m_pInvoker = Other.m_pInvoker != nullptr ? Other.m_pInvoker->Clone(m_Allocator) : nullptr;
		}

		Callback(Callback&& Other)
			: m_Allocator(std::move(Other.m_Allocator))
			, m_pInvoker(Other.m_pInvoker)
		{
			Other.m_pInvoker = nullptr;
		}

		~Callback()
		{
			CallbackDetail::DeleteInvoker(m_Allocator, m_pInvoker);
		}

		// -------- Assignment --------
		Callback& operator=(const Callback& Other)
		{
			if (this != &Other)
			{
				CallbackDetail::DeleteInvoker(m_Allocator, m_pInvoker);
				m_pInvoker = Other.m_pInvoker != nullptr ? Other.m_pInvoker->Clone(m_Allocator) : nullptr;
			}
			return *this;
		}

		Callback& operator=(Callback&& Other)
		{
			if (this != &Other)
			{
				CallbackDetail::DeleteInvoker(m_Allocator, m_pInvoker);
				m_pInvoker = Other.m_pInvoker;
				m_Allocator = std::move(Other.m_Allocator);
				Other.m_pInvoker = nullptr;
			}
			return *this;
		}

		// Rebinds to a new functor or lambda, releasing the previous one.
		template <typename FunctorType>
			requires (!std::is_same_v<std::remove_cvref_t<FunctorType>, Callback> &&
				std::is_invocable_r_v<ReturnType, FunctorType&, ArgumentTypes...>)
		Callback& operator=(FunctorType&& Functor)
		{
			CallbackDetail::DeleteInvoker(m_Allocator, m_pInvoker);
			using StoredFunctorType = std::remove_cvref_t<FunctorType>;
			m_pInvoker = CallbackDetail::AllocateInvoker<
				CallbackDetail::FunctorInvoker<StoredFunctorType, ReturnType, ArgumentTypes...>>(
				m_Allocator, std::forward<FunctorType>(Functor));
			return *this;
		}

		// Unbinds the callback and releases the bound callable.
		Callback& operator=(std::nullptr_t)
		{
			return Unbind();
		}

		Callback& Unbind()
		{
			CallbackDetail::DeleteInvoker(m_Allocator, m_pInvoker);
			m_pInvoker = nullptr;
			return *this;
		}

		// -------- State --------
		bool IsBound() const
		{
			return m_pInvoker != nullptr;
		}

		explicit operator bool() const
		{
			return IsBound();
		}

		// -------- Invocation --------
		// Calls the bound callable. Invoking an unbound callback is misuse:
		// DEBUG_BREAK() fires and a default-constructed return value is
		// produced (the return type must be default-constructible).
		ReturnType Invoke(ArgumentTypes... Arguments)
		{
			if (m_pInvoker == nullptr)
			{
				DEBUG_BREAK();
				if constexpr (std::is_void_v<ReturnType>)
				{
					return;
				}
				return ReturnType();
			}
			return m_pInvoker->Invoke(Arguments...);
		}

		ReturnType operator()(ArgumentTypes... Arguments)
		{
			return Invoke(Arguments...);
		}

	private:
		// Invoker storage comes from this allocator (OS layout), mirroring how
		// ArrayList manages its buffers.
		Allocator m_Allocator;

		InvokerBaseType* m_pInvoker = nullptr;
	};
}
