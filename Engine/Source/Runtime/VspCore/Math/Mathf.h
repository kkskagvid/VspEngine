#pragma once

#include <cmath>
#include <initializer_list>
#include <limits>

namespace Vsp
{
    struct Mathf
    {
        // ---------- 常量 ----------
        static constexpr float PI = 3.14159265358979f;
        static constexpr float Infinity = std::numeric_limits<float>::infinity();
        static constexpr float NegativeInfinity = -std::numeric_limits<float>::infinity();
        static constexpr float Deg2Rad = PI * 2.0f / 360.0f;
        static constexpr float Rad2Deg = 1.0f / Deg2Rad;
        static const float Epsilon;   // 运行时初始化（类似 Unity 的 static readonly）

        // ---------- 三角函数 ----------
        static float Sin(float f);
        static float Cos(float f);
        static float Tan(float f);
        static float Asin(float f);
        static float Acos(float f);
        static float Atan(float f);
        static float Atan2(float y, float x);

        // ---------- 幂与根 ----------
        static float Sqrt(float f);
        static float Pow(float f, float p);
        static float Exp(float power);
        static float Log(float f);
        static float Log(float f, float p);
        static float Log10(float f);

        // ---------- 绝对值 ----------
        static float Abs(float f);
        static int   Abs(int value);

        // ---------- 最小/最大值 ----------
        static float Min(float a, float b);
        static float Min(std::initializer_list<float> values);
        static int   Min(int a, int b);
        static int   Min(std::initializer_list<int> values);
        static float Max(float a, float b);
        static float Max(std::initializer_list<float> values);
        static int   Max(int a, int b);
        static int   Max(std::initializer_list<int> values);

        // ---------- 取整 ----------
        static float Ceil(float f);
        static float Floor(float f);
        static float Round(float f);
        static int   CeilToInt(float f);
        static int   FloorToInt(float f);
        static int   RoundToInt(float f);
        static float Sign(float f);

        // ---------- 钳制 ----------
        static float Clamp(float value, float min, float max);
        static int   Clamp(int value, int min, int max);
        static float Clamp01(float value);

        // ---------- 插值 ----------
        static float Lerp(float a, float b, float t);
        static float LerpUnclamped(float a, float b, float t);
        static float LerpAngle(float a, float b, float t);
        static float SmoothStep(float from, float to, float t);
        static float InverseLerp(float a, float b, float value);

        // ---------- 移动 ----------
        static float MoveTowards(float current, float target, float maxDelta);
        static float MoveTowardsAngle(float current, float target, float maxDelta);

        // ---------- 平滑阻尼 ----------
        // Unity 原始签名中的 deltaTime 默认取 Time.deltaTime，
        // C++ 中用两个重载模拟“省略 deltaTime”的调用形式。
        static float SmoothDamp(float current, float target,
            float& currentVelocity,
            float smoothTime, float maxSpeed,
            float deltaTime);

        // 省略 deltaTime（5 参），等价 Unity: deltaTime = Time.deltaTime
        static float SmoothDamp(float current, float target,
            float& currentVelocity,
            float smoothTime, float maxSpeed);

        // 省略 maxSpeed 与 deltaTime（4 参），等价 Unity: maxSpeed = Infinity, deltaTime = Time.deltaTime
        static float SmoothDamp(float current, float target,
            float& currentVelocity,
            float smoothTime);

        // SmoothDampAngle 同样三档重载
        static float SmoothDampAngle(float current, float target,
            float& currentVelocity,
            float smoothTime, float maxSpeed,
            float deltaTime);
        static float SmoothDampAngle(float current, float target,
            float& currentVelocity,
            float smoothTime, float maxSpeed);
        static float SmoothDampAngle(float current, float target,
            float& currentVelocity,
            float smoothTime);

        // ---------- 循环与往复 ----------
        static float Repeat(float t, float length);
        static float PingPong(float t, float length);

        // ---------- 角度差 ----------
        static float DeltaAngle(float current, float target);

        // ---------- 近似比较 ----------
        static bool Approximately(float a, float b);

        // ---------- 伽马校正 ----------
        static float Gamma(float value, float absmax, float gamma);

    private:
        static const float s_approxEpsilon;   // Epsilon * 8
    };

}
