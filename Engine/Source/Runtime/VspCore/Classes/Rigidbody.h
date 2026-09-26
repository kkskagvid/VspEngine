#pragma once

#include "Classes/Object.h"
#include "Core/Core.h"
#include "Math/Vector3.h"

namespace Vsp
{
	// -------------------------------------------------------------------------
	// Rigidbody
	// -------------------------------------------------------------------------
	// The MOTION of one object: what a frame of simulation does to it. A body
	// owns a velocity, an angular velocity and the settings that decide how they
	// change - mass, gravity, drag - and PhysicsWorld integrates them into the
	// transform of the game object the body belongs to.
	//
	// A collider says what an object IS; a rigidbody says that it MOVES. An
	// object with a collider and no body never moves: it is the level, and every
	// other body collides with it. That is the one rule the simulation needs to
	// tell a crate from a wall.
	//
	// A kinematic body is the middle case: it moves, but only where its transform
	// is put - by a script, an animation or a moving platform - and it pushes
	// other bodies without being pushed back.
	//
	// Units are the engine's: world units for length, seconds for time, degrees
	// for rotation (the same unit Transform and Camera use), so an angular
	// velocity of 90 turns the object a quarter turn per second.
	//
	// Every function is a plain data operation; nothing throws.
	// -------------------------------------------------------------------------
#pragma warning(push)
#pragma warning(disable : 4251)   // Vector3 members: header-only value type.
	class RUNTIME_API Rigidbody : public NativeObject
	{
	public:
		// Mass at or below which an object is treated as infinitely heavy: it is
		// the same answer as "static", and it keeps an inverse mass finite.
		static constexpr float k_fMinimumMass = 0.0001f;

		// Approach speed below which a contact does not bounce, whatever the
		// restitution says. Without it a body resting on the ground would keep
		// hopping on the speed gravity gives it every frame.
		static constexpr float k_fBounceSpeedThreshold = 0.5f;

		// Seconds a body has to stay slow before the simulation puts it to sleep.
		static constexpr float k_fSleepDelaySeconds = 0.25f;

		// Speed below which a body counts as slow (world units per second).
		static constexpr float k_fSleepSpeedThreshold = 0.08f;

		// Turn rate below which a body counts as slow (degrees per second).
		static constexpr float k_fSleepAngularSpeedThreshold = 2.0f;

		// -------- Owner --------
		// The game object whose transform the body drives.
		NativeObjectHandle GetOwnerGameObjectHandle() const { return m_uOwnerGameObjectHandle; }
		void SetOwnerGameObjectHandle(NativeObjectHandle uGameObjectHandle);

		// Transform handle of the owner (0 when the object is gone).
		NativeObjectHandle GetTransformHandle() const;

		// -------- Mass --------
		float GetMass() const { return m_fMass; }
		void SetMass(float fMass);

		// 1 / mass, and 0 for a body the simulation may not move.
		float GetInverseMass() const { return (m_fMass > k_fMinimumMass) ? (1.0f / m_fMass) : 0.0f; }

		// -------- What drives the body --------
		// A kinematic body is placed by its transform instead of by the
		// simulation: it pushes others, nothing pushes it.
		bool IsKinematic() const { return m_bIsKinematic; }
		void SetKinematic(bool bIsKinematic);

		bool GetUseGravity() const { return m_bUseGravity; }
		void SetUseGravity(bool bUseGravity) { m_bUseGravity = bUseGravity; }

		// Multiplier on the world's gravity: 0 floats, 2 falls twice as fast.
		float GetGravityScale() const { return m_fGravityScale; }
		void SetGravityScale(float fGravityScale);

		// -------- Motion --------
		const Vector3& GetVelocity() const { return m_Velocity; }
		void SetVelocity(const Vector3& Velocity);

		// Turn rate about the object's own axes, in degrees per second.
		const Vector3& GetAngularVelocity() const { return m_AngularVelocity; }
		void SetAngularVelocity(const Vector3& AngularVelocity);

		bool GetFreezeRotation() const { return m_bFreezeRotation; }
		void SetFreezeRotation(bool bFreezeRotation) { m_bFreezeRotation = bFreezeRotation; }

		// How quickly motion dies down on its own: 0 keeps it forever, larger
		// values stop the body sooner. Applied per second.
		float GetLinearDrag() const { return m_fLinearDrag; }
		void SetLinearDrag(float fLinearDrag);
		float GetAngularDrag() const { return m_fAngularDrag; }
		void SetAngularDrag(float fAngularDrag);

		// -------- Forces --------
		// A force is spread over the next step; an impulse changes the velocity at
		// once. Both wake a sleeping body, which is what a script pushing a crate
		// expects.
		void AddForce(const Vector3& Force);
		void AddImpulse(const Vector3& Impulse);
		void ClearAccumulatedForce();

		const Vector3& GetAccumulatedForce() const { return m_AccumulatedForce; }

		// -------- Resting --------
		// A body that stays slow long enough is put to sleep: its motion is
		// zeroed and the simulation stops integrating it, which is what keeps a
		// resting object from drifting or jittering. Anything that pushes it
		// wakes it again.
		bool IsSleeping() const { return m_bIsSleeping; }
		void Wake();
		void Sleep();

		// True while the body had a contact pushing it up in the last step.
		bool IsGrounded() const { return m_bIsGrounded; }

		// -------- Bookkeeping the simulation owns --------
		float GetRestingSeconds() const { return m_fRestingSeconds; }

		// Writes the result of one step back into the body. PhysicsWorld is the
		// only caller: unlike the setters above it does NOT wake the body, because
		// a body the solver has just put to sleep has to stay asleep.
		void ApplySimulationResult(
			const Vector3& Velocity,
			const Vector3& AngularVelocity,
			float fRestingSeconds,
			bool bIsSleeping,
			bool bIsGrounded);

	private:
		NativeObjectHandle m_uOwnerGameObjectHandle = k_nInvalidObjectHandle;

		float m_fMass = 1.0f;
		bool m_bIsKinematic = false;
		bool m_bUseGravity = true;
		float m_fGravityScale = 1.0f;

		Vector3 m_Velocity = Vector3::Zero;
		Vector3 m_AngularVelocity = Vector3::Zero;
		Vector3 m_AccumulatedForce = Vector3::Zero;

		bool m_bFreezeRotation = false;
		float m_fLinearDrag = 0.0f;
		float m_fAngularDrag = 0.05f;

		bool m_bIsSleeping = false;
		bool m_bIsGrounded = false;
		float m_fRestingSeconds = 0.0f;
	};
#pragma warning(pop)
}
