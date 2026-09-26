#include "RuntimePCH.h"

#include <cmath>

#include "Classes/Time.h"

#include "Math/Mathf.h"

namespace Vsp
{
    const float Mathf::Epsilon = std::numeric_limits<float>::min();
    const float Mathf::s_approxEpsilon = Mathf::Epsilon * 8.0f;

    // ---------- 三角函数 ----------
    float Mathf::Sin(float f) { return std::sin(f); }
    float Mathf::Cos(float f) { return std::cos(f); }
    float Mathf::Tan(float f) { return std::tan(f); }
    float Mathf::Asin(float f) { return std::asin(f); }
    float Mathf::Acos(float f) { return std::acos(f); }
    float Mathf::Atan(float f) { return std::atan(f); }
    float Mathf::Atan2(float y, float x) { return std::atan2(y, x); }

    // ---------- 幂与根 ----------
    float Mathf::Sqrt(float f) { return std::sqrt(f); }
    float Mathf::Pow(float f, float p) { return std::pow(f, p); }
    float Mathf::Exp(float power) { return std::exp(power); }
    float Mathf::Log(float f) { return std::log(f); }
    float Mathf::Log(float f, float p) { return std::log(f) / std::log(p); }
    float Mathf::Log10(float f) { return std::log10(f); }

    // ---------- 绝对值 ----------
    float Mathf::Abs(float f) { return std::fabs(f); }
    int   Mathf::Abs(int value) { return std::abs(value); }

    // ---------- 最小/最大值 ----------
    float Mathf::Min(float a, float b) { return a < b ? a : b; }

    float Mathf::Min(std::initializer_list<float> values)
    {
        if (values.size() == 0) return 0.0f;
        float m = *values.begin();
        for (float v : values) if (v < m) m = v;

        return m;
    }

    int Mathf::Min(int a, int b) { return a < b ? a : b; }

    int Mathf::Min(std::initializer_list<int> values)
    {
        if (values.size() == 0) return 0;
        int m = *values.begin();
        for (int v : values) if (v < m) m = v;

        return m;
    }

    float Mathf::Max(float a, float b) { return a > b ? a : b; }

    float Mathf::Max(std::initializer_list<float> values)
    {
        if (values.size() == 0) return 0.0f;
        float m = *values.begin();
        for (float v : values) if (v > m) m = v;

        return m;
    }

    int Mathf::Max(int a, int b) { return a > b ? a : b; }

    int Mathf::Max(std::initializer_list<int> values)
    {
        if (values.size() == 0) return 0;
        int m = *values.begin();
        for (int v : values) if (v > m) m = v;

        return m;
    }

    // ---------- 取整 ----------
    float Mathf::Ceil(float f) { return std::ceil(f); }
    float Mathf::Floor(float f) { return std::floor(f); }
    float Mathf::Round(float f) { return std::round(f); }

    int Mathf::CeilToInt(float f) { return static_cast<int>(std::ceil(f)); }
    int Mathf::FloorToInt(float f) { return static_cast<int>(std::floor(f)); }
    int Mathf::RoundToInt(float f) { return static_cast<int>(std::round(f)); }

    float Mathf::Sign(float f) { return f >= 0.0f ? 1.0f : -1.0f; }

    // ---------- 钳制 ----------
    float Mathf::Clamp(float value, float min, float max)
    {
        return value < min ? min : (value > max ? max : value);
    }

    int Mathf::Clamp(int value, int min, int max)
    {
        return value < min ? min : (value > max ? max : value);
    }

    float Mathf::Clamp01(float value)
    {
        return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
    }

    // ---------- 插值 ----------
    float Mathf::Lerp(float a, float b, float t)
    {
        return a + (b - a) * Clamp01(t);
    }

    float Mathf::LerpUnclamped(float a, float b, float t)
    {
        return a + (b - a) * t;
    }

    float Mathf::LerpAngle(float a, float b, float t)
    {
        float delta = Repeat(b - a, 360.0f);
        if (delta > 180.0f) delta -= 360.0f;

        return a + delta * Clamp01(t);
    }

    float Mathf::SmoothStep(float from, float to, float t)
    {
        t = Clamp01(t);
        t = -2.0f * t * t * t + 3.0f * t * t;

        return to * t + from * (1.0f - t);
    }

    float Mathf::InverseLerp(float a, float b, float value)
    {
        return a != b ? Clamp01((value - a) / (b - a)) : 0.0f;
    }

