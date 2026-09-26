using System;
using System.Numerics;

using VspEngine;

namespace Assembly.Rendering
{
	/// <summary>
	/// The demo's third-person camera: it ORBITS a target - the cube - at a fixed
	/// distance, so the camera always looks at the cube whatever the pointer
	/// does. That is the whole trick of a follow camera, and it is why the view
	/// needs no "look at" step: the camera's orientation IS the orbit, and the
	/// target is always dead centre.
	///
	///     yaw    turns the orbit about the world's up axis (mouse left/right)
	///     pitch  raises or lowers the camera (mouse up/down), 45 degrees by
	///            default, which is the "three-quarter" view a third-person game
	///            opens with
	///     distance  how far the camera sits from the target (mouse wheel)
	///
	/// The angle conventions are the ENGINE's, and the maths that turns them into
	/// a direction is the engine's NATIVE vector/quaternion code
	/// (<see cref="NativeMath"/>): yaw 0 looks down -Z, a positive pitch looks
	/// up, and Euler angles are applied Z then Y then X. Building the camera's
	/// rotation from the same quaternion the direction comes from means the two
	/// can never disagree.
	/// </summary>
	public sealed class ThirdPersonCameraRig
	{
		/// <summary>Default opening view: 45 degrees above the horizon.</summary>
		public const float DefaultPitchDegrees = -45.0f;

		/// <summary>The camera stays between these, so the view never flips over.</summary>
		public const float MinimumPitchDegrees = -80.0f;
		public const float MaximumPitchDegrees = -5.0f;

		/// <summary>The rig the game's script drives; set by the render pipeline.</summary>
		public static ThirdPersonCameraRig? Active { get; internal set; }

		private readonly GameObject cameraObject;
		private Vector3 targetPosition;

		private ThirdPersonCameraRig(GameObject cameraObject)
		{
			this.cameraObject = cameraObject;
			YawDegrees = 0.0f;
			PitchDegrees = DefaultPitchDegrees;
			Distance = 6.5f;
			targetPosition = Vector3.Zero;
		}

		/// <summary>Turn about the world's up axis, in degrees.</summary>
		public float YawDegrees { get; private set; }

		/// <summary>Height of the camera above the horizon, in degrees (negative looks down).</summary>
		public float PitchDegrees { get; private set; }

		/// <summary>How far the camera sits from its target, in world units.</summary>
		public float Distance { get; private set; }

		/// <summary>How many degrees one pixel of mouse movement turns.</summary>
		public float MouseSensitivityDegreesPerPixel { get; set; } = 0.15f;

		/// <summary>Bounds the wheel can move the distance between.</summary>
		public float MinimumDistance { get; set; } = 2.5f;
		public float MaximumDistance { get; set; } = 20.0f;

		/// <summary>Where the camera is currently looking (its target's world position).</summary>
		public Vector3 TargetPosition => targetPosition;

		/// <summary>Where the camera sits, in world space.</summary>
		public Vector3 CameraPosition { get; private set; }

		/// <summary>
		/// Attaches the rig to a game object that already carries a camera, and
		/// publishes it as <see cref="Active"/>.
		/// </summary>
		public static ThirdPersonCameraRig Attach(GameObject cameraObject, Camera camera)
		{
			if (cameraObject == null)
			{
				throw new ArgumentNullException(nameof(cameraObject));
			}
			if (camera == null)
			{
				throw new ArgumentNullException(nameof(camera));
			}

			ThirdPersonCameraRig rig = new ThirdPersonCameraRig(cameraObject);
			Active = rig;
			rig.Update(0.0f);
			return rig;
		}

		/// <summary>Detaches the published rig (the pipeline is being released).</summary>
		public static void Detach()
		{
			Active = null;
		}

