using System;
using System.Collections.Generic;
using System.Numerics;
using System.Text.Json;

namespace VspEngine
{
	/// <summary>
	/// One object's coordinates, in both spaces a transform lives in.
	///
	/// LOCAL is where the object sits relative to its parent - the values a scene
	/// file stores and a loader restores.
	/// WORLD is where the object ends up once the whole parent chain has been
	/// applied - the values a renderer and a camera read.
	/// </summary>
	public readonly struct TransformRecord
	{
		/// <summary>Position relative to the parent.</summary>
		public readonly Vector3 LocalPosition;

		/// <summary>Euler rotation relative to the parent, in degrees.</summary>
		public readonly Vector3 LocalEulerAngles;

		/// <summary>Scale relative to the parent.</summary>
		public readonly Vector3 LocalScale;

		/// <summary>Position in the scene.</summary>
		public readonly Vector3 WorldPosition;

		/// <summary>Euler rotation in the scene, in degrees.</summary>
		public readonly Vector3 WorldEulerAngles;

		/// <summary>Scale in the scene, with every parent's scale folded in.</summary>
		public readonly Vector3 WorldScale;

		/// <summary>Takes a point from local space into the scene.</summary>
		public readonly Matrix4x4 LocalToWorld;

		public TransformRecord(
			Vector3 localPosition, Vector3 localEulerAngles, Vector3 localScale,
			Vector3 worldPosition, Vector3 worldEulerAngles, Vector3 worldScale,
			Matrix4x4 localToWorld)
		{
			LocalPosition = localPosition;
			LocalEulerAngles = localEulerAngles;
			LocalScale = localScale;
			WorldPosition = worldPosition;
			WorldEulerAngles = worldEulerAngles;
			WorldScale = worldScale;
			LocalToWorld = localToWorld;
		}

		/// <summary>Reads both spaces off a live transform.</summary>
		public static TransformRecord FromTransform(Transform transform)
		{
			return new TransformRecord(
				transform.LocalPosition, transform.LocalEulerAngles, transform.LocalScale,
				transform.Position, transform.EulerAngles, transform.LossyScale,
				transform.LocalToWorldMatrix);
		}
	}

	/// <summary>One object of a serialized scene.</summary>
	public sealed class SceneObjectRecord
	{
		public string Name { get; set; } = string.Empty;
		public bool ActiveSelf { get; set; } = true;
		public int Layer { get; set; }

		/// <summary>Handle of the object's parent, or 0 for a scene root.</summary>
		public uint ParentGameObjectHandle { get; set; }

		/// <summary>The coordinates the object is stored with, in both spaces.</summary>
		public TransformRecord Transform { get; set; }
	}

	/// <summary>A whole scene, as it was when it was recorded.</summary>
	public sealed class SceneRecord
	{
		public const int CurrentVersion = 1;

		public int Version { get; set; } = CurrentVersion;
		public List<SceneObjectRecord> Objects { get; } = new List<SceneObjectRecord>();
	}

	/// <summary>
	/// Writes the scene to JSON and reads it back.
	///
	/// A scene file stores the LOCAL coordinates - that is what a transform is
	/// defined by - and records the WORLD coordinates next to them, so a file
	/// says both where an object sits relative to its parent and where that put
	/// it in the scene, which is what a tool or a diff needs to read.
	///
	/// Only the local values are restored: a world position is derived, so
	/// writing it back would fight the parent chain that produces it.
	/// </summary>
	public static class SceneSerializer
	{
		/// <summary>Records the live scene.</summary>
		public static SceneRecord CaptureScene()
		{
			SceneRecord record = new SceneRecord();

			uint gameObjectCount = NativeApi.VspScene_GetLiveGameObjectCount();
			for (uint gameObjectIndex = 0; gameObjectIndex < gameObjectCount; ++gameObjectIndex)
			{
				uint gameObjectHandle = NativeApi.VspScene_GetGameObjectHandle(gameObjectIndex);
				if (gameObjectHandle == 0)
				{
					continue;
				}

				GameObject gameObject = new GameObject(gameObjectHandle);
				Transform? transform = gameObject.HasValidTransform
					? new Transform(NativeApi.VspGameObject_GetTransform(gameObjectHandle))
					: null;

				SceneObjectRecord objectRecord = new SceneObjectRecord
				{
					Name = gameObject.Name,
					ActiveSelf = gameObject.ActiveSelf,
					Layer = gameObject.Layer,
					ParentGameObjectHandle = gameObject.Parent?.NativeHandle ?? 0,
				};

				if (transform != null)
				{
					objectRecord.Transform = TransformRecord.FromTransform(transform);
				}

				record.Objects.Add(objectRecord);
			}

			return record;
		}

		/// <summary>Renders the scene as indented JSON.</summary>
		public static string SerializeScene()
		{
			return ToJson(CaptureScene());
		}

		/// <summary>Saves the scene to a file. Returns false (with the reason) on failure.</summary>
		public static bool SaveScene(string filePath, out string errorText)
		{
			errorText = string.Empty;
			if (string.IsNullOrEmpty(filePath))
			{
				errorText = "the scene file path is empty";
				return false;
			}

			try
			{
				System.IO.File.WriteAllText(filePath, SerializeScene());
				return true;
			}
			catch (Exception exception)
			{
				errorText = "cannot write the scene file '" + filePath + "': " + exception.Message;
				return false;
			}
		}

