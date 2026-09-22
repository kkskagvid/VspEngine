using System.Numerics;

using Assembly.Rendering;

using VspEngine;

namespace Assembly
{
	/// <summary>
	/// Acceptance demo living in the game Assembly (Assembly.dll): drives the
	/// demo scene from script.
	///
	///   W / A / S / D   move the colored triangle (world X / Y)
	///   Q / E           move it away from / towards the camera (world Z)
	///   R               reset its position
	///   T               cycle its color (red -> blue -> green -> multicolor)
	///   Z / X           send it behind the cube / bring it back (depth test)
	///   C               cycle the camera (orthographic -> perspective -> physical)
	///   F5 / F9         save the scene / restore it from the file
	///
	/// The script owns no rendering state of its own: the transform lives in the
	/// native scene (local coordinates, with the world coordinates derived from
	/// them) and the color is a property of the material the component draws
	/// with - which is what the game's render pipeline reads while it builds the
	/// frame.
	/// </summary>
	public sealed class TriangleController : ScriptBehaviour
	{
		private const float MoveSpeed = 0.9f;
		private const float DepthSpeed = 1.5f;

		/// <summary>Where the triangle sits this frame, in world coordinates.</summary>
		private Vector3 position;

		/// <summary>Z the triangle starts at (0 = the cube's plane of the scene).</summary>
		private const float StartZ = 0.0f;

		/// <summary>Z that puts the triangle behind the cube.</summary>
		private const float BehindCubeZ = -TriangleRenderPipeline.CubeDistance - 0.6f;

		/// <summary>File the scene is saved to, next to the executable.</summary>
		public const string SceneFilePath = "DemoScene.json";

		private int colorCycle = (int)TriangleColorMode.MultiColor;

		public override void OnInit()
		{
			position = new Vector3(0.0f, 0.0f, StartZ);

			Transform.Position = position;

			// The component has no material until the pipeline's first frame, so
			// the color it starts with is the shader's own default (multicolor).
			TriangleMaterial.SetColorMode(this, (TriangleColorMode)colorCycle);

			Debug.Log("TriangleController: OnInit (InstanceID = " + InstanceID
				+ ", component handle = " + NativeHandle + ")");
			LogMaterialState();
		}

		public override void OnStart()
		{
			Debug.Log("TriangleController: OnStart - W/A/S/D move, Q/E depth, R reset, T color, Z/X depth test, C camera, F5 save, F9 restore");
			LogTransformSpaces();
		}

		public override void OnUpdate()
		{
			float deltaTime = Time.DeltaTime;

			if (Input.GetKey(KeyCode.W)) position.Y += MoveSpeed * deltaTime;
			if (Input.GetKey(KeyCode.A)) position.X -= MoveSpeed * deltaTime;
			if (Input.GetKey(KeyCode.S)) position.Y -= MoveSpeed * deltaTime;
			if (Input.GetKey(KeyCode.D)) position.X += MoveSpeed * deltaTime;
			if (Input.GetKey(KeyCode.E)) position.Z += DepthSpeed * deltaTime;   // towards the camera
			if (Input.GetKey(KeyCode.Q)) position.Z -= DepthSpeed * deltaTime;   // away from the camera

			if (Input.GetKeyDown(KeyCode.R))
			{
				position = new Vector3(0.0f, 0.0f, StartZ);
			}

			if (Input.GetKeyDown(KeyCode.T))
			{
				colorCycle = (colorCycle + 1) % 4;
				TriangleMaterial.SetColorMode(this, (TriangleColorMode)colorCycle);
				Debug.Log("TriangleController: color mode -> " + (TriangleColorMode)colorCycle
					+ " (material " + TriangleMaterial.ColorModePropertyName + " = "
					+ (int)TriangleMaterial.GetColorMode(this) + ")");
			}

			// Depth test: behind the cube the triangle has to disappear, which
			// only happens when the depth buffer is doing its job.
			if (Input.GetKeyDown(KeyCode.Z))
			{
				position.Z = BehindCubeZ;
				Debug.Log("TriangleController: moved behind the cube (z = " + position.Z + ")");
			}

			if (Input.GetKeyDown(KeyCode.X))
			{
				position.Z = StartZ;
				Debug.Log("TriangleController: back in front of the cube (z = " + position.Z + ")");
			}

			InputManager_CycleCamera();

			if (Input.GetKeyDown(KeyCode.F5))
			{
				SaveScene();
			}

			if (Input.GetKeyDown(KeyCode.F9))
			{
				RestoreScene();
			}

			Transform.Position = position;
		}

		public override void OnDestroy()
		{
			Debug.Log("TriangleController: OnDestroy");
		}

