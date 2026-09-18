using System.Numerics;

using VspEngine;

namespace Assembly
{
	/// <summary>
	/// Acceptance demo living in the game Assembly (Assembly.dll): drives the
	/// colored triangle from script.
	/// W = move up, A = move left, S = move down, D = move right,
	/// R = reset position, T = cycle color (red -> blue -> green -> multicolor).
	///
	/// The script owns no rendering state of its own: the transform lives in the
	/// native scene, and the color is a property of the material the component
	/// draws with - which is what the render pipeline reads while it builds the
	/// frame.
	/// </summary>
	public sealed class TriangleController : ScriptBehaviour
	{
		private const float MoveSpeed = 0.9f;

		private float positionX;
		private float positionY;
		private int colorCycle = (int)ColorMode.MultiColor; // 0 red, 1 blue, 2 green, 3 multicolor

		public override void OnInit()
		{
			positionX = 0.0f;
			positionY = 0.0f;

			Transform.Position = Vector2.Zero;
			Renderer.SetColorMode(this, (ColorMode)colorCycle);

			Debug.Log("TriangleController: OnInit (InstanceID = " + InstanceID
				+ ", component handle = " + NativeHandle + ")");
			LogMaterialState();
		}

		public override void OnStart()
		{
			Debug.Log("TriangleController: OnStart - use W/A/S/D to move, R to reset, T to cycle color");
		}

		public override void OnUpdate()
		{
			float deltaTime = Time.DeltaTime;

			if (Input.GetKey(KeyCode.W)) positionY += MoveSpeed * deltaTime; // screen up
			if (Input.GetKey(KeyCode.A)) positionX -= MoveSpeed * deltaTime; // screen left
			if (Input.GetKey(KeyCode.S)) positionY -= MoveSpeed * deltaTime; // screen down
			if (Input.GetKey(KeyCode.D)) positionX += MoveSpeed * deltaTime; // screen right

			if (Input.GetKeyDown(KeyCode.R))
			{
				positionX = 0.0f;
				positionY = 0.0f;
			}

			if (Input.GetKeyDown(KeyCode.T))
			{
				colorCycle = (colorCycle + 1) % 4;
				Renderer.SetColorMode(this, (ColorMode)colorCycle);
				Debug.Log("TriangleController: color mode -> " + (ColorMode)colorCycle
					+ " (material _ColorMode = " + (int)Renderer.GetColorMode(this) + ")");
			}

			Transform.Position = new Vector2(positionX, positionY);
		}

		public override void OnDestroy()
		{
			Debug.Log("TriangleController: OnDestroy");
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
