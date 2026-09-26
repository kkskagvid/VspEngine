#include "RuntimePCH.h"

#include "Classes/Rigidbody.h"
#include "Classes/Scene.h"

namespace Vsp
{
	void Rigidbody::SetOwnerGameObjectHandle(NativeObjectHandle uGameObjectHandle)
	{
		m_uOwnerGameObjectHandle = uGameObjectHandle;
	}

	NativeObjectHandle Rigidbody::GetTransformHandle() const
	{
		return Scene::Get().FindGameObjectTransformHandle(m_uOwnerGameObjectHandle);
	}

	void Rigidbody::SetMass(float fMass)
	{
		// A mass is a positive amount of matter. Zero and below mean "cannot be
		// moved", which is what a static body is, and the inverse mass reports
		// that as 0 rather than as an infinity.
		m_fMass = (fMass > k_fMinimumMass) ? fMass : 0.0f;
		if (m_fMass <= 0.0f)
		{
			// A body without mass cannot be pushed: it is static by definition.
			m_Velocity = Vector3::Zero;
			m_AngularVelocity = Vector3::Zero;
		}
	}

	void Rigidbody::SetKinematic(bool bIsKinematic)
	{
		m_bIsKinematic = bIsKinematic;
		if (bIsKinematic)
		{
			// The simulation does not move a kinematic body, so the velocities it
			// accumulated would only make it look like it was about to.
			m_Velocity = Vector3::Zero;
			m_AngularVelocity = Vector3::Zero;
			m_AccumulatedForce = Vector3::Zero;
		}
		Wake();
	}

	void Rigidbody::SetGravityScale(float fGravityScale)
	{
		m_fGravityScale = (fGravityScale > 0.0f) ? fGravityScale : 0.0f;
		Wake();
	}

	void Rigidbody::SetVelocity(const Vector3& Velocity)
	{
		m_Velocity = Velocity;
		Wake();
	}

	void Rigidbody::SetAngularVelocity(const Vector3& AngularVelocity)
	{
		m_AngularVelocity = AngularVelocity;
		Wake();
	}

	void Rigidbody::SetLinearDrag(float fLinearDrag)
	{
		m_fLinearDrag = (fLinearDrag > 0.0f) ? fLinearDrag : 0.0f;
	}

	void Rigidbody::SetAngularDrag(float fAngularDrag)
	{
		m_fAngularDrag = (fAngularDrag > 0.0f) ? fAngularDrag : 0.0f;
	}

	void Rigidbody::AddForce(const Vector3& Force)
	{
		m_AccumulatedForce += Force;
		Wake();
	}

	void Rigidbody::AddImpulse(const Vector3& Impulse)
	{
		// An impulse is a change of momentum, so it becomes a change of velocity
		// the moment it is applied - scaled by the inverse mass, which makes the
		// same impulse move a light body further than a heavy one.
		const float fInverseMass = GetInverseMass();
		if (fInverseMass <= 0.0f)
		{
			return;
		}

		m_Velocity += Impulse * fInverseMass;
		Wake();
	}

	void Rigidbody::ClearAccumulatedForce()
	{
		m_AccumulatedForce = Vector3::Zero;
	}

	void Rigidbody::Wake()
	{
		m_bIsSleeping = false;
		m_fRestingSeconds = 0.0f;
	}

	void Rigidbody::Sleep()
	{
		m_bIsSleeping = true;
		m_fRestingSeconds = k_fSleepDelaySeconds;
		m_Velocity = Vector3::Zero;
		m_AngularVelocity = Vector3::Zero;
	}

	void Rigidbody::ApplySimulationResult(
		const Vector3& Velocity,
		const Vector3& AngularVelocity,
		float fRestingSeconds,
		bool bIsSleeping,
		bool bIsGrounded)
	{
		m_Velocity = Velocity;
		m_AngularVelocity = AngularVelocity;
		m_fRestingSeconds = fRestingSeconds;
		m_bIsSleeping = bIsSleeping;
		m_bIsGrounded = bIsGrounded;

		// The force of a step is spent by the step that applied it.
		m_AccumulatedForce = Vector3::Zero;
	}
}
