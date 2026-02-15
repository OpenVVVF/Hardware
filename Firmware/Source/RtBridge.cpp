#include "RtBridge.h"
#include "Switching/PWMDriver.h"

// Definition of the static instance pointer
RtBridge* RtBridge::s_Instance = nullptr;

// -----------------------------
// Initialization
// -----------------------------
CommandContext RtBridge::InitAndGetContext(CommutationManager* _Manager) {
    // Assign instance pointer for the trampoline
    s_Instance = this;

    // Push the zone manager via the queue so Core 1 owns it
    m_Queue.push(InitZoneManager{_Manager});

    m_SharedStatus.Seq = 0;

    // Launch Core 1 using the static trampoline
    multicore_launch_core1(Core1EntryTrampoline);

    // Build the context with static function pointers
    // (Required because CommandContext stores C-style function pointers)
    CommandContext Ctx {};
    Ctx.zone_mgr = _Manager;

    Ctx.set_ramp_rate = CtxSetRampRate;
    Ctx.set_manual_carrier_hz = CtxSetManualCarrierHz;
    Ctx.set_manual_carrier_mode = CtxSetManualCarrierMode;

    Ctx.enable = CtxEnable;
    Ctx.disable = CtxDisable;

    Ctx.emergency_stop = CtxEstop;
    Ctx.clear_emergency_stop = CtxClearEstop;

    Ctx.set_target_frequency = CtxSetTargetFreq;
    Ctx.set_frequency_immediate = CtxSetFreqImmediate;

    Ctx.try_get_status = CtxTryGetStatus;

    return Ctx;
}

// -----------------------------
// Static Trampoline Implementation
// -----------------------------
void RtBridge::Core1EntryTrampoline() {
    if (s_Instance) {
        s_Instance->Core1Loop();
    }
}

// -----------------------------
// Core 1 Loop (Instance Method)
// -----------------------------
void RtBridge::Core1Loop() {
    // Create driver on Core 1
    PWMDriver::Config Cfg;
    Cfg.min_duty_percent = 1.0f;
    Cfg.max_duty_percent = 99.0f;

    PWMDriver Driver(Cfg);
    m_Driver = &Driver;

    Driver.setStrategy(new SPWMStrategy());
    Driver.setAutoModulation(true);
    Driver.init(2000.0f);
    Driver.enable();

    absolute_time_t Next = make_timeout_time_us(1000);

    while (true) {
        StructVariant Msg;
        while (m_Queue.try_pop(Msg)) {
            std::visit(Overloaded {
                [this](RtCmd _Cmd) {
                    if (!m_Driver) return;
                    switch (_Cmd) {
                        case RtCmd::Enable:  m_Driver->enable(); break;
                        case RtCmd::Disable: m_Driver->disable(); break;
                        case RtCmd::Estop:   m_Driver->emergencyStop(); break;
                        case RtCmd::ClearEstop: m_Driver->clearEmergency(); break;
                    }
                },
                [this](const SetRampRate& _R) { 
                    m_RampRate = _R.Value; 
                },
                [this](const SetManualCarrierHz& _R) { 
                    m_ManualCarrierHz = _R.Value; 
                },
                [this](const SetManualCarrierMode& _R) { 
                    m_ManualCarrierMode = _R.Enable; 
                },
                [this](const SetTargetFreq& _R) {
                    if(m_Driver) m_Driver->setTargetFrequency(_R.Value, m_RampRate);
                },
                [this](const SetFreqImmediate& _R) {
                    if(m_Driver) m_Driver->setFrequencyImmediate(_R.Value);
                },
                [this](const InitZoneManager& _R) {
                    m_Manager = _R.Manager;
                },
                [](const MotorConfig&) {},
                [](const SensorData&) {},
                [](const CurrentCommand&) {}
            }, Msg);
        }

        // 1kHz tick
        if (absolute_time_diff_us(get_absolute_time(), Next) <= 0) {
            Next = delayed_by_us(Next, 1000);

            if (m_Driver && !m_Driver->isEmergencyStopped()) {
                m_Driver->update(0.001f);
                UpdateCarrierFromZones();
            }

            if (m_Driver) {
                RtStatus St {};
                St.enabled = m_Driver->isEnabled();
                St.estop = m_Driver->isEmergencyStopped();
                St.current_freq = m_Driver->getCurrentFrequency();
                St.modulation_index = m_Driver->getModulationIndex();
                St.carrier_hz = m_Driver->getCarrierFrequency();
                St.sync_mode = m_Driver->isSynchronousMode();
                St.pulses = m_Driver->getPulsesPerCycle();
                St.manual_carrier_mode = m_ManualCarrierMode;
                St.manual_carrier_hz = m_ManualCarrierHz;
                St.ramp_rate = m_RampRate;

                StatusWrite(St);
            }
        }

        tight_loop_contents();
    }
}

