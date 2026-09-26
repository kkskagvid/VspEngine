using System.Numerics;

using Assembly.Rendering;

using VspEngine;

namespace Assembly
{
	/// <summary>
	/// Acceptance demo living in the game Assembly (Assembly.dll): it drives the
	/// grey cube the render pipeline draws.
	///
	///   W / A / S / D   push the cube over the ground, relative to the camera
	///   Mouse           orbit and tilt the camera around the cube (it always
	///                   looks at it); a full turn is 360 degrees left and right,
	///                   and the tilt covers the 180 degrees up and down
	///   Mouse wheel     move the camera closer to / further from the cube
	///   T               flip the spin: clockwise about Y, or counter-clockwise
	///   R               drop the cube again from its starting height
	///
	/// The cube is a PHYSICS object, not a script that writes positions: a
	/// <see cref="Rigidbody"/> carries its velocity and its spin, and a
	/// <see cref="BoxCollider"/> is the shape that lands on the ground plate. It
	/// is dropped from a height, falls, bounces once or twice and comes to rest ON
	/// the plate - the surface the plate's own mesh collider describes.
	///
	/// The script therefore does what a game script does: it sets the velocity of
	/// the body (W/A/S/D), sets the turn rate (the spin, flipped by T) and asks
	/// for a new drop (R). Everything that happens next - the fall, the bounce,
	/// the friction, coming to rest - is the simulation's answer.
	///
	/// The script owns no rendering state of its own: the transform lives in the
	/// native scene and the material is what the pipeline reads while it builds
	/// the frame. What the script publishes for the HUD is plain state a click on
	/// a HUD button can change as well.
	/// </summary>
	public sealed class CubeController : ScriptBehaviour
	{
		/// <summary>How fast the cube moves over the ground, in world units per second.</summary>
		public const float MoveSpeed = 2.4f;

		/// <summary>Degrees the cube turns about Y per second.</summary>
		public const float SpinSpeedDegreesPerSecond = 65.0f;

		/// <summary>
		/// Height the cube is dropped from, in world units above the origin. The
		/// plate's surface is at GroundSurfaceHeight, so the fall is this far plus
		/// half the cube.
		/// </summary>
		public const float DropHeight = 3.0f;

		/// <summary>Edge of the cube, and therefore of its box collider, in world units.</summary>
		public const float CubeSize = 1.0f;

		/// <summary>Mass of the cube, in kilograms (the simulation only uses the ratio of masses).</summary>
		public const float CubeMass = 1.0f;

		/// <summary>
		/// How much of the landing speed the cube gives back: 0 would drop it dead
		/// on the plate, 1 would return it to the height it fell from.
		/// </summary>
		public const float CubeRestitution = 0.4f;

		/// <summary>How strongly the cube resists sliding along the plate.</summary>
		public const float CubeFriction = 0.35f;

		/// <summary>
		/// Vertical speed, in world units per second, that counts as an impact and
		/// as a bounce. Below it the cube is settling, not bouncing.
		/// </summary>
		private const float BounceDetectionSpeed = 0.75f;

		/// <summary>
		/// The spin direction the demo starts with: CLOCKWISE about +Y, seen from
		/// above. T flips this.
		/// </summary>
		public const bool DefaultSpinsCounterClockwise = false;

		/// <summary>
		/// Which way the cube spins, shared with the render pipeline because the
		/// HUD reports it and a HUD button can flip it.
		/// </summary>
		private static bool spinsCounterClockwise = DefaultSpinsCounterClockwise;

		/// <summary>Set by a HUD button; consumed by the next OnUpdate.</summary>
		private static bool resetRequested;

		/// <summary>Bounces the cube has taken since it was last dropped.</summary>
		private static int bounceCount;

		/// <summary>The cube's body: what carries its velocity and its spin.</summary>
		private Rigidbody? physicsBody;

		/// <summary>The cube's shape: what lands on the ground plate.</summary>
		private BoxCollider? physicsShape;

		/// <summary>Vertical speed of the previous frame, which is what detects a bounce.</summary>
		private float previousVerticalSpeed;