		/// <summary>
		/// Cycles the demo camera through its three projection modes, which is
		/// what proves the camera is doing the projecting: the same scene, three
		/// projections.
		/// </summary>
		private void InputManager_CycleCamera()
		{
			if (!Input.GetKeyDown(KeyCode.C))
			{
				return;
			}

			Camera? camera = FindCamera();
			if (camera == null)
			{
				return;
			}

			switch (camera.ProjectionMode)
			{
			case CameraProjectionMode.Orthographic:
				camera.ProjectionMode = CameraProjectionMode.Perspective;
				Debug.Log("TriangleController: camera -> perspective (" + camera.FieldOfView + " degrees)");
				break;

			case CameraProjectionMode.Perspective:
				camera.ProjectionMode = CameraProjectionMode.Physical;
				Debug.Log("TriangleController: camera -> physical (" + camera.FocalLength + " mm on a "
					+ camera.SensorWidth + "x" + camera.SensorHeight + " mm sensor, "
					+ camera.EffectiveFieldOfView + " degrees, f/" + camera.Aperture + ")");
				break;

			default:
				camera.ProjectionMode = CameraProjectionMode.Orthographic;
				Debug.Log("TriangleController: camera -> orthographic (size " + camera.OrthographicSize + ")");
				break;
			}
		}

		private static Camera? FindCamera() => Camera.GetMainCamera();

		/// <summary>
		/// Reports the two coordinate spaces of this object: where it sits
		/// relative to its parent, and where that put it in the scene.
		/// </summary>
		private void LogTransformSpaces()
		{
			Transform transform = Transform;
			Vector3 localPosition = transform.LocalPosition;
			Vector3 worldPosition = transform.Position;

			Debug.Log("TriangleController: local (" + localPosition.X + ", " + localPosition.Y + ", " + localPosition.Z
				+ ") -> world (" + worldPosition.X + ", " + worldPosition.Y + ", " + worldPosition.Z + ")");
		}

		/// <summary>
		/// Records the whole scene - every object's LOCAL coordinates and the
		/// WORLD coordinates they produce - into a JSON file next to the
		/// executable.
		/// </summary>
		private void SaveScene()
		{
			string scenePath = Application.GetExecutableFilePath(SceneFilePath);

			SceneRecord record = SceneSerializer.CaptureScene();
			if (!SceneSerializer.SaveScene(scenePath, out string saveError))
			{
				Debug.LogError("TriangleController: " + saveError);
				return;
			}

			Debug.Log("TriangleController: saved " + record.Objects.Count + " object(s) to " + SceneFilePath + ".");
			foreach (SceneObjectRecord objectRecord in record.Objects)
			{
				Vector3 localPosition = objectRecord.Transform.LocalPosition;
				Vector3 worldPosition = objectRecord.Transform.WorldPosition;
				Debug.Log("TriangleController:   '" + objectRecord.Name
					+ "' local (" + localPosition.X + ", " + localPosition.Y + ", " + localPosition.Z
					+ ") world (" + worldPosition.X + ", " + worldPosition.Y + ", " + worldPosition.Z + ")");
			}
		}

		/// <summary>
		/// Reads the scene file back and reports what it holds. The file is the
		/// record of both coordinate spaces; reading it proves the round trip is
		/// lossless, and the log then says what was in it.
		/// </summary>
		private void RestoreScene()
		{
			string scenePath = Application.GetExecutableFilePath(SceneFilePath);

			if (!SceneSerializer.TryLoadScene(scenePath, out SceneRecord record, out string loadError))
			{
				Debug.LogError("TriangleController: " + loadError);
				return;
			}

			int objectCount = 0;
			foreach (SceneObjectRecord objectRecord in record.Objects)
			{
				Vector3 localPosition = objectRecord.Transform.LocalPosition;
				Vector3 worldPosition = objectRecord.Transform.WorldPosition;
				Debug.Log("TriangleController: restored '" + objectRecord.Name
					+ "' local (" + localPosition.X + ", " + localPosition.Y + ", " + localPosition.Z
					+ ") world (" + worldPosition.X + ", " + worldPosition.Y + ", " + worldPosition.Z + ")");
				++objectCount;
			}

			Debug.Log("TriangleController: scene file holds " + objectCount + " object(s), version "
				+ record.Version + ".");
		}

		/// <summary>
		/// Reports which shader the component's material draws with and which of
		/// the shader's variants it selects - the whole chain a draw goes through.
		/// A material only exists from the first frame on, so this may say so.
		/// </summary>
		private void LogMaterialState()
		{
			Material? material = Renderer.GetMaterial(this);
			if (material == null)
			{
				Debug.Log("TriangleController: no material yet; the render pipeline assigns one on the first frame.");
				return;
			}

			Shader? shader = material.Shader;
			if (shader == null)
			{
				Debug.Log("TriangleController: the material has no shader.");
				return;
			}

			Debug.Log("TriangleController: material draws with shader '" + shader.ShaderName
				+ "' (" + shader.VariantCount + " variant(s), " + shader.PropertyCount + " propert(ies), "
				+ shader.KeywordGroupCount + " keyword group(s)), variant " + material.ResolveVariantIndex() + ".");
		}
	}
}
