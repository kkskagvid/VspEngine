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

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspInput_GetCursorMode();

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspInput_SetCursorMode(int cursorMode);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspInput_IsCursorLocked();

		// ---- Time ----
		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern float VspTime_GetDeltaTime();

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern float VspTime_GetUnscaledDeltaTime();

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern float VspTime_GetElapsedTime();

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern float VspTime_GetUnscaledElapsedTime();

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern long VspTime_GetFrameCount();

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern float VspTime_GetFramesPerSecond();

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern float VspTime_GetTimeScale();

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspTime_SetTimeScale(float timeScale);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspTime_IsFixedTimeStep();

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern float VspTime_GetFixedDeltaTime();

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
		internal static extern void VspTransform_GetWorldPosition(uint transformHandle, [Out] float[] outXyz);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspTransform_SetWorldPosition(uint transformHandle, float positionX, float positionY, float positionZ);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspTransform_GetWorldRotation(uint transformHandle, [Out] float[] outXyz);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspTransform_GetWorldScale(uint transformHandle, [Out] float[] outXyz);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspTransform_GetLocalMatrix(uint transformHandle, [Out] float[] outMatrix16);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspTransform_GetWorldMatrix(uint transformHandle, [Out] float[] outMatrix16);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspTransform_GetWorldToLocalMatrix(uint transformHandle, [Out] float[] outMatrix16);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern uint VspTransform_GetParent(uint transformHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspTransform_SetParent(uint transformHandle, uint parentTransformHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspTransform_GetChildCount(uint transformHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern uint VspTransform_GetChild(uint transformHandle, int childIndex);

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
		internal static extern uint VspScene_GetGameObjectHandle(uint gameObjectIndex);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern uint VspScene_GetLiveTransformCount();

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern uint VspScene_GetLiveComponentCount();

		// ---- Cameras ----
		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern uint VspCamera_Create(uint gameObjectHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspCamera_Destroy(uint cameraHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern uint VspCamera_GetGameObject(uint cameraHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspCamera_GetProjectionMode(uint cameraHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspCamera_SetProjectionMode(uint cameraHandle, int projectionMode);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern float VspCamera_GetFieldOfView(uint cameraHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspCamera_SetFieldOfView(uint cameraHandle, float fieldOfView);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern float VspCamera_GetEffectiveFieldOfView(uint cameraHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern float VspCamera_GetOrthographicSize(uint cameraHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspCamera_SetOrthographicSize(uint cameraHandle, float orthographicSize);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern float VspCamera_GetNearClipPlane(uint cameraHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspCamera_SetNearClipPlane(uint cameraHandle, float nearClipPlane);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern float VspCamera_GetFarClipPlane(uint cameraHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspCamera_SetFarClipPlane(uint cameraHandle, float farClipPlane);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern float VspCamera_GetAspect(uint cameraHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspCamera_SetAspect(uint cameraHandle, float aspect);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern float VspCamera_GetFocalLength(uint cameraHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspCamera_SetFocalLength(uint cameraHandle, float focalLength);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern float VspCamera_GetSensorWidth(uint cameraHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspCamera_SetSensorWidth(uint cameraHandle, float sensorWidth);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern float VspCamera_GetSensorHeight(uint cameraHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspCamera_SetSensorHeight(uint cameraHandle, float sensorHeight);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern float VspCamera_GetAperture(uint cameraHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspCamera_SetAperture(uint cameraHandle, float aperture);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern float VspCamera_GetFocusDistance(uint cameraHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspCamera_SetFocusDistance(uint cameraHandle, float focusDistance);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspCamera_GetViewMatrix(uint cameraHandle, [Out] float[] outMatrix16);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspCamera_GetProjectionMatrix(uint cameraHandle, [Out] float[] outMatrix16);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspCamera_GetViewProjectionMatrix(uint cameraHandle, [Out] float[] outMatrix16);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspCamera_GetPosition(uint cameraHandle, [Out] float[] outXyz);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern uint VspScene_GetLiveCameraCount();

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern uint VspScene_GetCameraHandle(uint cameraIndex);

		// ---- Rigidbodies (the motion of a game object) ----
		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern uint VspRigidbody_Create(uint gameObjectHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspRigidbody_Destroy(uint rigidbodyHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern uint VspRigidbody_GetGameObject(uint rigidbodyHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern float VspRigidbody_GetMass(uint rigidbodyHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspRigidbody_SetMass(uint rigidbodyHandle, float mass);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern float VspRigidbody_GetInverseMass(uint rigidbodyHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspRigidbody_IsKinematic(uint rigidbodyHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspRigidbody_SetKinematic(uint rigidbodyHandle, int isKinematic);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspRigidbody_GetUseGravity(uint rigidbodyHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspRigidbody_SetUseGravity(uint rigidbodyHandle, int useGravity);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern float VspRigidbody_GetGravityScale(uint rigidbodyHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspRigidbody_SetGravityScale(uint rigidbodyHandle, float gravityScale);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspRigidbody_GetVelocity(uint rigidbodyHandle, [Out] float[] outXyz);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspRigidbody_SetVelocity(uint rigidbodyHandle, float velocityX, float velocityY, float velocityZ);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspRigidbody_GetAngularVelocity(uint rigidbodyHandle, [Out] float[] outXyz);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspRigidbody_SetAngularVelocity(uint rigidbodyHandle, float velocityX, float velocityY, float velocityZ);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspRigidbody_GetFreezeRotation(uint rigidbodyHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspRigidbody_SetFreezeRotation(uint rigidbodyHandle, int freezeRotation);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern float VspRigidbody_GetLinearDrag(uint rigidbodyHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspRigidbody_SetLinearDrag(uint rigidbodyHandle, float linearDrag);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern float VspRigidbody_GetAngularDrag(uint rigidbodyHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspRigidbody_SetAngularDrag(uint rigidbodyHandle, float angularDrag);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspRigidbody_AddForce(uint rigidbodyHandle, float forceX, float forceY, float forceZ);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspRigidbody_AddImpulse(uint rigidbodyHandle, float impulseX, float impulseY, float impulseZ);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspRigidbody_IsSleeping(uint rigidbodyHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspRigidbody_IsGrounded(uint rigidbodyHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspRigidbody_Wake(uint rigidbodyHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspRigidbody_Sleep(uint rigidbodyHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern uint VspScene_GetLiveRigidbodyCount();

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern uint VspScene_GetRigidbodyHandle(uint rigidbodyIndex);

		// ---- Colliders (the shape of a game object) ----
		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern uint VspCollider_Create(uint gameObjectHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspCollider_Destroy(uint colliderHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern uint VspCollider_GetGameObject(uint colliderHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspCollider_GetShape(uint colliderHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspCollider_SetShape(uint colliderHandle, int shape);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspCollider_GetBoxHalfExtents(uint colliderHandle, [Out] float[] outXyz);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspCollider_SetBoxHalfExtents(uint colliderHandle, float halfExtentX, float halfExtentY, float halfExtentZ);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern float VspCollider_GetSphereRadius(uint colliderHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspCollider_SetSphereRadius(uint colliderHandle, float radius);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspCollider_SetMesh(uint colliderHandle, [In] float[] localPositionsXyz, uint vertexCount, [In] uint[] localIndices, uint indexCount);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern uint VspCollider_GetMeshVertexCount(uint colliderHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern uint VspCollider_GetMeshTriangleCount(uint colliderHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspCollider_GetCenter(uint colliderHandle, [Out] float[] outXyz);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspCollider_SetCenter(uint colliderHandle, float centerX, float centerY, float centerZ);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern float VspCollider_GetRestitution(uint colliderHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspCollider_SetRestitution(uint colliderHandle, float restitution);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern float VspCollider_GetFriction(uint colliderHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspCollider_SetFriction(uint colliderHandle, float friction);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspCollider_GetWorldCenter(uint colliderHandle, [Out] float[] outXyz);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspCollider_GetWorldBounds(uint colliderHandle, [Out] float[] outMinimumXyz, [Out] float[] outMaximumXyz);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern uint VspScene_GetLiveColliderCount();

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern uint VspScene_GetColliderHandle(uint colliderIndex);

		// ---- The physics world ----
		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspPhysics_Step(float deltaTime);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspPhysics_GetGravity([Out] float[] outXyz);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspPhysics_SetGravity(float gravityX, float gravityY, float gravityZ);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspPhysics_GetAutoSimulation();

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspPhysics_SetAutoSimulation(int autoSimulationEnabled);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern uint VspPhysics_GetSolverIterationCount();

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspPhysics_SetSolverIterationCount(uint solverIterationCount);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern uint VspPhysics_GetContactCount();

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspPhysics_GetContact(uint contactIndex, [Out] uint[] outHandles2, [Out] float[] outValues7);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspPhysics_GetStats([Out] uint[] outValues6);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern uint VspPhysics_Raycast(
			float originX, float originY, float originZ,
			float directionX, float directionY, float directionZ,
			float maximumDistance,
			uint ignoredGameObjectHandle,
			[Out] float[] outValues7);

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