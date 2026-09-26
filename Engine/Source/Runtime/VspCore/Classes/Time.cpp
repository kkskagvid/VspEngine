#include "RuntimePCH.h"

#include "Classes/Time.h"

namespace Vsp
{
	Time& Time::Get()
	{
		static Time s_Instance;
		return s_Instance;
	}

	void Time::AdvanceFrame(float fUnscaledDeltaSeconds)
	{
		float fFrameSeconds = 0.0f;
		if (m_fFixedDeltaSeconds > 0.0f)
		{
			// A configured step is exact. The stall guard protects against a
			// MEASUREMENT, and this is not one.
			fFrameSeconds = m_fFixedDeltaSeconds;
		}
		else
		{
			// A negative or NaN measurement counts as "no time passed" rather
			// than as an error: a clock has nothing to report and nothing to
			// reject.
			fFrameSeconds = (fUnscaledDeltaSeconds > 0.0f) ? fUnscaledDeltaSeconds : 0.0f;
			if (fFrameSeconds > m_fMaximumDeltaSeconds)
			{
				fFrameSeconds = m_fMaximumDeltaSeconds;
			}
		}

		m_fUnscaledDeltaTime = fFrameSeconds;
		m_fDeltaTime = fFrameSeconds * m_fTimeScale;

		m_fUnscaledElapsedTime += m_fUnscaledDeltaTime;
		m_fElapsedTime += m_fDeltaTime;
		++m_nFrameCount;

		// The frame rate follows the unscaled step: it describes how fast the
		// machine renders, not how fast the game chose to play.
		UpdateFramesPerSecond(m_fUnscaledDeltaTime);
	}

	void Time::ResetFrameClock()
	{
		m_fDeltaTime = 0.0f;
		m_fUnscaledDeltaTime = 0.0f;
		m_fElapsedTime = 0.0f;
		m_fUnscaledElapsedTime = 0.0f;
		m_fFramesPerSecond = 0.0f;
		m_nFrameCount = 0;
	}

	void Time::SetTimeScale(float fTimeScale)
	{
		m_fTimeScale = (fTimeScale > 0.0f) ? fTimeScale : 0.0f;
	}

	void Time::SetFixedDeltaSeconds(float fFixedDeltaSeconds)
	{
		// 0 (and anything unusable) means "follow the wall clock".
		m_fFixedDeltaSeconds = (fFixedDeltaSeconds > 0.0f) ? fFixedDeltaSeconds : 0.0f;
	}

	void Time::SetMaximumDeltaSeconds(float fMaximumDeltaSeconds)
	{
		// A clamp of zero or less would freeze the clock; the default is the
		// useful answer to "no value".
		m_fMaximumDeltaSeconds = (fMaximumDeltaSeconds > 0.0f)
			? fMaximumDeltaSeconds
			: k_fDefaultMaximumDeltaSeconds;
	}

	void Time::UpdateFramesPerSecond(float fFrameSeconds)
	{
		if (fFrameSeconds <= 0.0f)
		{
			// A frame that took no measurable time says nothing about the rate.
			return;
		}

		const float fInstantaneousFramesPerSecond = 1.0f / fFrameSeconds;

		// The first frame has nothing to smooth against.
		m_fFramesPerSecond = (m_nFrameCount <= 1)
			? fInstantaneousFramesPerSecond
			: (m_fFramesPerSecond +
				((fInstantaneousFramesPerSecond - m_fFramesPerSecond) * k_fFrameRateSmoothingWeight));
	}
}
