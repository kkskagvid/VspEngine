using System.Numerics;

using VspEngine;

namespace Assembly.Rendering
{
	/// <summary>
	/// A handful of KNOWN-ANSWER checks of the engine's collision code, run once
	/// when the demo starts.
	///
	/// The demo itself uses a box collider on the cube and a mesh collider on the
	/// ground, which leaves the other shapes - a sphere, a box turned away from the
	/// world axes - and the rays that are not aimed at the ground unexercised. They
	/// are exactly the parts an error would hide in, so they are asked questions
	/// whose answers are arithmetic:
	///
	///   a sphere of radius 1 at the origin, from 5 units away    -> hit at 4
	///   a box 2 units wide at the origin, from 5 units above it   -> hit at 4
	///   the same box turned 45 degrees about Y                    -> hit at 5 - 0.707
	///   a triangle in the z = 0 plane, from 5 units in front of it -> hit at 5
	///
	/// Every check is a <see cref="Physics.Raycast"/> against a scratch object that
	/// is destroyed again, so the demo's own scene is never touched and nothing is
	/// left behind. A failure is reported like any other problem - with the numbers
	/// it expected and the numbers it got - and the run's log check catches it.
	/// </summary>
	public static class PhysicsSelfTest
	{
		/// <summary>How far off an answer may be and still count as the expected one.</summary>
		private const float Tolerance = 0.01f;

		/// <summary>
		/// Where the scratch object stands. Far from the origin, so the rays below
		/// cannot meet the demo's own cube or ground plate on their way - the
		/// question each check asks has exactly one thing in the way, which is what
		/// makes its answer arithmetic.
		/// </summary>
		private static readonly Vector3 ProbePosition = new Vector3(10.0f, 0.0f, 0.0f);

		/// <summary>Runs every check and reports the result. True when all of them passed.</summary>
		public static bool Run()
		{
			int passedCount = 0;
			int checkCount = 0;

			passedCount += CheckSphereRay() ? 1 : 0;
			++checkCount;
			passedCount += CheckBoxRay() ? 1 : 0;
			++checkCount;
			passedCount += CheckRotatedBoxRay() ? 1 : 0;
			++checkCount;
			passedCount += CheckMeshRay() ? 1 : 0;
			++checkCount;

			if (passedCount != checkCount)
			{
				Debug.LogError("PhysicsSelfTest: only " + passedCount + " of " + checkCount
					+ " collision checks passed; the answers above say which one did not.");
				return false;
			}

			Debug.Log("PhysicsSelfTest: " + passedCount + " of " + checkCount
				+ " collision checks passed (sphere, box, rotated box and mesh rays).");
			return true;
		}

		/// <summary>A ray into a sphere stops one radius before its centre.</summary>
		private static bool CheckSphereRay()
		{
			GameObject? probe = CreateProbe(Vector3.Zero);
			if (probe == null)
			{
				return false;
			}

			SphereCollider? collider = SphereCollider.Create(probe);
			if (collider == null)
			{
				probe.Destroy();
				return false;
			}
			collider.Radius = 1.0f;

			bool passed = Check("sphere ray", ProbePosition + new Vector3(-5.0f, 0.0f, 0.0f), Vector3.UnitX, 4.0f, new Vector3(-1.0f, 0.0f, 0.0f));
			probe.Destroy();
			return passed;
		}

		/// <summary>A ray straight down onto a box stops at the box's top face.</summary>
		private static bool CheckBoxRay()
		{
			GameObject? probe = CreateProbe(Vector3.Zero);
			if (probe == null)
			{
				return false;
			}

			BoxCollider? collider = BoxCollider.Create(probe);
			if (collider == null)
			{
				probe.Destroy();
				return false;
			}
			collider.Size = new Vector3(2.0f, 2.0f, 2.0f);

			bool passed = Check("box ray", ProbePosition + new Vector3(0.0f, 5.0f, 0.0f), -Vector3.UnitY, 4.0f, Vector3.UnitY);
			probe.Destroy();
			return passed;
		}

