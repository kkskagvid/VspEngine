using System.Numerics;

using VspEngine;

namespace Assembly
{
	/// <summary>
	/// Acceptance demo living in the game Assembly (Assembly.dll): drives the
	/// colored triangle from script.
	/// W = move up, A = move left, S = move down, D = move right,
	/// R = reset position, T = cycle color (red -> blue -> green -> multicolor).
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
			Renderer.SetColorMode(InstanceID, (ColorMode)colorCycle);
			Debug.Log("TriangleController: OnInit (InstanceID = " + InstanceID + ")");
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
				Renderer.SetColorMode(InstanceID, (ColorMode)colorCycle);
			}

			Transform.Position = new Vector2(positionX, positionY);
		}

		public override void OnDestroy()
		{
			Debug.Log("TriangleController: OnDestroy");
		}
	}
}