		/// <summary>
		/// Seconds of engine time since the last state line. The demo reports
		/// where the cube is and which way the camera looks twice a second: often
		/// enough that an automated run sees every phase of a scheduled key
		/// sequence, rare enough that the log stays readable.
		/// </summary>
		private float secondsSinceStateLine;

		/// <summary>How often that line is written, in seconds of engine time.</summary>
		private const float StateLineIntervalSeconds = 0.5f;

		/// <summary>True while the cube turns counter-clockwise about +Y.</summary>
		public static bool IsSpinningCounterClockwise => spinsCounterClockwise;

		/// <summary>How many times the cube has bounced off the ground since it was dropped.</summary>
		public static int BounceCount => bounceCount;

		/// <summary>Flips the spin direction. Wired to both T and the HUD button.</summary>
		public static void ToggleSpinDirection()
		{
			spinsCounterClockwise = !spinsCounterClockwise;
		}

		/// <summary>Asks the controller to drop the cube again. Wired to the HUD button.</summary>
		public static void RequestReset()
		{
			resetRequested = true;
		}

		/// <summary>Forgets the static state; called when the instance starts.</summary>
		public override void OnInit()
		{
			spinsCounterClockwise = DefaultSpinsCounterClockwise;
			resetRequested = false;
			bounceCount = 0;
			previousVerticalSpeed = 0.0f;

			CreatePhysicsObjects();
			DropCube();

			Debug.Log("CubeController: OnInit (InstanceID = " + InstanceID
				+ ", component handle = " + NativeHandle + ")");
		}

		public override void OnStart()
		{
			Debug.Log("CubeController: OnStart - W/A/S/D push the cube, mouse turns the camera, T flips the spin, R drops it again");

			// The demo turns the camera with the mouse, so the pointer is hidden and
			// held at the centre of the window: a pointer left visible would walk out
			// of the window and the view would stop turning with it. The engine
			// recentres it after every frame, so the movement the script reads is
			// always a movement of the hand.
			//
			// The mode is READ BACK and logged rather than assumed: what the engine
			// holds is what the run is checked against, not what this line says it
			// asked for.
			Cursor.Mode = CursorMode.Locked;
			Debug.Log("CubeController: cursor mode is " + Cursor.Mode
				+ " - the pointer is hidden and recentred so the mouse turns the camera without running out of screen");

			// The interface is what carries every static string of the demo, in
			// the locale the machine is set to. Loading before the first frame is
			// what keeps a screenshot free of untranslated keys.
			LoadLocalisation();
		}

		public override void OnUpdate()
		{
			float deltaTime = Time.DeltaTime;

			if (physicsBody == null || !physicsBody.IsValid)
			{
				// Without a body there is nothing to simulate; the demo keeps
				// running and the reason is already in the log.
				ReportStatePeriodically(deltaTime, ThirdPersonCameraRig.Active);
				return;
			}

			UpdateSpin();
			UpdateMovement();
			UpdateKeys();
			UpdateBounceReporting();
			UpdateCamera();

			ReportStatePeriodically(deltaTime, ThirdPersonCameraRig.Active);
		}

		/// <summary>
		/// Creates the two physics objects the cube needs: a box collider, which
		/// is its shape, and a rigidbody, which is what the simulation moves.
		/// </summary>
		private void CreatePhysicsObjects()
		{
			physicsShape = BoxCollider.Create(GameObject);
			if (physicsShape == null)
			{
				Debug.LogError("CubeController: the cube's box collider could not be created.");
				return;
			}

			// The collider is the cube the pipeline draws: same size, same place.
			physicsShape.Size = new Vector3(CubeSize, CubeSize, CubeSize);
			physicsShape.Restitution = CubeRestitution;
			physicsShape.Friction = CubeFriction;

			physicsBody = Rigidbody.Create(GameObject);
			if (physicsBody == null)
			{
				Debug.LogError("CubeController: the cube's rigidbody could not be created.");
				return;
			}

			physicsBody.Mass = CubeMass;

			// The demo wants the plainest fall there is: no drag in the air, so
			// the only things acting on the cube are gravity and the plate.
			physicsBody.LinearDrag = 0.0f;
			physicsBody.AngularDrag = 0.0f;

			Debug.Log("CubeController: cube is a physics object (box collider "
				+ CubeSize + " wide, mass " + CubeMass + ", restitution "
				+ CubeRestitution + ", friction " + CubeFriction + ").");
		}