		/// <summary>
		/// Turns the orbit by a mouse movement in pixels. This is the ONLY way the
		/// view changes: the camera stays locked on its target, so "moving the
		/// camera" and "moving the look direction" are the same action.
		/// </summary>
		public void Orbit(Vector2 mouseDeltaPixels)
		{
			YawDegrees += mouseDeltaPixels.X * MouseSensitivityDegreesPerPixel;
			PitchDegrees += mouseDeltaPixels.Y * MouseSensitivityDegreesPerPixel;

			// Keep the angles in a readable range; the maths does not care, but a
			// log or a saved setting does.
			YawDegrees = WrapDegrees(YawDegrees);
			PitchDegrees = Clamp(PitchDegrees, MinimumPitchDegrees, MaximumPitchDegrees);
		}

		/// <summary>
		/// Returns the orbit to the view the demo opens with: behind the target,
		/// 45 degrees above it, at the standard distance.
		/// </summary>
		public void ResetView()
		{
			YawDegrees = 0.0f;
			PitchDegrees = DefaultPitchDegrees;
			Distance = 6.5f;
			Update(0.0f);
		}

		/// <summary>Moves the camera closer to or further from its target.</summary>
		public void Zoom(float wheelDelta)
		{
			Distance = Clamp(Distance - (wheelDelta * 0.5f), MinimumDistance, MaximumDistance);
		}

		/// <summary>Places the target - the cube - the camera orbits.</summary>
		public void SetTarget(Vector3 worldPosition)
		{
			targetPosition = worldPosition;
		}

		/// <summary>
		/// Writes the orbit into the camera's transform: where it is and which way
		/// it looks, which is all a camera needs.
		/// </summary>
		public void Update(float deltaTime)
		{
			// The camera's own rotation, built from the engine's Euler
			// convention. The direction the camera looks is that rotation applied
			// to the engine's forward (-Z), so position and orientation are two
			// views of ONE quaternion.
			Quaternion orientation = NativeMath.QuaternionFromEuler(new Vector3(PitchDegrees, YawDegrees, 0.0f));
			Vector3 forward = NativeMath.QuaternionRotateVector(orientation, new Vector3(0.0f, 0.0f, -1.0f));

			// Standing "distance" behind the target along the view direction puts
			// the target exactly in the middle of the frame.
			CameraPosition = targetPosition - (forward * Distance);

			Transform transform = cameraObject.Transform;
			transform.Position = CameraPosition;
			transform.LocalEulerAngles = new Vector3(PitchDegrees, YawDegrees, 0.0f);
		}

		/// <summary>Where the camera looks, in world space.</summary>
		public Vector3 GetForwardDirection()
		{
			Quaternion orientation = NativeMath.QuaternionFromEuler(new Vector3(PitchDegrees, YawDegrees, 0.0f));
			return NativeMath.QuaternionRotateVector(orientation, new Vector3(0.0f, 0.0f, -1.0f));
		}

		/// <summary>
		/// The camera's forward direction flattened onto the ground plane - what
		/// "forward" means when a character walks. Falls back to -Z when the
		/// camera looks straight down, where the flattened direction vanishes.
		/// </summary>
		public Vector3 GetGroundForwardDirection()
		{
			Vector3 forward = GetForwardDirection();
			forward.Y = 0.0f;
			if (forward.LengthSquared() < 1e-6f)
			{
				return new Vector3(0.0f, 0.0f, -1.0f);
			}
			return NativeMath.Normalize(forward);
		}

		/// <summary>
		/// The camera's right direction flattened onto the ground plane. In a
		/// right-handed, Y-up frame it is forward x up, which is the cross product
		/// that points to +X when the camera looks down -Z.
		/// </summary>
		public Vector3 GetGroundRightDirection()
		{
			Vector3 forward = GetGroundForwardDirection();
			return NativeMath.Normalize(NativeMath.Cross(forward, new Vector3(0.0f, 1.0f, 0.0f)));
		}

		private static float Clamp(float value, float minimum, float maximum) =>
			value < minimum ? minimum : (value > maximum ? maximum : value);

		private static float WrapDegrees(float degrees)
		{
			while (degrees > 180.0f)
			{
				degrees -= 360.0f;
			}
			while (degrees < -180.0f)
			{
				degrees += 360.0f;
			}
			return degrees;
		}
	}
}
