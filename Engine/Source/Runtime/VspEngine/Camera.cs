using System;
using System.Numerics;

namespace VspEngine
{
	/// <summary>How a camera turns the scene into clip space.</summary>
	public enum CameraProjectionMode
	{
		/// <summary>A box: things keep their size however far away they are.</summary>
		Orthographic = 0,

		/// <summary>A pyramid described by a vertical field of view.</summary>
		Perspective = 1,

		/// <summary>A pyramid described by a real lens: focal length and sensor.</summary>
		Physical = 2,
	}

	/// <summary>
	/// Reference handle for a native camera. A camera belongs to a game object,
	/// and that object's transform is where the camera is and which way it looks
	/// (down its local -Z, with +Y up).
	///
	/// The projection settings - and the lens a <see cref="CameraProjectionMode.Physical"/>
	/// camera is described by - live in the native scene; the matrices follow from
	/// them and from the transform, so nothing here can go stale.
	///
	/// A render pipeline renders from a camera by handing it to the frame:
	/// <c>context.SetCamera(camera)</c>, which is what fills the engine's camera
	/// uniform buffer (the block a shader reads as "CameraUniformBuffer").
	/// </summary>
	public sealed class Camera : Object
	{
		private readonly float[] xyzScratch = new float[3];
		private readonly float[] matrixScratch = new float[16];

		internal Camera(uint nativeHandle)
		{
			NativeHandle = nativeHandle;
		}

		/// <summary>Creates a camera on a game object.</summary>
		public static Camera? Create(GameObject gameObject)
		{
			if (gameObject == null)
			{
				throw new ArgumentNullException(nameof(gameObject));
			}

			uint nativeHandle = NativeApi.VspCamera_Create(gameObject.NativeHandle);
			return nativeHandle != 0 ? new Camera(nativeHandle) : null;
		}

		/// <summary>Every camera of the scene, in the order the scene stores them.</summary>
		public static Camera[] All()
		{
			int cameraCount = (int)NativeApi.VspScene_GetLiveCameraCount();
			System.Collections.Generic.List<Camera> cameras = new System.Collections.Generic.List<Camera>(cameraCount);
			for (uint cameraIndex = 0; cameraIndex < cameraCount; ++cameraIndex)
			{
				uint cameraHandle = NativeApi.VspScene_GetCameraHandle(cameraIndex);
				if (cameraHandle != 0)
				{
					cameras.Add(new Camera(cameraHandle));
				}
			}
			return cameras.ToArray();
		}

		/// <summary>The first camera of the scene, or null when there is none.</summary>
		public static Camera? GetMainCamera()
		{
			Camera[] cameras = All();
			return cameras.Length > 0 ? cameras[0] : null;
		}

		/// <summary>The game object the camera belongs to (its transform is the view).</summary>
		public GameObject? OwnerGameObject
		{
			get
			{
				uint gameObjectHandle = NativeApi.VspCamera_GetGameObject(NativeHandle);
				return gameObjectHandle != 0 ? new GameObject(gameObjectHandle) : null;
			}
		}

		/// <summary>How the camera projects the scene.</summary>
		public CameraProjectionMode ProjectionMode
		{
			get => (CameraProjectionMode)NativeApi.VspCamera_GetProjectionMode(NativeHandle);
			set => NativeApi.VspCamera_SetProjectionMode(NativeHandle, (int)value);
		}

		/// <summary>Vertical field of view in degrees (perspective cameras).</summary>
		public float FieldOfView
		{
			get => NativeApi.VspCamera_GetFieldOfView(NativeHandle);
			set => NativeApi.VspCamera_SetFieldOfView(NativeHandle, value);
		}

		/// <summary>
		/// The vertical field of view the camera actually works out to: the
		/// configured one, or the one a physical camera's lens produces.
		/// </summary>
		public float EffectiveFieldOfView => NativeApi.VspCamera_GetEffectiveFieldOfView(NativeHandle);

		/// <summary>Half the height the camera sees, in world units (orthographic).</summary>
		public float OrthographicSize
		{
			get => NativeApi.VspCamera_GetOrthographicSize(NativeHandle);
			set => NativeApi.VspCamera_SetOrthographicSize(NativeHandle, value);
		}

