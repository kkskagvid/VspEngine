using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;

namespace VspEngine
{
	/// <summary>
	/// Managed side of the C++ &lt;-&gt; C# interop bridge.
	/// Every entry point is a static method marked with UnmanagedCallersOnly so
	/// the C++ host can fetch a raw function pointer for it through hostfxr
	/// (CoreCLR Hosting API).
	///
	/// Ownership model:
	///   - The C++ host GENERATES the non-negative InstanceID and passes it
	///     into CreateInstance.
	///   - The created GameObject holds its own IntPtr (NativePtr, the
	///     object's GCHandle) and the host passes that pointer back on every
	///     lifecycle call. No instances Dictionary is maintained here.
	/// </summary>
	public static class NativeBridge
	{
		private static List<Type>? cachedScriptTypes;

		private static List<Type> GetScriptTypes()
		{
			if (cachedScriptTypes == null)
			{
				Type baseType = typeof(ScriptBehaviour);
				cachedScriptTypes = Assembly.GetExecutingAssembly()
					.GetTypes()
					.Where(type => type.IsClass && !type.IsAbstract && baseType.IsAssignableFrom(type))
					.ToList();
			}
			return cachedScriptTypes;
		}

		/// <summary>Number of concrete ScriptBehaviour types in the assembly.</summary>
		[UnmanagedCallersOnly(EntryPoint = "GetScriptTypeCount")]
		public static int GetScriptTypeCount()
		{
			return GetScriptTypes().Count;
		}

		/// <summary>
		/// Writes the assembly-qualified name of the script type at the given
		/// index into the buffer as UTF-16 (null terminated). Silently writes
		/// an empty string when the index or the buffer is invalid.
		/// </summary>
		[UnmanagedCallersOnly(EntryPoint = "GetScriptTypeName")]
		public static void GetScriptTypeName(int typeIndex, IntPtr buffer, int bufferCapacity)
		{
			if (buffer == IntPtr.Zero || bufferCapacity <= 0)
				return;

			List<Type> scriptTypes = GetScriptTypes();
			string? typeName = (typeIndex >= 0 && typeIndex < scriptTypes.Count)
				? scriptTypes[typeIndex].AssemblyQualifiedName
				: null;

			if (string.IsNullOrEmpty(typeName))
			{
				Marshal.WriteInt16(buffer, 0, 0);
				return;
			}

			int unitCount = Math.Min(typeName.Length, bufferCapacity - 1);
			for (int unitIndex = 0; unitIndex < unitCount; unitIndex++)
			{
				Marshal.WriteInt16(buffer, unitIndex * 2, typeName[unitIndex]);
			}
			Marshal.WriteInt16(buffer, unitCount * 2, 0);
		}

		/// <summary>
		/// Creates a managed instance of the given script type. The InstanceID
		/// is assigned by the C++ host; the returned handle is the instance's
		/// IntPtr (GCHandle) used for all later lifecycle calls.
		/// </summary>
		[UnmanagedCallersOnly(EntryPoint = "CreateInstance")]
		public static IntPtr CreateInstance(int instanceId, IntPtr typeNamePtr)
		{
			string? typeName = Marshal.PtrToStringUni(typeNamePtr);
			if (string.IsNullOrEmpty(typeName))
				return IntPtr.Zero;

			Type? type = Type.GetType(typeName);
			if (type == null || !typeof(ScriptBehaviour).IsAssignableFrom(type))
				return IntPtr.Zero;

			ScriptBehaviour? instance = Activator.CreateInstance(type) as ScriptBehaviour;
			if (instance == null)
				return IntPtr.Zero;

			instance.InstanceID = (uint)instanceId;

			// Pin the instance and hand its GCHandle back to the host. The
			// handle keeps the object alive until DestroyInstance frees it.
			GCHandle handle = GCHandle.Alloc(instance);
			instance.NativePtr = GCHandle.ToIntPtr(handle);
			return instance.NativePtr;
		}

		/// <summary>Invokes OnInit on the script instance addressed by its IntPtr.</summary>
		[UnmanagedCallersOnly(EntryPoint = "CallOnInit")]
		public static void CallOnInit(IntPtr handlePtr)
		{
			GetInstanceFromHandle(handlePtr)?.OnInit();
		}

		/// <summary>Invokes OnStart on the script instance addressed by its IntPtr.</summary>
		[UnmanagedCallersOnly(EntryPoint = "CallOnStart")]
		public static void CallOnStart(IntPtr handlePtr)
		{
			GetInstanceFromHandle(handlePtr)?.OnStart();
		}

		/// <summary>Invokes OnUpdate on the script instance addressed by its IntPtr.</summary>
		[UnmanagedCallersOnly(EntryPoint = "CallOnUpdate")]
		public static void CallOnUpdate(IntPtr handlePtr)
		{
			GetInstanceFromHandle(handlePtr)?.OnUpdate();
		}

		/// <summary>
		/// Invokes OnDestroy on the script instance and releases its GCHandle,
		/// which drops the last managed reference to the instance.
		/// </summary>
		[UnmanagedCallersOnly(EntryPoint = "DestroyInstance")]
		public static void DestroyInstance(IntPtr handlePtr)
		{
			if (handlePtr == IntPtr.Zero)
				return;

			GCHandle handle = GCHandle.FromIntPtr(handlePtr);
			if (handle.Target is ScriptBehaviour instance)
			{
				instance.OnDestroy();
			}
			handle.Free();
		}

		private static ScriptBehaviour? GetInstanceFromHandle(IntPtr handlePtr)
		{
			if (handlePtr == IntPtr.Zero)
				return null;

			GCHandle handle = GCHandle.FromIntPtr(handlePtr);
			return handle.Target as ScriptBehaviour;
		}
	}
}
