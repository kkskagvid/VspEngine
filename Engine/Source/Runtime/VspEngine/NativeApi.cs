using System;
using System.Runtime.InteropServices;

namespace VspEngine
{
	/// <summary>
	/// Raw P/Invoke bindings against the native engine core (VspCore.dll).
	/// These functions form the C# -&gt; C++ direction of the interop bridge.
	/// The C++ host calls back into managed code through NativeBridge.
	///
	/// The engine's core classes are reference handles, so the scene half of
	/// this file is what actually implements their properties; the graphics half
	/// (VspRhi_*) lives in Rendering/RhiApi.cs.
	/// </summary>
	internal static class NativeApi
	{
		private const string LibraryName = "VspCore";

		// ---- Input (keyboard) ----
		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspInput_IsKeyDown(int keyCode);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspInput_WasKeyPressed(int keyCode);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspInput_WasKeyReleased(int keyCode);

		// ---- Input (mouse) ----
		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspInput_IsMouseButtonDown(int button);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspInput_WasMouseButtonPressed(int button);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspInput_WasMouseButtonReleased(int button);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern float VspInput_GetMousePositionX();

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern float VspInput_GetMousePositionY();

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern float VspInput_GetMouseDeltaX();

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern float VspInput_GetMouseDeltaY();

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern float VspInput_GetScrollX();

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern float VspInput_GetScrollY();

		// ---- Time ----
		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern float VspTime_GetDeltaTime();

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern float VspTime_GetElapsedTime();

