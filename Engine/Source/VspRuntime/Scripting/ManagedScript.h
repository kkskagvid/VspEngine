#pragma once

#include "Core/String/VspString.h"
#include "BridgeFunctions.h"

namespace Vsp
{
    class ManagedScript
    {
    public:
        ManagedScript(const BridgeFunctions& bridge)
            : m_Bridge(&bridge), m_InstanceID(0) {}

        ~ManagedScript()
        {
            DestroyInstance();
        }

        bool CreateInstance(const VspString& typeName)
        {
            m_InstanceID = m_Bridge->CreateInst(typeName.GetData());
            return m_InstanceID != 0;
        }

        void OnInit()
        {
            if (m_InstanceID != 0)
                m_Bridge->OnInit(m_InstanceID);
        }

        void OnUpdate(float deltaTime)
        {
            if (m_InstanceID != 0)
                m_Bridge->OnUpdate(m_InstanceID);
        }

        void DestroyInstance()
        {
            if (m_InstanceID != 0)
            {
                m_Bridge->DestroyInst(m_InstanceID);
                m_InstanceID = 0;
            }
        }

        int GetInstanceID() const { return m_InstanceID; }

    private:
        const BridgeFunctions* m_Bridge;
        int m_InstanceID;
    };
}
