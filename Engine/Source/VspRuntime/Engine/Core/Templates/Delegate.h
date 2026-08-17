#pragma once

#include <type_traits>
#include <utility>

#include "Engine/Core/Core.h"
#include "Engine/Core/Templates/ArrayList.h"
#include "Engine/Core/Templates/Callback.h"

namespace Vsp
{
	// =========================================================================
	// UnicastDelegate
	// =========================================================================

	namespace DelegateDetail
	{
		// Dependent-false helper so the primary templates' static_asserts only
		// fire when an invalid signature is actually instantiated.
		template <typename Type>
		inline constexpr bool bIsInvalidDelegateSignature = false;
	}

	// -------------------------------------------------------------------------
	// UnicastDelegate<ReturnType(ArgumentTypes...)>
	// -------------------------------------------------------------------------
	// Single-cast delegate: binds exactly one function at a time, and binding a
	// new function replaces the previous one. It supports a return value and is
	// built on Callback, so class/struct return types must derive from
	// Vsp::Object while fundamental types (int, double, ...) need no base class.
	//
	//     UnicastDelegate<int(int)> Delegate;
	//     Delegate.Bind([](int Value) { return Value * 2; });
	//     int Result = Delegate.Execute(21);
	//
	// Invoking an unbound delegate is misuse: DEBUG_BREAK() fires and a
	// default-constructed return value is produced. This class never uses C++
	// exceptions.
	// -------------------------------------------------------------------------

	// Primary template: only the function-signature specialization is usable.
	template <typename Signature>
	class UnicastDelegate
	{
		static_assert(DelegateDetail::bIsInvalidDelegateSignature<Signature>,
			"UnicastDelegate must be instantiated with a function signature, e.g. UnicastDelegate<int(double)>.");
	};

	template <typename ReturnType, typename... ArgumentTypes>
	class UnicastDelegate<ReturnType(ArgumentTypes...)>
	{
	public:
		using CallbackType = Callback<ReturnType(ArgumentTypes...)>;

		// -------- Construction --------
		UnicastDelegate() {}
		UnicastDelegate(std::nullptr_t) {}

		// Binds a free/static function with the exact signature.
		UnicastDelegate(ReturnType (*pFunctionPointer)(ArgumentTypes...))
		{
			Bind(pFunctionPointer);
		}

		// Binds a functor or lambda.
		template <typename FunctorType>
			requires (!std::is_same_v<std::remove_cvref_t<FunctorType>, UnicastDelegate> &&
				std::is_invocable_r_v<ReturnType, FunctorType&, ArgumentTypes...>)
		explicit UnicastDelegate(FunctorType&& Functor)
		{
			Bind(std::forward<FunctorType>(Functor));
		}

		// Binds a member function to a borrowed object pointer (the object must
		// outlive the delegate). Works for const and non-const members.
		template <typename ObjectType, typename MemberFunctionType>
			requires std::is_invocable_r_v<ReturnType, MemberFunctionType, ObjectType*, ArgumentTypes...>
		UnicastDelegate(ObjectType* pObject, MemberFunctionType pMemberFunction)
		{
			Bind(pObject, pMemberFunction);
		}

		// -------- Binding --------
		// A unicast delegate holds at most one target: binding replaces the
		// previously bound function.
		UnicastDelegate& Bind(ReturnType (*pFunctionPointer)(ArgumentTypes...))
		{
			m_Callback = pFunctionPointer;
			return *this;
		}

		template <typename FunctorType>
			requires (!std::is_same_v<std::remove_cvref_t<FunctorType>, UnicastDelegate> &&
				std::is_invocable_r_v<ReturnType, FunctorType&, ArgumentTypes...>)
		UnicastDelegate& Bind(FunctorType&& Functor)
		{
			m_Callback = std::forward<FunctorType>(Functor);
			return *this;
		}

		template <typename ObjectType, typename MemberFunctionType>
			requires std::is_invocable_r_v<ReturnType, MemberFunctionType, ObjectType*, ArgumentTypes...>
		UnicastDelegate& Bind(ObjectType* pObject, MemberFunctionType pMemberFunction)
		{
			CallbackType NewCallback(pObject, pMemberFunction);
			m_Callback = std::move(NewCallback);
			return *this;
		}

		// -------- State --------
		bool IsBound() const
		{
			return m_Callback.IsBound();
		}

		explicit operator bool() const
		{
			return IsBound();
		}

		UnicastDelegate& Unbind()
		{
			m_Callback.Unbind();
			return *this;
		}

		// -------- Execution --------
		ReturnType Execute(ArgumentTypes... Arguments)
		{
			return m_Callback.Invoke(Arguments...);
		}

		ReturnType operator()(ArgumentTypes... Arguments)
		{
			return Execute(Arguments...);
		}

	private:
		// The single bound target; Callback provides type erasure, the
		// allocator-backed invoker storage and the return-type constraint.
		CallbackType m_Callback;
	};

	// =========================================================================
	// MulticastDelegate
	// =========================================================================

