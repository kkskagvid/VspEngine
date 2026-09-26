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
	///     yaw    turns the orbit about the world's up axis (mouse left/right).
	///            A full TURN is 360 degrees and the angle wraps, so the camera
	///            swings all the way round the cube and keeps going.
	///     pitch  raises or lowers the camera (mouse up/down), 45 degrees by
	///            default, which is the "three-quarter" view a third-person game
	///            opens with. It covers 178 of the 180 degrees a vertical view
	///            has: from almost straight above to almost straight below.
	///     distance  how far the camera sits from the target (mouse wheel)
	///
	/// The angle conventions are the ENGINE's, and the maths that turns them into
	/// a direction is the engine's NATIVE vector/quaternion code
	/// (<see cref="NativeMath"/>): yaw 0 looks down -Z, a positive pitch looks
	/// up, and Euler angles are applied Z then Y then X. Building the camera's
	/// rotation from the same quaternion the direction comes from means the two
	/// can never disagree.
	///
	/// A camera that only orbits walks through the world: tilt far enough down
	/// and it ends up below the floor. The rig therefore asks the PHYSICS world
	/// for the first collider between the target and the place the camera wants to
	/// be, and stops in front of it - which is what keeps the view out of the
	/// ground while the cube stays centred.
	/// </summary>
	public sealed class ThirdPersonCameraRig
	{
		/// <summary>Default opening view: 45 degrees above the horizon.</summary>
		public const float DefaultPitchDegrees = -45.0f;

		/// <summary>
		/// How far the camera may look up or down. The last degree at each end is
		/// left out on purpose: at exactly straight up or straight down the view
		/// direction is vertical, the ground-plane "forward" a character walks
		/// along vanishes and the yaw stops meaning anything.
		/// </summary>
		public const float MinimumPitchDegrees = -89.0f;
		public const float MaximumPitchDegrees = 89.0f;

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

		/// <summary>Turn about the world's up axis, in degrees, wrapped to (-180, 180].</summary>
		public float YawDegrees { get; private set; }

		/// <summary>
		/// The camera's rotation for the two angles: the PITCH about the camera's
		/// own right axis first, then the YAW that carries the tilted camera around
		/// the world's up axis.
		///
		/// The order is the whole point. Building the rotation the other way round
		/// - which is what a pair of Euler angles applied Y then X gives - makes
		/// the tilt a rotation about the WORLD's X axis, so at a yaw of 90 degrees
		/// "up and down" turns into a roll and the camera stops rising at all. A
		/// camera whose two angles have to MEAN "higher" and "further round" needs
		/// the tilt inside the turn.
		/// </summary>
		public Quaternion Orientation
		{
			get
			{
				Quaternion yawRotation = NativeMath.QuaternionFromEuler(new Vector3(0.0f, YawDegrees, 0.0f));
				Quaternion pitchRotation = NativeMath.QuaternionFromEuler(new Vector3(PitchDegrees, 0.0f, 0.0f));
				return NativeMath.QuaternionMultiply(yawRotation, pitchRotation);
			}
		}

		/// <summary>Height of the camera above the horizon, in degrees (negative looks down).</summary>
		public float PitchDegrees { get; private set; }

		/// <summary>How far the camera is asked to sit from its target, in world units.</summary>
		public float Distance { get; private set; }

		/// <summary>
		/// How far the camera actually ended up from its target once the world had
		/// its say: equal to <see cref="Distance"/> in the open, shorter when
		/// something was in the way.
		/// </summary>
		public float ResolvedDistance { get; private set; }

		/// <summary>How many degrees one pixel of mouse movement turns.</summary>
		public float MouseSensitivityDegreesPerPixel { get; set; } = 0.15f;

		/// <summary>Bounds the wheel can move the distance between.</summary>
		public float MinimumDistance { get; set; } = 2.5f;
		public float MaximumDistance { get; set; } = 20.0f;

		/// <summary>
		/// Whether the view is kept out of the world. On by default; a game that
		/// wants a camera that can walk through walls turns it off.
		/// </summary>
		public bool CollisionEnabled { get; set; } = true;

		/// <summary>How far in front of a surface the camera is kept, in world units.</summary>
		public float CollisionPadding { get; set; } = 0.35f;

		/// <summary>
		/// Closest the camera is brought to its target when something is in the
		/// way. Closing in is what a camera does when it is cornered, but a camera
		/// that reaches the target shows the inside of the object it follows.
		/// </summary>
		public float MinimumCollisionDistance { get; set; } = 1.2f;

		/// <summary>
		/// The object the camera follows. Its colliders are looked PAST, because
		/// the ray towards the camera leaves the object from the inside and would
		/// otherwise stop on it at once.
		/// </summary>
		public GameObject? FollowedObject { get; set; }

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
			// The camera's rotation, built from the two angles. Its own +Z points
			// BEHIND the camera and its -Z is where it looks, so standing one
			// "distance" along +Z from the target puts the target exactly in the
			// middle of the frame - the camera looks at the cube because of where
			// it was placed, not because a second step aimed it.
			Quaternion orientation = Orientation;
			Vector3 behind = NativeMath.QuaternionRotateVector(orientation, new Vector3(0.0f, 0.0f, 1.0f));

			// A body between the target and that place pulls the camera in front of
			// it; the direction towards the camera is unchanged, so the cube stays
			// centred either way.
			Vector3 desiredPosition = targetPosition + (behind * Distance);
			CameraPosition = ResolveCameraPosition(desiredPosition);

			Transform transform = cameraObject.Transform;
			transform.Position = CameraPosition;

			// The rotation is written back as Euler angles - that is what a
			// Transform stores - through the engine's own conversion, so the
			// orientation it is given and the one it renders are the same rotation
			// to the last bit.
			transform.LocalEulerAngles = NativeMath.QuaternionToEuler(orientation);
		}

		/// <summary>Where the camera looks, in world space.</summary>
		public Vector3 GetForwardDirection() =>
			NativeMath.QuaternionRotateVector(Orientation, new Vector3(0.0f, 0.0f, -1.0f));

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

		/// <summary>
		/// The place the camera ends up: where it wanted to be, or - when
		/// something is between the target and that place - just in front of
		/// whatever the ray found.
		/// </summary>
		private Vector3 ResolveCameraPosition(Vector3 desiredPosition)
		{
			Vector3 offset = desiredPosition - targetPosition;
			float wantedDistance = offset.Length();
			ResolvedDistance = wantedDistance;

			if (!CollisionEnabled || wantedDistance <= MinimumCollisionDistance)
			{
				return desiredPosition;
			}

			// The ray leaves the target, so the colliders of the object being
			// followed are looked past: they are around the START of the ray, not
			// something it ran into.
			Vector3 direction = offset / wantedDistance;
			RaycastResult hit = Physics.Raycast(targetPosition, direction, wantedDistance, FollowedObject);
			if (!hit.HasHit)
			{
				return desiredPosition;
			}

			float safeDistance = hit.Distance - CollisionPadding;
			if (safeDistance < MinimumCollisionDistance)
			{
				safeDistance = MinimumCollisionDistance;
			}

			ResolvedDistance = safeDistance;
			return targetPosition + (direction * safeDistance);
		}

		private static float Clamp(float value, float minimum, float maximum) =>
			value < minimum ? minimum : (value > maximum ? maximum : value);

		/// <summary>Turns an angle into the (-180, 180] range a reader expects.</summary>
		private static float WrapDegrees(float degrees)
		{
			while (degrees > 180.0f)
			{
				degrees -= 360.0f;
			}
			while (degrees <= -180.0f)
			{
				degrees += 360.0f;
			}
			return degrees;
		}
	}
}