		/// <summary>
		/// A box turned 45 degrees about Y reaches only 0.707 of its half width
		/// towards a ray along X, so the same ray stops further away - which is what
		/// proves the shape really is an ORIENTED box and not just an axis-aligned
		/// one that happens to sit square to the world.
		/// </summary>
		private static bool CheckRotatedBoxRay()
		{
			GameObject? probe = CreateProbe(new Vector3(0.0f, 45.0f, 0.0f));
			if (probe == null)
			{
				return false;
			}

			BoxCollider? collider = BoxCollider.Create(probe);
			if (collider == null)
			{
				probe.Destroy();
				return false;
			}
			collider.Size = new Vector3(1.0f, 1.0f, 1.0f);

			// (cos 45 + sin 45) * 0.5 = 0.7071..., which is how far the turned
			// box's corner reaches along X.
			const float halfDiagonal = 0.70710678f;
			float expectedDistance = 5.0f - halfDiagonal;

			// The face the ray meets is turned 45 degrees as well, so its normal is
			// (-cos 45, 0, sin 45): a box that answered (1, 0, 0) here would be an
			// axis-aligned one wearing a rotation it never applied.
			Vector3 expectedNormal = new Vector3(-halfDiagonal, 0.0f, halfDiagonal);
			bool passed = Check("rotated box ray", ProbePosition + new Vector3(-5.0f, 0.0f, 0.0f), Vector3.UnitX, expectedDistance, expectedNormal);
			probe.Destroy();
			return passed;
		}

		/// <summary>A ray into a triangle mesh stops on the triangle it crosses.</summary>
		private static bool CheckMeshRay()
		{
			GameObject? probe = CreateProbe(Vector3.Zero);
			if (probe == null)
			{
				return false;
			}

			MeshCollider? collider = MeshCollider.Create(probe);
			if (collider == null)
			{
				probe.Destroy();
				return false;
			}

			// One triangle in the z = 0 plane, facing +Z.
			Vector3[] vertices =
			{
				new Vector3(-1.0f, -1.0f, 0.0f),
				new Vector3(1.0f, -1.0f, 0.0f),
				new Vector3(0.0f, 1.0f, 0.0f),
			};
			if (!collider.SetMesh(vertices, new uint[] { 0, 1, 2 }))
			{
				Debug.LogError("PhysicsSelfTest: the mesh collider refused a triangle it was given.");
				probe.Destroy();
				return false;
			}

			bool passed = Check("mesh ray", ProbePosition + new Vector3(0.0f, 0.0f, 5.0f), -Vector3.UnitZ, 5.0f, Vector3.UnitZ);
			probe.Destroy();
			return passed;
		}

		/// <summary>Creates the scratch object a check puts its collider on.</summary>
		private static GameObject? CreateProbe(Vector3 eulerDegrees)
		{
			GameObject? probe = GameObject.Create("PhysicsProbe");
			if (probe == null)
			{
				Debug.LogError("PhysicsSelfTest: the scratch object could not be created.");
				return null;
			}

			probe.Transform.Position = ProbePosition;
			probe.Transform.LocalEulerAngles = eulerDegrees;
			return probe;
		}

		/// <summary>
		/// Fires one ray and compares the answer with what the shape says it has to
		/// be. The distance and the surface normal are both checked: a hit at the
		/// right distance with the wrong normal is a shape that is somewhere else
		/// than it looks.
		/// </summary>
		private static bool Check(string label, Vector3 origin, Vector3 direction, float expectedDistance, Vector3 expectedNormal)
		{
			RaycastResult hit = Physics.Raycast(origin, direction, 100.0f);
			if (!hit.HasHit)
			{
				Debug.LogError("PhysicsSelfTest: the " + label + " hit nothing where it had to hit at "
					+ Format(expectedDistance) + ".");
				return false;
			}

			bool distanceMatches = System.MathF.Abs(hit.Distance - expectedDistance) <= Tolerance;
			bool normalMatches = (hit.Normal - expectedNormal).Length() <= Tolerance;
			if (!distanceMatches || !normalMatches)
			{
				Debug.LogError("PhysicsSelfTest: the " + label + " stopped at " + Format(hit.Distance)
					+ " facing (" + Format(hit.Normal.X) + ", " + Format(hit.Normal.Y) + ", " + Format(hit.Normal.Z)
					+ ") where it had to stop at " + Format(expectedDistance)
					+ " facing (" + Format(expectedNormal.X) + ", " + Format(expectedNormal.Y) + ", " + Format(expectedNormal.Z) + ").");
				return false;
			}

			return true;
		}

		private static string Format(float value) =>
			value.ToString("0.000", System.Globalization.CultureInfo.InvariantCulture);
	}
}