	// -------------------------------------------------------------------------
	// MulticastDelegate<void(ArgumentTypes...)>
	// -------------------------------------------------------------------------
	// Multi-cast delegate: binds any number of functions; broadcasting executes
	// all of them in bind order. Multicast delegates have no return value, so
	// they are only instantiated with void signatures.
	//
	//     MulticastDelegate<void(int)> Delegate;
	//     Delegate.Add(&SomeFreeFunction);
	//     Delegate.Add([](int Value) { ... });
	//     Delegate.Add(&SomeObject, &ObjectType::OnValue);
	//     Delegate.Broadcast(42);
	//
	// The object of a member-function entry is borrowed, not owned: it must
	// outlive the delegate, and RemoveAll() should be used to unbind an object
	// before it dies. Entries must not be added or removed while a broadcast is
	// in progress. This class never uses C++ exceptions.
	// -------------------------------------------------------------------------

	// Primary template: only the void-signature specialization is usable.
	template <typename Signature>
	class MulticastDelegate
	{
		static_assert(DelegateDetail::bIsInvalidDelegateSignature<Signature>,
			"MulticastDelegate must be instantiated with a void function signature, e.g. MulticastDelegate<void(int)>.");
	};

	template <typename... ArgumentTypes>
	class MulticastDelegate<void(ArgumentTypes...)>
	{
	public:
		using SizeType = size_t;
		using CallbackType = Callback<void(ArgumentTypes...)>;

		// -------- Binding --------
		// Adds a free/static function with the exact signature.
		MulticastDelegate& Add(void (*pFunctionPointer)(ArgumentTypes...))
		{
			m_Entries.Emplace(CallbackType(pFunctionPointer), nullptr);
			return *this;
		}

		// Adds a functor or lambda.
		template <typename FunctorType>
			requires (!std::is_same_v<std::remove_cvref_t<FunctorType>, MulticastDelegate> &&
				std::is_invocable_r_v<void, FunctorType&, ArgumentTypes...>)
		MulticastDelegate& Add(FunctorType&& Functor)
		{
			m_Entries.Emplace(CallbackType(std::forward<FunctorType>(Functor)), nullptr);
			return *this;
		}

		// Adds a member function of a borrowed object; RemoveAll(Object) drops
		// every entry bound to that object.
		template <typename ObjectType, typename MemberFunctionType>
			requires std::is_invocable_r_v<void, MemberFunctionType, ObjectType*, ArgumentTypes...>
		MulticastDelegate& Add(ObjectType* pObject, MemberFunctionType pMemberFunction)
		{
			m_Entries.Emplace(CallbackType(pObject, pMemberFunction), static_cast<const void*>(pObject));
			return *this;
		}

		// Adds a deep copy of an existing unicast callback.
		MulticastDelegate& Add(const CallbackType& Callback)
		{
			m_Entries.Emplace(Callback, nullptr);
			return *this;
		}

		// -------- Execution --------
		// Runs every bound function in bind order.
		void Broadcast(ArgumentTypes... Arguments)
		{
			for (SizeType Index = 0; Index < m_Entries.GetSize(); ++Index)
			{
				m_Entries[Index].Callback.Invoke(Arguments...);
			}
		}

		// Alias of Broadcast().
		void Execute(ArgumentTypes... Arguments)
		{
			Broadcast(Arguments...);
		}

		void operator()(ArgumentTypes... Arguments)
		{
			Broadcast(Arguments...);
		}

		// -------- Removal --------
		// Removes the entry at Index (out-of-range indices trigger
		// DEBUG_BREAK() inside ArrayList and are ignored).
		void RemoveAt(SizeType Index)
		{
			m_Entries.RemoveAt(Index);
		}

		// Removes every entry bound to pBoundObject. Passing nullptr removes
		// the free-function and lambda entries, which carry no bound object.
		void RemoveAll(const void* pBoundObject)
		{
			for (SizeType Index = m_Entries.GetSize(); Index > 0; --Index)
			{
				if (m_Entries[Index - 1].pBoundObject == pBoundObject)
				{
					m_Entries.RemoveAt(Index - 1);
				}
			}
		}

		void Clear()
		{
			m_Entries.Clear();
		}

		// -------- State --------
		SizeType GetCallbackCount() const
		{
			return m_Entries.GetSize();
		}

		bool IsEmpty() const
		{
			return m_Entries.IsEmpty();
		}

	private:
		// One bound function plus the object it was bound to (nullptr for free
		// functions and lambdas), so RemoveAll() can unbind by object.
		struct CallbackEntry
		{
			CallbackEntry(CallbackType InCallback, const void* InBoundObject)
				: Callback(std::move(InCallback))
				, pBoundObject(InBoundObject)
			{
			}

			CallbackType Callback;
			const void* pBoundObject;
		};

		ArrayList<CallbackEntry> m_Entries;
	};

#define FUNC_DECLARE_DELEGATE(DelegateName, ReturnType, ...) \
	using DelegateName = UnicastDelegate<ReturnType(__VA_ARGS__)>

#define FUNC_DECLARE_MULTICAST_DELEGATE(MulticastDelegateName, ReturnType, ...)\
	using DelegateName = MulticastDelegate<ReturnType(__VA_ARGS__)>

}
