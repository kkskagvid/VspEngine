using System.Numerics;

using Assembly.Rendering;

using VspEngine;

namespace Assembly
{
	/// <summary>
	/// Acceptance demo living in the game Assembly (Assembly.dll): it drives the
	/// lit cube the render pipeline draws.
	///
	///   W / A / S / D   move the cube over the ground, relative to the camera
	///   Mouse           orbit the camera around the cube (it always looks at it)
	///   Mouse wheel     move the camera closer to / further from the cube
	///   T               flip the spin: clockwise about Y, or counter-clockwise
	///   R               send the cube back to the origin
	///
	/// The cube spins about its own Y axis from the first frame on; T only
	/// decides WHICH WAY. The camera is a third-person orbit: it sits 45 degrees
	/// above the horizon behind the cube and follows it, so the cube is centred
	/// whatever the pointer does.
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

		/// <summary>Where the cube sits, in world coordinates.</summary>
		private Vector3 position;

		/// <summary>How far the cube has turned about Y, in degrees.</summary>
		private float spinDegrees;

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

		/// <summary>Current spin angle, in degrees.</summary>
		public static float SpinDegrees { get; private set; }

		/// <summary>Flips the spin direction. Wired to both T and the HUD button.</summary>
		public static void ToggleSpinDirection()
		{
			spinsCounterClockwise = !spinsCounterClockwise;
		}

		/// <summary>Asks the controller to send the cube home. Wired to the HUD button.</summary>
		public static void RequestReset()
		{
			resetRequested = true;
		}

		/// <summary>Forgets the static state; called when the instance starts.</summary>
		public override void OnInit()
		{
			spinsCounterClockwise = DefaultSpinsCounterClockwise;
			resetRequested = false;
			position = Vector3.Zero;
			spinDegrees = 0.0f;

			Transform.Position = position;
			Transform.LocalEulerAngles = new Vector3(0.0f, spinDegrees, 0.0f);

			Debug.Log("CubeController: OnInit (InstanceID = " + InstanceID
				+ ", component handle = " + NativeHandle + ")");
		}

		public override void OnStart()
		{
			Debug.Log("CubeController: OnStart - W/A/S/D move, mouse orbits the camera, T flips the spin, R resets");

			// The interface is what carries every static string of the demo, in
			// the locale the machine is set to. Loading before the first frame is
			// what keeps a screenshot free of untranslated keys.
			LoadLocalisation();
		}

		public override void OnUpdate()
		{
			float deltaTime = Time.DeltaTime;

			// -------- Spin --------
			// The angle grows counter-clockwise about +Y and shrinks clockwise,
			// which is what the right-hand rule says about a positive rotation.
			float spinDirection = spinsCounterClockwise ? 1.0f : -1.0f;
			spinDegrees += SpinSpeedDegreesPerSecond * spinDirection * deltaTime;
			SpinDegrees = spinDegrees;

			// -------- Movement --------
			// W/A/S/D move the cube in the CAMERA's frame, which is what makes
			// "forward" mean "away from the viewer" however the camera is turned.
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

			if (movement.LengthSquared() > 0.0f)
			{
				position += NativeMath.Normalize(movement) * (MoveSpeed * deltaTime);
			}

			// -------- Keys --------
			if (Input.GetKeyDown(KeyCode.T))
			{
				ToggleSpinDirection();
				Debug.Log("CubeController: spin -> " + (spinsCounterClockwise ? "counter-clockwise" : "clockwise") + " about Y");
			}

			if (Input.GetKeyDown(KeyCode.R) || resetRequested)
			{
				resetRequested = false;
				position = Vector3.Zero;
				spinDegrees = 0.0f;
				SpinDegrees = 0.0f;
				Debug.Log("CubeController: reset to the origin");
			}

			// -------- Camera --------
			// The pointer turns the ORBIT, never a free look: the rig keeps the
			// cube centred, which is the whole definition of a follow camera.
			if (rig != null)
			{
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

				rig.SetTarget(position);
			}

			// -------- Publish --------
			Transform.Position = position;
			Transform.LocalEulerAngles = new Vector3(0.0f, spinDegrees, 0.0f);

			ReportStatePeriodically(deltaTime, rig);
		}

		/// <summary>
		/// Writes one state line per second of engine time: the cube's world
		/// position, the way it spins and where the camera is looking. An
		/// automated run reads these lines to check that the input did what it
		/// was supposed to without having to interpret pixels.
		/// </summary>
		private void ReportStatePeriodically(float deltaTime, ThirdPersonCameraRig? rig)
		{
			secondsSinceStateLine += deltaTime;
			if (secondsSinceStateLine < StateLineIntervalSeconds)
			{
				return;
			}
			secondsSinceStateLine -= StateLineIntervalSeconds;

			string cameraState = rig != null
				? "yaw " + Format(rig.YawDegrees) + " pitch " + Format(rig.PitchDegrees)
					+ " distance " + Format(rig.Distance)
				: "no rig";

			Debug.Log("CubeController: cube (" + Format(position.X) + ", " + Format(position.Y) + ", " + Format(position.Z)
				+ ") spins " + (spinsCounterClockwise ? "counter-clockwise" : "clockwise")
				+ " about Y at " + Format(spinDegrees) + " degrees; camera " + cameraState + ".");
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
