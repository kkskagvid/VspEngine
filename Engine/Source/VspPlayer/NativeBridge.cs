using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;

namespace VspEngine
{
	public class NativeBridge
	{
		private static readonly Dictionary<int, ScriptBehaviour> instances = new Dictionary<int, ScriptBehaviour>();
		private static int nextId = 1; // 从 1 开始，0 表示无效
		private static readonly object lockObj = new object();

		// 创建指定类型实例（已有）
		public static int CreateInstance(string typeName)
		{
			Type type = Type.GetType(typeName);
			if (type == null || !typeof(ScriptBehaviour).IsAssignableFrom(type))
				return 0;

			ScriptBehaviour instance = (ScriptBehaviour)Activator.CreateInstance(type);

			lock (lockObj)
			{
				int id = nextId++;
				instances[id] = instance;
				return id;
			}
		}

		// 自动化创建所有脚本类型实例
		public static IntPtr CreateAllScripts(out int count)
		{
			// 获取当前程序集（或指定程序集）中所有继承 ScriptBehaviour 的公开类型
			Type baseType = typeof(ScriptBehaviour);
			var scriptTypes = Assembly.GetExecutingAssembly()
				.GetTypes()
				.Where(t => t.IsClass && !t.IsAbstract && baseType.IsAssignableFrom(t))
				.ToList();

			var ids = new List<int>();
			foreach (Type type in scriptTypes)
			{
				int id = CreateInstance(type.AssemblyQualifiedName);
				if (id != 0)
					ids.Add(id);
			}

			count = ids.Count;
			if (count == 0)
				return IntPtr.Zero;

			// 分配非托管内存存储 ID 数组
			IntPtr arrayPtr = Marshal.AllocHGlobal(count * sizeof(int));
			for (int i = 0; i < count; i++)
			{
				Marshal.WriteInt32(arrayPtr, i * sizeof(int), ids[i]);
			}
			return arrayPtr;
		}

		// 释放由 CreateAllScripts 分配的内存
		public static void FreeIntArray(IntPtr ptr)
		{
			if (ptr != IntPtr.Zero)
				Marshal.FreeHGlobal(ptr);
		}

		public static void CallOnInit(int id)
		{
			ScriptBehaviour instance = GetInstance(id);
			instance?.OnInit();
		}

		public static void CallOnUpdate(int id)
		{
			ScriptBehaviour instance = GetInstance(id);
			instance?.OnUpdate();
		}

		public static void DestroyInstance(int id)
		{
			lock (lockObj)
			{
				if (instances.TryGetValue(id, out ScriptBehaviour instance))
				{
					instance?.OnDestroy();
					instances.Remove(id);
				}
			}
		}

		private static ScriptBehaviour GetInstance(int id)
		{
			lock (lockObj)
			{
				instances.TryGetValue(id, out ScriptBehaviour instance);
				return instance;
			}
		}
	}
}