		/// <summary>Puts the cube back at its starting height and lets go of it.</summary>
		private void DropCube()
		{
			Transform.Position = new Vector3(0.0f, DropHeight, 0.0f);
			Transform.LocalEulerAngles = Vector3.Zero;

			if (physicsBody != null && physicsBody.IsValid)
			{
				physicsBody.Velocity = Vector3.Zero;
				physicsBody.AngularVelocity = Vector3.Zero;
				physicsBody.Wake();
			}

			bounceCount = 0;
			previousVerticalSpeed = 0.0f;
		}

		/// <summary>
		/// Sets the cube's TURN RATE. The simulation turns the object, so a
		/// contact can never fight a script over the rotation - which is exactly
		/// what would happen if the script wrote Euler angles into a body that
		/// physics also rotates.
		/// </summary>
		private void UpdateSpin()
		{
			float spinDirection = spinsCounterClockwise ? 1.0f : -1.0f;
			physicsBody!.AngularVelocity = new Vector3(0.0f, SpinSpeedDegreesPerSecond * spinDirection, 0.0f);
		}

		/// <summary>
		/// W/A/S/D set the cube's HORIZONTAL velocity in the camera's frame, which
		/// is what makes "forward" mean "away from the viewer" however the camera
		/// is turned. The vertical velocity is left alone: gravity and the plate
		/// own it. Releasing the keys leaves the velocity to friction, so the cube
		/// slides to a stop instead of stopping dead.
		/// </summary>
		private void UpdateMovement()
		{
			ThirdPersonCameraRig? rig = ThirdPersonCameraRig.Active;
			Vector3 forward = rig?.GetGroundForwardDirection() ?? new Vector3(0.0f, 0.0f, -1.0f);
			Vector3 right = rig?.GetGroundRightDirection() ?? new Vector3(1.0f, 0.0f, 0.0f);

			Vector3 movement = Vector3.Zero;
			if (Input.GetKey(KeyCode.W))
			{
				movement += forward;
			}
			if (Input.GetKey(KeyCode.S))
			{
				movement -= forward;
			}
			if (Input.GetKey(KeyCode.D))
			{
				movement += right;
			}
			if (Input.GetKey(KeyCode.A))
			{
				movement -= right;
			}

			if (movement.LengthSquared() <= 0.0f)
			{
				return;
			}

			Vector3 velocity = physicsBody!.Velocity;
			Vector3 pushDirection = NativeMath.Normalize(movement);
			physicsBody.Velocity = new Vector3(
				pushDirection.X * MoveSpeed,
				velocity.Y,
				pushDirection.Z * MoveSpeed);
		}

		private void UpdateKeys()
		{
			if (Input.GetKeyDown(KeyCode.T))
			{
				ToggleSpinDirection();
				Debug.Log("CubeController: spin -> " + (spinsCounterClockwise ? "counter-clockwise" : "clockwise") + " about Y");
			}

			if (Input.GetKeyDown(KeyCode.R) || resetRequested)
			{
				resetRequested = false;
				DropCube();
                Debug.Log("CubeController: dropped again from height " + DropHeight + " with no velocity");
			}

            if (Input.GetKeyDown(KeyCode.G))
            {
                Cursor.Mode = CursorMode.Visible;
                Debug.Log("CubeController: cursor mode is " + Cursor.Mode + " - the pointer is visible and can leave the window");
            }
            if (Input.GetKeyUp(KeyCode.G))
            {
                Cursor.Mode = CursorMode.Locked;
                Debug.Log("CubeController: cursor mode is " + Cursor.Mode + " - the pointer is hidden and recentred so the mouse turns the camera without running out of screen");
            }
        }

