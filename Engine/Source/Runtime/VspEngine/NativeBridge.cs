using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Runtime.Loader;

using VspEngine.Rendering;

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
	///   - Creating an instance also creates its native scene objects (a
	///     GameObject with its Transform plus the script Component); the host
	///     hands those handles over as well, and the created ScriptBehaviour
	///     stores nothing but the handles. Lifecycle calls pass the object's
	///     GCHandle back, so no instances Dictionary is maintained here.
	///
	/// Assembly model:
	///   - This class lives in VspEngine.dll (the engine's managed runtime).
	///   - LoadGameAssembly loads the game Assembly (Assembly.dll) that holds
	///     the user scripts; type enumeration and instantiation operate on it.
	///   - Exceptions never cross the interop boundary: every entry point
	///     contains its failures and reports them through its return value.
	/// </summary>
	public static class NativeBridge
	{
		private static Assembly? cachedGameAssembly;
		private static List<Type>? cachedScriptTypes;
		private static IGameModule? gameModule;

		// -----------------------------------------------------------------
		// Native entry-point delegates. The C++ host calls these through
		// raw function pointers; the delegates below keep the thunks (and
		// therefore the pointers) alive for the whole process lifetime.
		// -----------------------------------------------------------------

		[UnmanagedFunctionPointer(CallingConvention.StdCall)]
		private delegate IntPtr CreateInstanceDelegate(int instanceId, uint gameObjectHandle, uint transformHandle, uint componentHandle, IntPtr typeNamePtr);

		[UnmanagedFunctionPointer(CallingConvention.StdCall)]
		private delegate int GetScriptTypeCountDelegate();

		[UnmanagedFunctionPointer(CallingConvention.StdCall)]
		private delegate void GetScriptTypeNameDelegate(int typeIndex, IntPtr buffer, int bufferCapacity);

		[UnmanagedFunctionPointer(CallingConvention.StdCall)]
		private delegate void CallLifecycleDelegate(IntPtr handlePtr);

		[UnmanagedFunctionPointer(CallingConvention.StdCall)]
		private delegate void CallRenderFlowDelegate();

		[UnmanagedFunctionPointer(CallingConvention.StdCall)]
		private delegate int LoadGameAssemblyDelegate(IntPtr assemblyPathPtr);

		[UnmanagedFunctionPointer(CallingConvention.StdCall)]
		private delegate void ReleaseRenderPipelineDelegate();

		private static readonly CreateInstanceDelegate s_CreateInstanceDelegate = CreateInstance;
		private static readonly GetScriptTypeCountDelegate s_GetScriptTypeCountDelegate = GetScriptTypeCount;
		private static readonly GetScriptTypeNameDelegate s_GetScriptTypeNameDelegate = GetScriptTypeName;
		private static readonly CallLifecycleDelegate s_CallOnInitDelegate = CallOnInit;
		private static readonly CallLifecycleDelegate s_CallOnStartDelegate = CallOnStart;
		private static readonly CallLifecycleDelegate s_CallOnUpdateDelegate = CallOnUpdate;
		private static readonly CallLifecycleDelegate s_DestroyInstanceDelegate = DestroyInstance;
		private static readonly CallRenderFlowDelegate s_CallRenderFlowDelegate = CallRenderFlow;
		private static readonly LoadGameAssemblyDelegate s_LoadGameAssemblyDelegate = LoadGameAssembly;
		private static readonly ReleaseRenderPipelineDelegate s_ReleaseRenderPipelineDelegate = ReleaseRenderPipeline;

		private static Assembly? GetGameAssembly()
		{
			return cachedGameAssembly;
		}

		private static List<Type> GetScriptTypes()
		{
			if (cachedScriptTypes == null)
			{
				cachedScriptTypes = new List<Type>();

				Assembly? gameAssembly = GetGameAssembly();
				if (gameAssembly != null)
				{
					Type baseType = typeof(ScriptBehaviour);
					cachedScriptTypes = gameAssembly
						.GetTypes()
						.Where(type => type.IsClass && !type.IsAbstract && baseType.IsAssignableFrom(type))
						.ToList();
				}
			}
			return cachedScriptTypes;
		}

		/// <summary>
		/// Fills the native entry-point table with pointers to every bridge
		/// entry point, all taken from THIS (the single loaded) copy of
		/// VspEngine.dll. Slot order (mirrored in ScriptEngine.cpp):
		///   0 CreateInstance, 1 GetScriptTypeCount, 2 GetScriptTypeName,
		///   3 CallOnInit, 4 CallOnStart, 5 CallOnUpdate, 6 DestroyInstance,
		///   7 CallRenderFlow, 8 LoadGameAssembly, 9 ReleaseRenderPipeline.
		/// </summary>
		[UnmanagedCallersOnly(EntryPoint = "GetBridgeFunctionTable")]
		public static void GetBridgeFunctionTable(IntPtr tablePtr)
		{
			if (tablePtr == IntPtr.Zero)
				return;

			int slotIndex = 0;
			Marshal.WriteIntPtr(tablePtr, (slotIndex++) * IntPtr.Size, Marshal.GetFunctionPointerForDelegate(s_CreateInstanceDelegate));
			Marshal.WriteIntPtr(tablePtr, (slotIndex++) * IntPtr.Size, Marshal.GetFunctionPointerForDelegate(s_GetScriptTypeCountDelegate));
			Marshal.WriteIntPtr(tablePtr, (slotIndex++) * IntPtr.Size, Marshal.GetFunctionPointerForDelegate(s_GetScriptTypeNameDelegate));
			Marshal.WriteIntPtr(tablePtr, (slotIndex++) * IntPtr.Size, Marshal.GetFunctionPointerForDelegate(s_CallOnInitDelegate));
			Marshal.WriteIntPtr(tablePtr, (slotIndex++) * IntPtr.Size, Marshal.GetFunctionPointerForDelegate(s_CallOnStartDelegate));
			Marshal.WriteIntPtr(tablePtr, (slotIndex++) * IntPtr.Size, Marshal.GetFunctionPointerForDelegate(s_CallOnUpdateDelegate));
			Marshal.WriteIntPtr(tablePtr, (slotIndex++) * IntPtr.Size, Marshal.GetFunctionPointerForDelegate(s_DestroyInstanceDelegate));
			Marshal.WriteIntPtr(tablePtr, (slotIndex++) * IntPtr.Size, Marshal.GetFunctionPointerForDelegate(s_CallRenderFlowDelegate));
			Marshal.WriteIntPtr(tablePtr, (slotIndex++) * IntPtr.Size, Marshal.GetFunctionPointerForDelegate(s_LoadGameAssemblyDelegate));
			Marshal.WriteIntPtr(tablePtr, (slotIndex++) * IntPtr.Size, Marshal.GetFunctionPointerForDelegate(s_ReleaseRenderPipelineDelegate));
		}

		/// <summary>
		/// Loads the game Assembly (the assembly holding the user scripts)
		/// into THIS assembly's own load context - the context that holds the
		/// single VspEngine.dll copy - and refreshes the script-type cache.
		/// Loading it anywhere else would resolve a second copy of VspEngine
		/// whose ScriptBehaviour type would not match the bridge's. Returns 1
		/// on success, 0 on failure (exceptions are contained here).
		/// </summary>
		public static int LoadGameAssembly(IntPtr assemblyPathPtr)
		{
			string? assemblyPath = Marshal.PtrToStringUni(assemblyPathPtr);
			if (string.IsNullOrEmpty(assemblyPath) || !System.IO.File.Exists(assemblyPath))
				return 0;

			try
			{
				AssemblyLoadContext? bridgeContext =
					AssemblyLoadContext.GetLoadContext(Assembly.GetExecutingAssembly());
				if (bridgeContext == null)
					return 0;

				cachedGameAssembly = bridgeContext.LoadFromAssemblyPath(assemblyPath);
				cachedScriptTypes = null;
				gameModule = null;

				if (cachedGameAssembly == null)
				{
					return 0;
				}

				// The game's entry point: it installs the render pipeline and loads
				// the shaders the engine will never load by itself.
				LoadGameModule(cachedGameAssembly);
				return 1;
			}
			catch
			{
				// Loading failures are contained here; the host reports them.
				cachedGameAssembly = null;
				cachedScriptTypes = null;
				return 0;
			}
		}

		/// <summary>
		/// Finds the game module - the single concrete IGameModule of the game
		/// assembly - and lets it start the game. A game without one is legal: it
		/// simply renders nothing until a script installs a pipeline.
		/// </summary>
		private static void LoadGameModule(Assembly gameAssembly)
		{
			Type? moduleType = null;
			foreach (Type candidateType in gameAssembly.GetTypes())
			{
				if (candidateType.IsClass && !candidateType.IsAbstract &&
					typeof(IGameModule).IsAssignableFrom(candidateType))
				{
					if (moduleType != null)
					{
						NativeApi.VspLog_Error("GameModule: '" + moduleType.FullName + "' and '" +
							candidateType.FullName + "' both implement IGameModule; using the first.");
						break;
					}
					moduleType = candidateType;
				}
			}

			if (moduleType == null)
			{
				return;
			}

			try
			{
				gameModule = Activator.CreateInstance(moduleType) as IGameModule;
				gameModule?.OnGameLoad();
			}
			catch (Exception exception)
			{
				// A game that cannot start must not tear the process down: the
				// host keeps running and the log says what happened.
				gameModule = null;
				NativeApi.VspLog_Error("GameModule: '" + moduleType.FullName + "' failed to load: " + exception.Message);
			}
		}

		/// <summary>
		/// Lets the game module release what it created. Called before the
		/// engine shuts the graphics backend down.
		/// </summary>
		private static void UnloadGameModule()
		{
			IGameModule? module = gameModule;
			gameModule = null;

			if (module == null)
			{
				return;
			}

			try
			{
				module.OnGameUnload();
			}
			catch (Exception exception)
			{
				NativeApi.VspLog_Error("GameModule: OnGameUnload failed: " + exception.Message);
			}
		}

		/// <summary>Number of concrete ScriptBehaviour types in the game Assembly.</summary>
		public static int GetScriptTypeCount()
		{
			return GetScriptTypes().Count;
		}

		/// <summary>
		/// Writes the full name of the script type at the given index into the
		/// buffer as UTF-16 (null terminated). Silently writes an empty string
		/// when the index or the buffer is invalid.
		/// </summary>
		public static void GetScriptTypeName(int typeIndex, IntPtr buffer, int bufferCapacity)
		{
			if (buffer == IntPtr.Zero || bufferCapacity <= 0)
				return;

			List<Type> scriptTypes = GetScriptTypes();
			string? typeName = (typeIndex >= 0 && typeIndex < scriptTypes.Count)
				? scriptTypes[typeIndex].FullName
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
		/// Creates a managed instance of the given script type (looked up in
		/// the game Assembly). The InstanceID and the handles of the native
		/// scene objects the script is attached to are all assigned by the C++
		/// host; the returned handle is the instance's IntPtr (GCHandle) used
		/// for all later lifecycle calls.
		/// </summary>
		public static IntPtr CreateInstance(
			int instanceId,
			uint gameObjectHandle,
			uint transformHandle,
			uint componentHandle,
			IntPtr typeNamePtr)
		{
			try
			{
				Assembly? gameAssembly = GetGameAssembly();
				string? typeName = Marshal.PtrToStringUni(typeNamePtr);
				if (gameAssembly == null || string.IsNullOrEmpty(typeName))
					return IntPtr.Zero;

				Type? type = gameAssembly.GetType(typeName);
				if (type == null || !typeof(ScriptBehaviour).IsAssignableFrom(type))
					return IntPtr.Zero;

				ScriptBehaviour? instance = Activator.CreateInstance(type) as ScriptBehaviour;
				if (instance == null)
					return IntPtr.Zero;

				// A managed engine object is a reference handle: it stores the
				// native handles and forwards everything else to them.
				instance.InstanceID = (uint)instanceId;
				instance.NativeHandle = componentHandle;

				// Pin the instance and hand its GCHandle back to the host. The
				// handle keeps the object alive until DestroyInstance frees it.
				GCHandle handle = GCHandle.Alloc(instance);
				instance.NativePtr = GCHandle.ToIntPtr(handle);
				instance.OnHandlesAssigned(gameObjectHandle, transformHandle);
				return instance.NativePtr;
			}
			catch
			{
				// Instance creation failures are contained here; the host sees
				// a null handle and reports the failed type.
				return IntPtr.Zero;
			}
		}

		/// <summary>Invokes OnInit on the script instance addressed by its IntPtr.</summary>
		public static void CallOnInit(IntPtr handlePtr)
		{
			GetInstanceFromHandle(handlePtr)?.OnInit();
		}

		/// <summary>Invokes OnStart on the script instance addressed by its IntPtr.</summary>
		public static void CallOnStart(IntPtr handlePtr)
		{
			GetInstanceFromHandle(handlePtr)?.OnStart();
		}

		/// <summary>Invokes OnUpdate on the script instance addressed by its IntPtr.</summary>
		public static void CallOnUpdate(IntPtr handlePtr)
		{
			GetInstanceFromHandle(handlePtr)?.OnUpdate();
		}

		/// <summary>
		/// Invokes OnDestroy on the script instance and releases its GCHandle,
		/// which drops the last managed reference to the instance.
		/// </summary>
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

		/// <summary>
		/// Runs the managed render pipeline (RenderPipelineManager) for one
		/// frame. The pipeline gathers its draw list from the native scene and
		/// records every graphics command through the wrapped graphics API.
		/// </summary>
		public static void CallRenderFlow()
		{
			try
			{
				RenderFlow.Execute();
			}
			catch (Exception exception)
			{
				// A broken render pipeline must not tear the process down: report
				// the failure and keep the last submitted frame state.
				NativeApi.VspLog_Error("RenderFlow: " + exception.Message);
			}
		}

		/// <summary>
		/// Lets the managed render pipeline release the graphics resources it
		/// created. The host calls it before the graphics backend goes away.
		/// </summary>
		public static void ReleaseRenderPipeline()
		{
			try
			{
				RenderFlow.Release();
				UnloadGameModule();
			}
			catch (Exception exception)
			{
				NativeApi.VspLog_Error("RenderFlow: " + exception.Message);
			}
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