		public float NearClipPlane
		{
			get => NativeApi.VspCamera_GetNearClipPlane(NativeHandle);
			set => NativeApi.VspCamera_SetNearClipPlane(NativeHandle, value);
		}

		public float FarClipPlane
		{
			get => NativeApi.VspCamera_GetFarClipPlane(NativeHandle);
			set => NativeApi.VspCamera_SetFarClipPlane(NativeHandle, value);
		}

		/// <summary>Width / height of the viewport; 0 follows the render target.</summary>
		public float Aspect
		{
			get => NativeApi.VspCamera_GetAspect(NativeHandle);
			set => NativeApi.VspCamera_SetAspect(NativeHandle, value);
		}

		// -------- The lens, for a physical camera --------

		/// <summary>Focal length of the lens, in millimetres.</summary>
		public float FocalLength
		{
			get => NativeApi.VspCamera_GetFocalLength(NativeHandle);
			set => NativeApi.VspCamera_SetFocalLength(NativeHandle, value);
		}

		/// <summary>Width of the film back, in millimetres (36 for full frame).</summary>
		public float SensorWidth
		{
			get => NativeApi.VspCamera_GetSensorWidth(NativeHandle);
			set => NativeApi.VspCamera_SetSensorWidth(NativeHandle, value);
		}

		/// <summary>Height of the film back, in millimetres (24 for full frame).</summary>
		public float SensorHeight
		{
			get => NativeApi.VspCamera_GetSensorHeight(NativeHandle);
			set => NativeApi.VspCamera_SetSensorHeight(NativeHandle, value);
		}

		/// <summary>Aperture as an f-number; 0 means "no depth of field".</summary>
		public float Aperture
		{
			get => NativeApi.VspCamera_GetAperture(NativeHandle);
			set => NativeApi.VspCamera_SetAperture(NativeHandle, value);
		}

		/// <summary>Distance the lens is focused at, in world units.</summary>
		public float FocusDistance
		{
			get => NativeApi.VspCamera_GetFocusDistance(NativeHandle);
			set => NativeApi.VspCamera_SetFocusDistance(NativeHandle, value);
		}

		// -------- Matrices --------

		/// <summary>Takes a world point into the camera's view space.</summary>
		public Matrix4x4 ViewMatrix
		{
			get
			{
				NativeApi.VspCamera_GetViewMatrix(NativeHandle, matrixScratch);
				return ToMatrix4x4(matrixScratch);
			}
		}

		/// <summary>Takes a view point into Vulkan clip space.</summary>
		public Matrix4x4 ProjectionMatrix
		{
			get
			{
				NativeApi.VspCamera_GetProjectionMatrix(NativeHandle, matrixScratch);
				return ToMatrix4x4(matrixScratch);
			}
		}

		/// <summary>Takes a world point into Vulkan clip space.</summary>
		public Matrix4x4 ViewProjectionMatrix
		{
			get
			{
				NativeApi.VspCamera_GetViewProjectionMatrix(NativeHandle, matrixScratch);
				return ToMatrix4x4(matrixScratch);
			}
		}

		/// <summary>Where the camera is, in world space.</summary>
		public Vector3 Position
		{
			get
			{
				NativeApi.VspCamera_GetPosition(NativeHandle, xyzScratch);
				return new Vector3(xyzScratch[0], xyzScratch[1], xyzScratch[2]);
			}
		}

		public void Destroy()
		{
			NativeApi.VspCamera_Destroy(NativeHandle);
			NativeHandle = 0;
		}

		/// <summary>
		/// Turns the native column-major array into a <see cref="Matrix4x4"/>
		/// (which stores rows): element (row, column) sits at column * 4 + row.
		/// </summary>
		private static Matrix4x4 ToMatrix4x4(float[] columnMajorValues)
		{
			return new Matrix4x4(
				columnMajorValues[0], columnMajorValues[4], columnMajorValues[8], columnMajorValues[12],
				columnMajorValues[1], columnMajorValues[5], columnMajorValues[9], columnMajorValues[13],
				columnMajorValues[2], columnMajorValues[6], columnMajorValues[10], columnMajorValues[14],
				columnMajorValues[3], columnMajorValues[7], columnMajorValues[11], columnMajorValues[15]);
		}
	}
}