		/// <summary>
		/// Notices the moment the cube stops falling and starts rising, which is a
		/// bounce, and reports it. The impact speed is the speed it arrived with,
		/// and the bounce is what restitution gave back.
		/// </summary>
		private void UpdateBounceReporting()
		{
			float verticalSpeed = physicsBody!.Velocity.Y;

			if (previousVerticalSpeed < -BounceDetectionSpeed && verticalSpeed > BounceDetectionSpeed)
			{
				++bounceCount;
				Debug.Log("CubeController: the cube bounced off the ground (impact "
					+ Format(-previousVerticalSpeed) + " down, rebound " + Format(verticalSpeed)
					+ " up, bounce " + bounceCount + ").");
			}

			previousVerticalSpeed = verticalSpeed;
		}

		/// <summary>
		/// The pointer turns and tilts the ORBIT, never a free look: the rig keeps
		/// the cube centred, which is the whole definition of a follow camera.
		/// </summary>
		private void UpdateCamera()
		{
			ThirdPersonCameraRig? rig = ThirdPersonCameraRig.Active;
			if (rig == null)
			{
				return;
			}

			Vector2 mouseDelta = Input.MouseDelta;
			if (mouseDelta.LengthSquared() > 0.0f)
			{
				rig.Orbit(mouseDelta);
			}

			float wheel = Input.MouseScroll.Y;
			if (wheel != 0.0f)
			{
				rig.Zoom(wheel);
			}

			// The camera looks past the cube's own collider: a ray from the cube
			// towards the camera leaves the cube from the inside. WHERE the camera
			// points is the render pipeline's business - it reads the position the
			// frame will actually draw, which is the one the physics step just
			// produced.
			rig.FollowedObject = GameObject;
		}

		/// <summary>
		/// Writes one state line per half second of engine time: where the cube is,
		/// how fast it moves, whether it is standing on something and where the
		/// camera is looking. An automated run reads these lines to check what the
		/// simulation did without having to interpret pixels.
		/// </summary>
		private void ReportStatePeriodically(float deltaTime, ThirdPersonCameraRig? rig)
		{
			secondsSinceStateLine += deltaTime;
			if (secondsSinceStateLine < StateLineIntervalSeconds)
			{
				return;
			}
			secondsSinceStateLine -= StateLineIntervalSeconds;

			Vector3 position = Transform.Position;
			Vector3 velocity = (physicsBody != null && physicsBody.IsValid) ? physicsBody.Velocity : Vector3.Zero;
			bool grounded = physicsBody != null && physicsBody.IsValid && physicsBody.IsGrounded;

			string cameraState = "no rig";
			if (rig != null)
			{
				Vector3 cameraPosition = rig.CameraPosition;
				cameraState = "yaw " + Format(rig.YawDegrees)
					+ " pitch " + Format(rig.PitchDegrees)
					+ " distance " + Format(rig.ResolvedDistance)
					+ " at (" + Format(cameraPosition.X) + ", " + Format(cameraPosition.Y) + ", " + Format(cameraPosition.Z) + ")";
			}

			Debug.Log("CubeController: cube (" + Format(position.X) + ", " + Format(position.Y) + ", " + Format(position.Z)
				+ ") velocity (" + Format(velocity.X) + ", " + Format(velocity.Y) + ", " + Format(velocity.Z)
				+ ") grounded " + (grounded ? "True" : "False")
				+ " spins " + (spinsCounterClockwise ? "counter-clockwise" : "clockwise")
				+ " about Y; camera " + cameraState + ".");
		}

		private static string Format(float value) =>
			value.ToString("0.00", System.Globalization.CultureInfo.InvariantCulture);

		public override void OnDestroy()
		{
			Debug.Log("CubeController: OnDestroy");
		}

		/// <summary>
		/// Reads the demo's translation catalog: the locale the machine is set to
		/// first, English as the fallback, and - when neither file exists - the
		/// keys themselves, which is what keeps the interface readable while a
		/// catalog is still being written.
		/// </summary>
		private static void LoadLocalisation()
		{
			I18N.FallbackLocale = "en-US";
			I18N.Locale = I18N.SystemLocale;
			if (string.IsNullOrEmpty(I18N.Locale))
			{
				I18N.Locale = I18N.FallbackLocale;
			}

			bool loaded = I18N.LoadDefaultCatalog();
			Debug.Log("CubeController: locale '" + I18N.Locale + "' with " + I18N.EntryCount
				+ " translation(s)" + (loaded ? "." : " (no catalog file; the keys are shown as they are)."));
		}
	}
}
