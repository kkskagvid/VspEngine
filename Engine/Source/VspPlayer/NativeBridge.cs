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
	/// (CoreCLR Hosting API). Script instances live in a dictionary keyed by a
	/// non-negative integer InstanceID (0 = invalid, ids start at 1).
	/// </summary>
	public static class NativeBridge
	{
		private static readonly Dictionary<int, ScriptBehaviour> instances = new Dictionary<int, ScriptBehaviour>();
		private static readonly object lockObject = new object();
		private static int nextInstanceId = 1; // 1-based; 0 means invalid

		/// <summary>Creates a managed instance of the given script type and returns its InstanceID.</summary>
		[UnmanagedCallersOnly(EntryPoint = "CreateInstance")]
		public static int CreateInstance(IntPtr typeNamePtr)
		{
			string? typeName = Marshal.PtrToStringUni(typeNamePtr);
			return CreateInstanceCore(typeName);
		}

		private static int CreateInstanceCore(string? typeName)
		{
			if (string.IsNullOrEmpty(typeName))
				return 0;

			Type? type = Type.GetType(typeName);
			if (type == null || !typeof(ScriptBehaviour).IsAssignableFrom(type))
				return 0;

			ScriptBehaviour? instance = Activator.CreateInstance(type) as ScriptBehaviour;
			if (instance == null)
				return 0;

			lock (lockObject)
			{
				int id = nextInstanceId++;
				instances[id] = instance;
				instance.InstanceID = (uint)id;
				return id;
			}
		}

		/// <summary>
		/// Creates one instance of every concrete ScriptBehaviour type in the
		/// assembly and returns an unmanaged int array of the assigned ids.
		/// The array must be released with FreeIntArray.
		/// </summary>
		[UnmanagedCallersOnly(EntryPoint = "CreateAllScripts")]
		public static unsafe int* CreateAllScripts(int* outCount)
		{
			*outCount = 0;

			Type baseType = typeof(ScriptBehaviour);
			List<Type> scriptTypes = Assembly.GetExecutingAssembly()
				.GetTypes()
				.Where(type => type.IsClass && !type.IsAbstract && baseType.IsAssignableFrom(type))
				.ToList();

			List<int> ids = new List<int>();
			foreach (Type type in scriptTypes)
			{
				int id = CreateInstanceCore(type.AssemblyQualifiedName);
				if (id != 0)
					ids.Add(id);
			}

			*outCount = ids.Count;
			if (ids.Count == 0)
				return null;

			int* result = (int*)Marshal.AllocHGlobal(ids.Count * sizeof(int)).ToPointer();
			for (int i = 0; i < ids.Count; i++)
				result[i] = ids[i];
			return result;
		}

		/// <summary>Releases an array previously returned by CreateAllScripts.</summary>
		[UnmanagedCallersOnly(EntryPoint = "FreeIntArray")]
		public static void FreeIntArray(IntPtr ptr)
		{
			if (ptr != IntPtr.Zero)
				Marshal.FreeHGlobal(ptr);
		}

		/// <summary>Invokes OnInit on the script instance.</summary>
		[UnmanagedCallersOnly(EntryPoint = "CallOnInit")]
		public static void CallOnInit(int id)
		{
			GetInstance(id)?.OnInit();
		}

		/// <summary>Invokes OnStart on the script instance.</summary>
		[UnmanagedCallersOnly(EntryPoint = "CallOnStart")]
		public static void CallOnStart(int id)
		{
			GetInstance(id)?.OnStart();
		}

		/// <summary>Invokes OnUpdate on the script instance.</summary>
		[UnmanagedCallersOnly(EntryPoint = "CallOnUpdate")]
		public static void CallOnUpdate(int id)
		{
			GetInstance(id)?.OnUpdate();
		}

		/// <summary>Invokes OnDestroy on the script instance and removes it from the registry.</summary>
		[UnmanagedCallersOnly(EntryPoint = "DestroyInstance")]
		public static void DestroyInstance(int id)
		{
			lock (lockObject)
			{
				if (instances.TryGetValue(id, out ScriptBehaviour? instance))
				{
					instances.Remove(id);
					instance.OnDestroy();
				}
			}
		}

		private static ScriptBehaviour? GetInstance(int id)
		{
			lock (lockObject)
			{
				instances.TryGetValue(id, out ScriptBehaviour? instance);
				return instance;
			}
		}
	}
}