		/// <summary>Renders one recorded scene as JSON.</summary>
		public static string ToJson(SceneRecord record)
		{
			JsonWriterOptions writerOptions = new JsonWriterOptions { Indented = true };
			using System.IO.MemoryStream stream = new System.IO.MemoryStream();
			using (Utf8JsonWriter writer = new Utf8JsonWriter(stream, writerOptions))
			{
				writer.WriteStartObject();
				writer.WriteNumber("version", record.Version);
				writer.WriteNumber("objectCount", record.Objects.Count);
				writer.WriteStartArray("objects");

				foreach (SceneObjectRecord objectRecord in record.Objects)
				{
					writer.WriteStartObject();
					writer.WriteString("name", objectRecord.Name);
					writer.WriteBoolean("activeSelf", objectRecord.ActiveSelf);
					writer.WriteNumber("layer", objectRecord.Layer);
					writer.WriteNumber("parentGameObjectHandle", objectRecord.ParentGameObjectHandle);

					TransformRecord transform = objectRecord.Transform;
					WriteVector(writer, "localPosition", transform.LocalPosition);
					WriteVector(writer, "localRotation", transform.LocalEulerAngles);
					WriteVector(writer, "localScale", transform.LocalScale);
					WriteVector(writer, "worldPosition", transform.WorldPosition);
					WriteVector(writer, "worldRotation", transform.WorldEulerAngles);
					WriteVector(writer, "worldScale", transform.WorldScale);

					writer.WriteStartArray("localToWorld");
					WriteMatrixRows(writer, transform.LocalToWorld);
					writer.WriteEndArray();

					writer.WriteEndObject();
				}

				writer.WriteEndArray();
				writer.WriteEndObject();
			}

			return System.Text.Encoding.UTF8.GetString(stream.ToArray());
		}

		/// <summary>Reads a scene document.</summary>
		public static bool TryParseScene(string json, out SceneRecord record, out string errorText)
		{
			record = new SceneRecord();
			errorText = string.Empty;

			if (string.IsNullOrEmpty(json))
			{
				errorText = "the scene document is empty";
				return false;
			}

			try
			{
				using JsonDocument document = JsonDocument.Parse(json);
				JsonElement root = document.RootElement;

				if (root.ValueKind != JsonValueKind.Object)
				{
					errorText = "the scene document is not a JSON object";
					return false;
				}

				if (root.TryGetProperty("version", out JsonElement versionElement) &&
					versionElement.ValueKind == JsonValueKind.Number)
				{
					record.Version = versionElement.GetInt32();
				}
				if (record.Version != SceneRecord.CurrentVersion)
				{
					errorText = "the scene document has version " + record.Version +
						", but this engine reads version " + SceneRecord.CurrentVersion;
					return false;
				}

				if (!root.TryGetProperty("objects", out JsonElement objectsElement) ||
					objectsElement.ValueKind != JsonValueKind.Array)
				{
					errorText = "the scene document lists no objects";
					return false;
				}

				foreach (JsonElement objectElement in objectsElement.EnumerateArray())
				{
					SceneObjectRecord objectRecord = new SceneObjectRecord();
					objectRecord.Name = ReadString(objectElement, "name");
					objectRecord.ActiveSelf = ReadBoolean(objectElement, "activeSelf", true);
					objectRecord.Layer = ReadInt(objectElement, "layer", 0);
					objectRecord.ParentGameObjectHandle = (uint)ReadInt(objectElement, "parentGameObjectHandle", 0);

					Vector3 localPosition = ReadVector(objectElement, "localPosition");
					Vector3 localRotation = ReadVector(objectElement, "localRotation");
					Vector3 localScale = ReadVector(objectElement, "localScale", Vector3.One);

					objectRecord.Transform = new TransformRecord(
						localPosition, localRotation, localScale,
						ReadVector(objectElement, "worldPosition"),
						ReadVector(objectElement, "worldRotation"),
						ReadVector(objectElement, "worldScale", Vector3.One),
						Matrix4x4.Identity);

					record.Objects.Add(objectRecord);
				}

				return true;
			}
			catch (JsonException exception)
			{
				errorText = "the scene document is not valid JSON: " + exception.Message;
				return false;
			}
		}

		/// <summary>Reads a scene file.</summary>
		public static bool TryLoadScene(string filePath, out SceneRecord record, out string errorText)
		{
			record = new SceneRecord();
			errorText = string.Empty;

			try
			{
				return TryParseScene(System.IO.File.ReadAllText(filePath), out record, out errorText);
			}
			catch (Exception exception)
			{
				errorText = "cannot read the scene file '" + filePath + "': " + exception.Message;
				return false;
			}
		}