// -----------------------------
// Carrier Logic (Instance Method)
// -----------------------------
void RtBridge::UpdateCarrierFromZones() {
    if (!m_Driver || !m_Driver->isEnabled()) return;

    if (m_ManualCarrierMode) {
        if (m_LastCarrierHz != m_ManualCarrierHz || m_LastSyncMode != false) {
            m_Driver->setCarrierFrequency(m_ManualCarrierHz);
            m_Driver->setSynchronousMode(false, 0);
            m_LastCarrierHz = m_ManualCarrierHz;
            m_LastSyncMode = false;
            m_LastPulses = 0;
        }
        return;
    }

    float CurrentFreq = m_Driver->getCurrentFrequency();
    ZoneConfig Zone {};
    float SyncPulsesF = 0.0f;

    if (m_Manager && m_Manager->getZone(CurrentFreq, &Zone)) {
        float Carrier = m_Manager->calculateCarrier(CurrentFreq, &Zone, &SyncPulsesF);
        bool SyncMode = (Zone.type == ZoneType::SYNC);
        uint16_t Pulses = SyncMode ? (uint16_t)SyncPulsesF : 0;

        if (m_LastCarrierHz != Carrier || m_LastSyncMode != SyncMode || m_LastPulses != Pulses) {
            m_Driver->setCarrierFrequency(Carrier);
            m_Driver->setSynchronousMode(SyncMode, Pulses);
            m_LastCarrierHz = Carrier;
            m_LastSyncMode = SyncMode;
            m_LastPulses = Pulses;
        }
    }
}

// -----------------------------
// Status Seqlock Implementation
// -----------------------------
void RtBridge::StatusWrite(const RtStatus& _Status) {
    m_SharedStatus.Seq++;
    __dmb();
    m_SharedStatus.Status = _Status;
    __dmb();
    m_SharedStatus.Seq++;
}

bool RtBridge::StatusRead(RtStatus* _OutStatus) {
    uint32_t A = m_SharedStatus.Seq;
    __dmb();
    RtStatus Snap = m_SharedStatus.Status;
    __dmb();
    uint32_t B = m_SharedStatus.Seq;
    if (A != B || (A & 1u)) return false;
    *_OutStatus = Snap;
    return true;
}

// -----------------------------
// Static Context Callbacks
// -----------------------------
void RtBridge::CtxSetRampRate(float _Value) { 
    if(s_Instance) s_Instance->m_Queue.push(SetRampRate{_Value}); 
}

void RtBridge::CtxSetManualCarrierHz(float _Value) { 
    if(s_Instance) s_Instance->m_Queue.push(SetManualCarrierHz{_Value}); 
}

void RtBridge::CtxSetManualCarrierMode(bool _Enable) { 
    if(s_Instance) s_Instance->m_Queue.push(SetManualCarrierMode{_Enable}); 
}

void RtBridge::CtxEnable() { 
    if(s_Instance) s_Instance->m_Queue.push(RtCmd::Enable); 
}

void RtBridge::CtxDisable() { 
    if(s_Instance) s_Instance->m_Queue.push(RtCmd::Disable); 
}

void RtBridge::CtxEstop() { 
    if(s_Instance) s_Instance->m_Queue.push(RtCmd::Estop); 
}

void RtBridge::CtxClearEstop() { 
    if(s_Instance) s_Instance->m_Queue.push(RtCmd::ClearEstop); 
}

void RtBridge::CtxSetTargetFreq(float _Value) { 
    if(s_Instance) s_Instance->m_Queue.push(SetTargetFreq{_Value}); 
}

void RtBridge::CtxSetFreqImmediate(float _Value) { 
    if(s_Instance) s_Instance->m_Queue.push(SetFreqImmediate{_Value}); 
}

bool RtBridge::CtxTryGetStatus(RtStatus* _OutStatus) { 
    if(s_Instance) return s_Instance->StatusRead(_OutStatus);
    return false;
}