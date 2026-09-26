#pragma once

#include "Core/Core.h"

namespace Vsp
{
	// -------------------------------------------------------------------------
	// Time
	// -------------------------------------------------------------------------
	// The engine's clock: the one place that turns "how long the last frame
	// took" into the values the engine, the render pipeline and the scripts
	// read.
	//
	// The class owns the POLICY, never the platform. Measuring a frame is the
	// host's job (Common/PlatformMisc's HighResolutionTimer today, whatever the
	// platform provides tomorrow); AdvanceFrame() takes those raw seconds and
	// applies
	//
	//   * the frame-step mode - the measured wall clock, or a fixed step, which
	//     is what makes an automated run reproducible on any machine,
	//   * the time scale a game slows down, speeds up or pauses with,
	//   * the clamp that keeps a stall (a breakpoint, a modal dialog, a resize)
	//     from making the simulation jump,
	//
	// and publishes delta time, elapsed time, the frame count and a smoothed
	// frame rate. Nothing here is platform specific and nothing throws: an
	// input the clock cannot use is clamped, never rejected.
	//
	// Everything reads the SAME instance (Time::Get()): the managed facade
	// (VspEngine.Time), the native diagnostics and the render pipeline, so two
	// parts of the engine can never disagree about when "now" is.
	// -------------------------------------------------------------------------
	class RUNTIME_API Time
	{
	public:
		// Longest frame the clock accepts by default, in seconds.
		static constexpr float k_fDefaultMaximumDeltaSeconds = 0.1f;

		// The process-wide clock.
		static Time& Get();

		// -------- The frame --------
		// Advances the clock by one frame. fUnscaledDeltaSeconds is the raw
		// frame duration the host measured; it is ignored while a fixed step is
		// configured. A value the clock cannot use (negative, NaN) counts as 0,
		// and one longer than the maximum is clamped to it.
		void AdvanceFrame(float fUnscaledDeltaSeconds);

		// Forgets everything the clock accumulated (elapsed time, frame count,
		// frame rate) and keeps the configured policy.
		void ResetFrameClock();

		// -------- What the clock reads --------
		// Seconds since the previous frame, with the time scale applied.
		float GetDeltaTime() const { return m_fDeltaTime; }

		// Seconds the previous frame really took: the unscaled step, even while
		// a fixed step drives GetDeltaTime.
		float GetUnscaledDeltaTime() const { return m_fUnscaledDeltaTime; }

		// Seconds of engine time since the clock started (time scale applied).
		float GetElapsedTime() const { return m_fElapsedTime; }

		// Seconds of real time since the clock started.
		float GetUnscaledElapsedTime() const { return m_fUnscaledElapsedTime; }

		// Frames advanced so far.
		uint64 GetFrameCount() const { return m_nFrameCount; }

		// Smoothed frames per second, so a readout does not flicker.
		float GetFramesPerSecond() const { return m_fFramesPerSecond; }

		// -------- Time scale --------
		// 1 = real time, 0 = paused, 2 = twice as fast. A negative or unusable
		// value is clamped to 0: this clock never runs backwards.
		float GetTimeScale() const { return m_fTimeScale; }
		void SetTimeScale(float fTimeScale);

		// -------- Frame-step mode --------
		// True while every frame advances the clock by GetFixedDeltaSeconds
		// instead of by what the host measured.
		bool IsFixedTimeStep() const { return m_fFixedDeltaSeconds > 0.0f; }

		// The step a fixed-time run advances by, in seconds; 0 = wall clock.
		float GetFixedDeltaSeconds() const { return m_fFixedDeltaSeconds; }
		void SetFixedDeltaSeconds(float fFixedDeltaSeconds);

		// -------- Stall guard --------
		// Longest MEASURED frame the clock accepts, in seconds. A
		// configured fixed step is never clamped: it is a setting, not a
		// measurement.
		float GetMaximumDeltaSeconds() const { return m_fMaximumDeltaSeconds; }
		void SetMaximumDeltaSeconds(float fMaximumDeltaSeconds);

	private:
		Time() = default;

		// How far one frame moves the smoothed frame rate towards the frame's
		// own value: a low weight keeps the readout steady, a high one follows
		// a hitch quickly.
		static constexpr float k_fFrameRateSmoothingWeight = 0.1f;

		// Folds one frame's duration into the smoothed frame rate.
		void UpdateFramesPerSecond(float fFrameSeconds);

		float m_fDeltaTime = 0.0f;
		float m_fUnscaledDeltaTime = 0.0f;
		float m_fElapsedTime = 0.0f;
		float m_fUnscaledElapsedTime = 0.0f;
		float m_fTimeScale = 1.0f;
		float m_fFixedDeltaSeconds = 0.0f;
		float m_fMaximumDeltaSeconds = k_fDefaultMaximumDeltaSeconds;
		float m_fFramesPerSecond = 0.0f;
		uint64 m_nFrameCount = 0;
	};
}