		/// <summary>Destroys every game object of the live scene.</summary>
		public static void DestroyAllGameObjects()
		{
			// The handles are collected first: destroying while walking the scene
			// would move the objects under the walk.
			List<uint> gameObjectHandles = new List<uint>();
			uint gameObjectCount = NativeApi.VspScene_GetLiveGameObjectCount();
			for (uint gameObjectIndex = 0; gameObjectIndex < gameObjectCount; ++gameObjectIndex)
			{
				uint gameObjectHandle = NativeApi.VspScene_GetGameObjectHandle(gameObjectIndex);
				if (gameObjectHandle != 0)
				{
					gameObjectHandles.Add(gameObjectHandle);
				}
			}

			foreach (uint gameObjectHandle in gameObjectHandles)
			{
				NativeApi.VspGameObject_Destroy(gameObjectHandle);
			}
		}

		/// <summary>
		/// Restores a recorded scene: every object is created again with the LOCAL
		/// coordinates the file stored, which is what makes the world coordinates
		/// come out the same.
		/// </summary>
		public static bool RestoreScene(SceneRecord record, out string errorText) =>
			RestoreScene(record, false, out errorText);

		/// <summary>
		/// Restores a recorded scene, optionally emptying the live scene first.
		/// Objects are created in the order the file lists them, so a parent is
		/// there before the objects parented to it.
		/// </summary>
		public static bool RestoreScene(SceneRecord record, bool clearExistingScene, out string errorText)
		{
			errorText = string.Empty;
			if (record == null)
			{
				errorText = "there is no scene to restore";
				return false;
			}

			if (clearExistingScene)
			{
				DestroyAllGameObjects();
			}

			List<GameObject> createdObjects = new List<GameObject>();
			foreach (SceneObjectRecord objectRecord in record.Objects)
			{
				GameObject? gameObject = GameObject.Create(
					string.IsNullOrEmpty(objectRecord.Name) ? "GameObject" : objectRecord.Name);
				if (gameObject == null)
				{
					errorText = "the scene refused an object for '" + objectRecord.Name + "'";
					return false;
				}

				gameObject.ActiveSelf = objectRecord.ActiveSelf;
				gameObject.Layer = objectRecord.Layer;

				Transform transform = gameObject.Transform;
				transform.LocalPosition = objectRecord.Transform.LocalPosition;
				transform.LocalEulerAngles = objectRecord.Transform.LocalEulerAngles;
				transform.LocalScale = objectRecord.Transform.LocalScale;

				createdObjects.Add(gameObject);
			}

			return true;
		}

		// -----------------------------------------------------------------
		// JSON helpers
		// -----------------------------------------------------------------

		private static void WriteVector(Utf8JsonWriter writer, string name, Vector3 value)
		{
			writer.WriteStartObject(name);
			writer.WriteNumber("x", value.X);
			writer.WriteNumber("y", value.Y);
			writer.WriteNumber("z", value.Z);
			writer.WriteEndObject();
		}

		private static void WriteMatrixRows(Utf8JsonWriter writer, Matrix4x4 matrix)
		{
			// One array per row, which is how a reader expects to see a matrix.
			WriteRow(writer, matrix.M11, matrix.M12, matrix.M13, matrix.M14);
			WriteRow(writer, matrix.M21, matrix.M22, matrix.M23, matrix.M24);
			WriteRow(writer, matrix.M31, matrix.M32, matrix.M33, matrix.M34);
			WriteRow(writer, matrix.M41, matrix.M42, matrix.M43, matrix.M44);
		}

		private static void WriteRow(Utf8JsonWriter writer, float value0, float value1, float value2, float value3)
		{
			writer.WriteStartArray();
			writer.WriteNumberValue(value0);
			writer.WriteNumberValue(value1);
			writer.WriteNumberValue(value2);
			writer.WriteNumberValue(value3);
			writer.WriteEndArray();
		}

		private static string ReadString(JsonElement element, string name) =>
			element.TryGetProperty(name, out JsonElement value) && value.ValueKind == JsonValueKind.String
				? value.GetString() ?? string.Empty
				: string.Empty;

		private static bool ReadBoolean(JsonElement element, string name, bool fallback) =>
			element.TryGetProperty(name, out JsonElement value) &&
			(value.ValueKind == JsonValueKind.True || value.ValueKind == JsonValueKind.False)
				? value.GetBoolean()
				: fallback;

		private static int ReadInt(JsonElement element, string name, int fallback) =>
			element.TryGetProperty(name, out JsonElement value) && value.ValueKind == JsonValueKind.Number
				? value.GetInt32()
				: fallback;

		private static Vector3 ReadVector(JsonElement element, string name) =>
			ReadVector(element, name, Vector3.Zero);

		private static Vector3 ReadVector(JsonElement element, string name, Vector3 fallback)
		{
			if (!element.TryGetProperty(name, out JsonElement value) || value.ValueKind != JsonValueKind.Object)
			{
				return fallback;
			}
			return new Vector3(
				ReadFloat(value, "x"),
				ReadFloat(value, "y"),
				ReadFloat(value, "z"));
		}

		private static float ReadFloat(JsonElement element, string name) =>
			element.TryGetProperty(name, out JsonElement value) && value.ValueKind == JsonValueKind.Number
				? value.GetSingle()
				: 0.0f;
	}
}
