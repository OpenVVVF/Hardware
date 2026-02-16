#include "RtBridge.h"
#include "Switching/Modulation.h"

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

    static SPWMStrategy strat;
    m_Strategy = &strat;

    Driver.setStrategy(m_Strategy);
    Driver.setAutoModulation(true);
    Driver.init(2000.0f);
    Driver.enable();


    
    static FocController controller;
    FOCController_ = &controller;

    FocOutput FOCOutput;





    // // --- START CALIBRATION ROUTINE ---
    
    // // 1. Create a dummy FOC output and sensor data
    // SensorData TempSensors; 
    // TempSensors._EncoderPosition_Rad = 0; 
    
    // // 2. Lock the rotor for 2 seconds (2000ms)
    // // Use a small voltage (e.g., 2-3V). 
    // // If your bus is 60V, 3V is safe. 
    // // If it doesn't lock, increase slightly, but do not exceed 5V for safety during test.
    // absolute_time_t CalibEnd = delayed_by_ms(get_absolute_time(), 2000);
    
    // while (absolute_time_diff_us(get_absolute_time(), CalibEnd) > 0) {
    //     // We need to continuously feed the SPWM generator
    //     // We bypass the normal UpdateVoltages and use our Calibrate function
        
    //     // Note: You might need to update TempSensors here if your encoder needs time to settle
    //     // but usually for calibration we just care about the final reading.
        
    //     FOCController_->CalibrateEncoderOffset(3.0f); // Apply 3V
        
    //     PhaseVoltages TargetDutyCycles;
    //     GenerateSpwm(FOCController_->UpdateVoltages(), 0.50, TargetDutyCycles); 
    //     // NOTE: You need a getter for FocOutput or make _Valpha/_Vbeta public.
    //     // Easiest hack: FOCController_->_Valpha_V is likely public or friend.
        
    //     m_Driver->setDutyCycles(TargetDutyCycles._Du_unitless, TargetDutyCycles._Dv_unitless, TargetDutyCycles._Dw_unitless);
        
    //     StructVariant Msg;
    //     if (m_Queue.try_pop(Msg)) {
    //          if (std::holds_alternative<SensorData>(Msg)) {
    //              FOCController_->UpdateSensors(std::get<SensorData>(Msg));
    //          }
    //     }
        

    //     sleep_ms(10); // Run at ~100Hz during calibration
    // }
    
    // // 3. Read the current sensor data from Core0 (it should be updating in background)
    // // OR, if Sensors_ is not updating because Core0 hasn't pushed, you have a problem.
    // // Let's assume Sensors_ has valid data or push a request.
    
    // // HACK: Since Core0 pushes sensor data, we just read the last known state.
    // // But we need the ENCODER value NOW.
    
    // // Ideally, you have access to measurements pointer here, but you don't.
    // // We rely on the last SensorData pushed to FOCController.
    // // This requires Core0 to be running and pushing data while we lock.
    // // Since Core0 is in an infinite loop in main, it IS pushing to the queue.
    // // We need to pop that queue!
    

    // // 4. Calculate the offset
    // // Current Electrical Angle = 0 (where we forced it)
    // // Measured Mechanical Angle = Sensors_._EncoderPosition_Rad
    // // Offset = TargetElec - (Mech * Poles)
    
    // float MechedAngle = FOCController_->Sensors_._EncoderPosition_Rad;
    // float Poles = FOCController_->GetMotorConfig()._PolePairs_unitless;
    
    // // The offset needed to make the measured angle equal to 0
    // float CalculatedOffset = 0.0f - (MechedAngle * Poles);
    
    // // Normalize to -PI to PI? Not strictly necessary if the angle logic wraps, but good practice.
    
    // FOCController_->_EncoderOffset_Rad = CalculatedOffset;
    
    
    // // Turn off calibration voltage (reset controllers)
    // FOCController_->Reset();
    
    // // --- END CALIBRATION ROUTINE ---





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
                [this](const MotorConfig& _R) {
                    FOCController_->SetMotorConfig(_R);
                },
                [this](const SensorData& _R) {
                    FOCController_->UpdateSensors(_R);
                },
                [this](const CurrentCommand& _R) {
                    FOCController_->ApplyCurrentLimits(_R);
                }
            }, Msg);
        }

        // 1kHz tick
        if (absolute_time_diff_us(get_absolute_time(), Next) <= 0) {
            Next = delayed_by_us(Next, 1000);


            if (m_Driver && !m_Driver->isEmergencyStopped()) {

                // Now that the FOC Controller has been fed with new data (if any)
                // firstly, generate the raw output
                // then use (either swpm, or svpwm) to generate actual output 0-1 duty cycles
                // then write those to the registers for each phase
                FOCOutput = FOCController_->UpdateVoltages();
                PhaseVoltages TargetDutyCycles;
                GenerateSpwm(FOCOutput, 0.95, TargetDutyCycles);
                 
                // m_Driver->update(0.001f);
                m_Driver->setDutyCycles(TargetDutyCycles._Du_unitless, TargetDutyCycles._Dv_unitless, TargetDutyCycles._Dw_unitless);
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

                St.debug_Vd = FOCController_->_Vd_V;
                St.debug_Vq = FOCController_->_Vq_V;
                St.debug_Iq_measured = FOCController_->_Iq_A;
                St.debug_angle_elec = FOCController_->_ElectricalAngle_Rad;
                // St.debug_Iq_error = FOCController_->_QaxisController_.

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