		// ---- Scene objects (the data behind Object / GameObject / Transform / Component) ----
		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspObject_IsValid(uint handle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspObject_GetName(uint handle, byte[] bufferUtf8, int bufferCapacityBytes);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspObject_SetName(uint handle, [MarshalAs(UnmanagedType.LPUTF8Str)] string nameUtf8);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern uint VspGameObject_Create([MarshalAs(UnmanagedType.LPUTF8Str)] string nameUtf8);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspGameObject_Destroy(uint gameObjectHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern uint VspGameObject_GetTransform(uint gameObjectHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspGameObject_GetActiveSelf(uint gameObjectHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspGameObject_SetActiveSelf(uint gameObjectHandle, int isActive);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspGameObject_GetLayer(uint gameObjectHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspGameObject_SetLayer(uint gameObjectHandle, int layer);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern uint VspTransform_GetOwnerGameObject(uint transformHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspTransform_GetLocalPosition(uint transformHandle, [Out] float[] outXyz);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspTransform_SetLocalPosition(uint transformHandle, float positionX, float positionY, float positionZ);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspTransform_GetLocalRotation(uint transformHandle, [Out] float[] outXyz);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspTransform_SetLocalRotation(uint transformHandle, float rotationX, float rotationY, float rotationZ);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspTransform_GetLocalScale(uint transformHandle, [Out] float[] outXyz);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspTransform_SetLocalScale(uint transformHandle, float scaleX, float scaleY, float scaleZ);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspTransform_IsDirty(uint transformHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspTransform_ClearDirtyFlag(uint transformHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern uint VspComponent_GetOwnerGameObject(uint componentHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspComponent_IsEnabled(uint componentHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspComponent_SetEnabled(uint componentHandle, int isEnabled);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspComponent_IsRenderable(uint componentHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspComponent_SetRenderable(uint componentHandle, int isRenderable);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern uint VspComponent_GetRenderableCount();

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern uint VspComponent_GetRenderableHandle(uint renderableIndex);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern uint VspScene_GetLiveGameObjectCount();

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern uint VspScene_GetLiveTransformCount();

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern uint VspScene_GetLiveComponentCount();

		// ---- Shader assets (what HLSLCC compiled) ----
		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern uint VspShaderAsset_Load(
			[MarshalAs(UnmanagedType.LPUTF8Str)] string shaderNameUtf8,
			byte[] errorBufferUtf8,
			int errorBufferCapacityBytes);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspShaderLibrary_SetShaderDirectory(
			[MarshalAs(UnmanagedType.LPUTF8Str)] string? directoryUtf8);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspShaderAsset_GetShaderName(uint shaderHandle, byte[] bufferUtf8, int bufferCapacityBytes);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspShaderAsset_GetRenderQueue(uint shaderHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspShaderAsset_GetVariantCount(uint shaderHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspShaderAsset_GetVariantKey(uint shaderHandle, uint variantIndex, byte[] bufferUtf8, int bufferCapacityBytes);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspShaderAsset_GetCompiledVariantIndex(uint shaderHandle, uint variantIndex);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspShaderAsset_GetEntryPointName(uint shaderHandle, uint variantIndex, int stage, byte[] bufferUtf8, int bufferCapacityBytes);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspShaderAsset_GetStageSpirvByteCount(uint shaderHandle, uint variantIndex, int stage);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspShaderAsset_CopyStageSpirv(uint shaderHandle, uint variantIndex, int stage, [Out] byte[] buffer, uint bufferCapacity);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspShaderAsset_GetStageReflectionSummary(uint shaderHandle, uint variantIndex, int stage, [Out] uint[] outValues);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspShaderAsset_GetPropertyCount(uint shaderHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspShaderAsset_GetProperty(
			uint shaderHandle,
			uint propertyIndex,
			byte[] nameBufferUtf8,
			int nameBufferCapacityBytes,
			byte[] displayNameBufferUtf8,
			int displayNameBufferCapacityBytes,
			[Out] int[] outType,
			[Out] float[] outDefaults4);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspShaderAsset_GetKeywordGroupCount(uint shaderHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspShaderAsset_GetKeywordGroup(
			uint shaderHandle,
			uint groupIndex,
			byte[] nameBufferUtf8,
			int nameBufferCapacityBytes,
			[Out] int[] outKind,
			[Out] int[] outStrippable);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspShaderAsset_GetKeywordGroupState(uint shaderHandle, uint groupIndex, uint stateIndex, byte[] bufferUtf8, int bufferCapacityBytes);

		// ---- Materials ----
		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern uint VspMaterial_Create(uint shaderHandle, byte[] errorBufferUtf8, int errorBufferCapacityBytes);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspMaterial_Destroy(uint materialHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern uint VspMaterial_GetShader(uint materialHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspMaterial_SetShader(uint materialHandle, uint shaderHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspMaterial_GetPropertyCount(uint materialHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspMaterial_GetPropertyName(uint materialHandle, uint valueIndex, byte[] bufferUtf8, int bufferCapacityBytes);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspMaterial_SetFloat(uint materialHandle, [MarshalAs(UnmanagedType.LPUTF8Str)] string propertyNameUtf8, float value);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspMaterial_GetFloat(uint materialHandle, [MarshalAs(UnmanagedType.LPUTF8Str)] string propertyNameUtf8, [Out] float[] outValue);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspMaterial_SetVector(uint materialHandle, [MarshalAs(UnmanagedType.LPUTF8Str)] string propertyNameUtf8, float valueX, float valueY, float valueZ, float valueW);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspMaterial_GetVector(uint materialHandle, [MarshalAs(UnmanagedType.LPUTF8Str)] string propertyNameUtf8, [Out] float[] outValues4);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspMaterial_SetKeywordEnabled(uint materialHandle, [MarshalAs(UnmanagedType.LPUTF8Str)] string keywordUtf8, int isEnabled);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspMaterial_IsKeywordEnabled(uint materialHandle, [MarshalAs(UnmanagedType.LPUTF8Str)] string keywordUtf8);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspMaterial_ResolveVariantIndex(uint materialHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern uint VspComponent_GetMaterial(uint componentHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspComponent_SetMaterial(uint componentHandle, uint materialHandle);

		// ---- Engine paths ----
		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspPlatform_GetExecutableDirectoryUtf8(byte[] bufferUtf8, int bufferCapacityBytes);

		// ---- Logging ----
		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspLog_Message([MarshalAs(UnmanagedType.LPUTF8Str)] string message);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspLog_Debug([MarshalAs(UnmanagedType.LPUTF8Str)] string message);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspLog_Info([MarshalAs(UnmanagedType.LPUTF8Str)] string message);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspLog_Warning([MarshalAs(UnmanagedType.LPUTF8Str)] string message);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspLog_Error([MarshalAs(UnmanagedType.LPUTF8Str)] string message);
	}
}