    // ---------- 移动 ----------
    float Mathf::MoveTowards(float current, float target, float maxDelta)
    {
        float diff = target - current;
        if (Abs(diff) <= maxDelta) return target;

        return current + Sign(diff) * maxDelta;
    }

    float Mathf::MoveTowardsAngle(float current, float target, float maxDelta)
    {
        float deltaAngle = DeltaAngle(current, target);
        if (-maxDelta < deltaAngle && deltaAngle < maxDelta) return target;
        target = current + deltaAngle;

        return MoveTowards(current, target, maxDelta);
    }

    // ---------- 平滑阻尼（核心算法） ----------
    float Mathf::SmoothDamp(float current, float target,
        float& currentVelocity,
        float smoothTime, float maxSpeed, float deltaTime)
    {
        // Game Programming Gems 4, Chapter 1.10
        smoothTime = Max(0.0001f, smoothTime);
        const float omega = 2.0f / smoothTime;

        const float x = omega * deltaTime;
        const float exp = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);

        float change = current - target;
        const float originalTo = target;

        const float maxChange = maxSpeed * smoothTime;
        change = Clamp(change, -maxChange, maxChange);
        target = current - change;

        const float temp = (currentVelocity + omega * change) * deltaTime;
        const float originalVelocity = currentVelocity;
        currentVelocity = (currentVelocity - omega * temp) * exp;

        float output = target + (change + temp) * exp;

        // 防止过冲
        if ((originalTo - current > 0.0f) == (output > originalTo))
        {
            output = originalTo;
            currentVelocity = (deltaTime != 0.0f)
                ? (output - originalTo) / deltaTime
                : originalVelocity;
        }

        return output;
    }

    // ---------- 5 参版本：缺省 deltaTime ← Time::deltaTime ----------
    float Mathf::SmoothDamp(float current, float target,
        float& currentVelocity,
        float smoothTime, float maxSpeed)
    {
        return SmoothDamp(current, target, currentVelocity,
            smoothTime, maxSpeed, Time::Get().GetDeltaTime());
    }

    // ---------- 4 参版本：缺省 maxSpeed=Infinity, deltaTime ← Time::deltaTime ----------
    float Mathf::SmoothDamp(float current, float target,
        float& currentVelocity,
        float smoothTime)
    {
        return SmoothDamp(current, target, currentVelocity,
            smoothTime, Mathf::Infinity, Time::Get().GetDeltaTime());
    }

    // ---------- SmoothDampAngle 同构包装 ----------
    float Mathf::SmoothDampAngle(float current, float target,
        float& currentVelocity,
        float smoothTime, float maxSpeed, float deltaTime)
    {
        target = current + DeltaAngle(current, target);

        return SmoothDamp(current, target, currentVelocity,
            smoothTime, maxSpeed, deltaTime);
    }

    float Mathf::SmoothDampAngle(float current, float target,
        float& currentVelocity,
        float smoothTime, float maxSpeed)
    {
        return SmoothDampAngle(current, target, currentVelocity,
            smoothTime, maxSpeed, Time::Get().GetDeltaTime());
    }

    float Mathf::SmoothDampAngle(float current, float target,
        float& currentVelocity,
        float smoothTime)
    {
        return SmoothDampAngle(current, target, currentVelocity,
            smoothTime, Mathf::Infinity, Time::Get().GetDeltaTime());
    }

    // ---------- 循环与往复 ----------
    float Mathf::Repeat(float t, float length)
    {
        return Clamp(t - std::floor(t / length) * length, 0.0f, length);
    }

    float Mathf::PingPong(float t, float length)
    {
        t = Repeat(t, length * 2.0f);

        return length - Abs(t - length);
    }

    // ---------- 角度差 ----------
    float Mathf::DeltaAngle(float current, float target)
    {
        float delta = Repeat(target - current, 360.0f);
        if (delta > 180.0f) delta -= 360.0f;

        return delta;
    }

    // ---------- 近似比较 ----------
    bool Mathf::Approximately(float a, float b)
    {
        return Abs(b - a) < Max(0.000001f * Max(Abs(a), Abs(b)), s_approxEpsilon);
    }

    // ---------- 伽马校正 ----------
    float Mathf::Gamma(float value, float absmax, float gamma)
    {
        bool negative = value < 0.0f;
        float absval = Abs(value);
        if (absval > absmax) return negative ? -absval : absval;
        float result = Pow(absval / absmax, gamma) * absmax;

        return negative ? -result : result;
    }
}
