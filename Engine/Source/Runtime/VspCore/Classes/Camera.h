#pragma once

#include "Classes/Object.h"
#include "Core/Core.h"

namespace Vsp
{
	// -------------------------------------------------------------------------
	// CameraProjectionMode
	// -------------------------------------------------------------------------
	// How a camera turns the scene into clip space:
	//
	//   Orthographic  a box: things keep their size however far away they are,
	//                 which is what 2D content drawn in 3D wants.
	//   Perspective   a pyramid described by a vertical field of view.
	//   Physical      a pyramid described by a real lens - focal length and
	//                 sensor size - which is how a photographer (or a film
	//                 camera) states the same thing.
	// -------------------------------------------------------------------------
	enum class CameraProjectionMode : uint32
	{
		Orthographic = 0,
		Perspective = 1,
		Physical = 2,
	};

	// -------------------------------------------------------------------------
	// Camera
	// -------------------------------------------------------------------------
	// Native storage of one camera: the projection settings, the lens a physical
	// camera is described by, and the matrices that follow from them plus the
	// world transform of the game object the camera is attached to.
	//
	// A camera looks down the local -Z of its transform (Y is up, Vulkan clip
	// space: X right, Y down, depth 0..1), so a camera placed at (0, 0, 5) with no
	// rotation looks at the origin.
	//
	// Managed code reaches it through the VspEngine.Camera reference handle; the
	// render pipeline hands the camera it draws with to the frame, which is how
	// the engine's camera uniform buffer gets filled.
	//
	// Every function is a plain data read or write; nothing throws.
	// -------------------------------------------------------------------------
	class RUNTIME_API Camera : public NativeObject
	{
	public:
		// -------- Owner --------
		// The game object the camera belongs to: its transform is the camera's
		// place in the scene.
		NativeObjectHandle GetOwnerGameObjectHandle() const { return m_uOwnerGameObjectHandle; }
		void SetOwnerGameObjectHandle(NativeObjectHandle uGameObjectHandle);

		// Transform handle behind the camera's view (0 when the owner is gone).
		NativeObjectHandle GetTransformHandle() const;

		// -------- Projection --------
		CameraProjectionMode GetProjectionMode() const { return m_eProjectionMode; }
		void SetProjectionMode(CameraProjectionMode eProjectionMode);

		// Vertical field of view in degrees (perspective; physical derives it).
		float GetFieldOfView() const { return m_fFieldOfView; }
		void SetFieldOfView(float fFieldOfView);

		// Half the height the camera sees, in world units (orthographic).
		float GetOrthographicSize() const { return m_fOrthographicSize; }
		void SetOrthographicSize(float fOrthographicSize);

		float GetNearClipPlane() const { return m_fNearClipPlane; }
		void SetNearClipPlane(float fNearClipPlane);
		float GetFarClipPlane() const { return m_fFarClipPlane; }
		void SetFarClipPlane(float fFarClipPlane);

		// Width / height of the viewport. 0 means "follow the render target".
		float GetAspect() const { return m_fAspect; }
		void SetAspect(float fAspect) { m_fAspect = fAspect; }

		// -------- The lens a physical camera is described by --------
		// Focal length and sensor size in millimetres, aperture as an f-number,
		// focus distance in world units.
		float GetFocalLength() const { return m_fFocalLength; }
		void SetFocalLength(float fFocalLength);
		float GetSensorWidth() const { return m_fSensorWidth; }
		void SetSensorWidth(float fSensorWidth);
		float GetSensorHeight() const { return m_fSensorHeight; }
		void SetSensorHeight(float fSensorHeight);
		float GetAperture() const { return m_fAperture; }
		void SetAperture(float fAperture);
		float GetFocusDistance() const { return m_fFocusDistance; }
		void SetFocusDistance(float fFocusDistance);

		// -------- What the settings work out to --------
		// The vertical field of view the mode produces: the value itself for a
		// perspective camera, the one the lens implies for a physical camera.
		float GetEffectiveFieldOfView() const;

		// The aspect ratio the projection uses: the configured one, or the render
		// target's when none was configured.
		float ResolveAspect() const;

		// -------- Matrices (column-major, 16 floats each) --------
		// World -> view, from the camera's place in the scene.
		void GetViewMatrix(float* pOutMatrix16) const;

		// View -> clip, from the projection settings.
		void GetProjectionMatrix(float* pOutMatrix16) const;

		// World -> clip, the matrix a vertex stage multiplies by.
		void GetViewProjectionMatrix(float* pOutMatrix16) const;

		// World -> view without the translation: what a direction is transformed
		// by (and what an environment map is looked up with).
		void GetRotationOnlyViewMatrix(float* pOutMatrix16) const;

		// Where the camera is, in world space.
		void GetWorldPosition(float& outPositionX, float& outPositionY, float& outPositionZ) const;

		// True when a world point is in front of the camera, which is the cheap
		// half of a visibility test.
		bool IsPointVisible(float fWorldX, float fWorldY, float fWorldZ) const;

	private:
		NativeObjectHandle m_uOwnerGameObjectHandle = k_nInvalidObjectHandle;

		CameraProjectionMode m_eProjectionMode = CameraProjectionMode::Perspective;

		float m_fFieldOfView = 60.0f;        // Vertical, degrees.
		float m_fOrthographicSize = 1.0f;    // Half height, world units.
		float m_fNearClipPlane = 0.1f;
		float m_fFarClipPlane = 100.0f;
		float m_fAspect = 0.0f;              // 0 = follow the render target.

		// A 36 x 24 mm film back with a 50 mm lens: what a "normal" camera sees.
		float m_fFocalLength = 50.0f;
		float m_fSensorWidth = 36.0f;
		float m_fSensorHeight = 24.0f;
		float m_fAperture = 2.8f;
		float m_fFocusDistance = 10.0f;
	};
}
