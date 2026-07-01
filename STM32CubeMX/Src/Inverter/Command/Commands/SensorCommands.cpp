#include "Inverter/Command/CommandInterface.h"
#include "Inverter/Command/CommandContext.h"
#include "Inverter/Drivers/Sensors/PhaseCurrentADC.h"
#include "Inverter/Drivers/Sensors/DcLinkVoltageSensor.h"
#include "Inverter/Drivers/Sensors/MAX22530.h"
#include "Inverter/Telemetry.h"

using Inverter::PhaseCurrentADC;
using Inverter::DcLinkVoltageSensor;
using Inverter::MAX22530;
using Inverter::phaseCurrentADC;
using Inverter::dcLinkVoltageSensor;

class OcSetCommand : public CommandInterface {
public:
    OcSetCommand()
      : CommandInterface("ocset", "Set phase overcurrent threshold (0 disables)",
            ArgSpec{"amps", "A", 0.0f, 2000.0f, 0.0f, true, ArgSpec::FLOAT}) {}

    void execute(const ArgValue* args, CommandContext&) override {
        float amps = args[0].f_val;
        if (amps < 0.0f) amps = 0.0f;
        phaseCurrentADC().setOvercurrentThreshold(amps);
        Telemetry::printf("[SHELL] phase overcurrent threshold set to %.3f A", static_cast<double>(amps));
    }
};

class HwOcSetCommand : public CommandInterface {
public:
    HwOcSetCommand()
      : CommandInterface("hwocset", "Set hardware overcurrent threshold (0 disables ADC watchdog)",
            ArgSpec{"amps", "A", 0.0f, 2000.0f, 0.0f, true, ArgSpec::FLOAT}) {}

    void execute(const ArgValue* args, CommandContext&) override {
        float amps = args[0].f_val;
        if (amps < 0.0f) amps = 0.0f;
        if (phaseCurrentADC().setHardwareOvercurrentThreshold(amps)) {
            Telemetry::printf("[SHELL] hardware overcurrent threshold set to %.3f A", static_cast<double>(amps));
        } else {
            Telemetry::printf("[SHELL] failed to set hardware overcurrent threshold (stop motor first)");
        }
    }
};

class MaxCfgOvCommand : public CommandInterface {
public:
    MaxCfgOvCommand()
      : CommandInterface("maxcfg_ov", "Set DC-link overvoltage threshold",
            ArgSpec{"volts", "V", 0.0f, 1000.0f, 0.0f, true, ArgSpec::FLOAT}) {}

    void execute(const ArgValue* args, CommandContext&) override {
        DcLinkVoltageSensor& vdc = dcLinkVoltageSensor();
        if (vdc.setOvervoltageThreshold(args[0].f_val)) {
            Telemetry::printf("[SHELL] Vbus OV threshold set to %.3f V", static_cast<double>(args[0].f_val));
        } else {
            Telemetry::printf("[SHELL] failed to set OV threshold");
        }
    }
};

class MaxCfgUvCommand : public CommandInterface {
public:
    MaxCfgUvCommand()
      : CommandInterface("maxcfg_uv", "Set DC-link undervoltage threshold",
            ArgSpec{"volts", "V", 0.0f, 1000.0f, 0.0f, true, ArgSpec::FLOAT}) {}

    void execute(const ArgValue* args, CommandContext&) override {
        DcLinkVoltageSensor& vdc = dcLinkVoltageSensor();
        if (vdc.setUndervoltageThreshold(args[0].f_val)) {
            Telemetry::printf("[SHELL] Vbus UV threshold set to %.3f V", static_cast<double>(args[0].f_val));
        } else {
            Telemetry::printf("[SHELL] failed to set UV threshold");
        }
    }
};

class MaxCfgStatusCommand : public CommandInterface {
public:
    MaxCfgStatusCommand() : CommandInterface("maxcfg_status", "Print MAX22530 status") {}

    void execute(const ArgValue*, CommandContext&) override {
        MAX22530& adc = dcLinkVoltageSensor().adc();
        uint16_t cout_status = 0;
        (void)adc.getComparatorStatus(cout_status);
        uint16_t couthi = 0, coutlo = 0;
        (void)adc.readComparatorThreshold(0, couthi, coutlo);
        Telemetry::printf("[SHELL] MAX int_status=0x%04X cout_status=0x%04X int_enable=0x%04X",
                          adc.lastInterruptStatus(), cout_status, adc.lastInterruptEnable());
        Telemetry::printf("[SHELL] MAX ch1 thresholds: HI=0x%03X LO=0x%03X",
                          couthi, coutlo);
        Telemetry::printf("[SHELL] MAX irq=%lu dma=%lu err=%lu fail=%lu crc_err=%lu",
                          static_cast<unsigned long>(adc.irqCount()),
                          static_cast<unsigned long>(adc.dmaCompleteCount()),
                          static_cast<unsigned long>(adc.dmaErrorCount()),
                          static_cast<unsigned long>(adc.dmaStartFailCount()),
                          static_cast<unsigned long>(adc.crcErrorCount()));
    }
};

class MaxCfgThresholdsCommand : public CommandInterface {
public:
    MaxCfgThresholdsCommand() : CommandInterface("maxcfg_thresholds", "Print DC-link OV/UV thresholds") {}

    void execute(const ArgValue*, CommandContext&) override {
        DcLinkVoltageSensor& vdc = dcLinkVoltageSensor();
        Telemetry::printf("[SHELL] Vbus OV=%.3f V UV=%.3f V (scaled)",
                          static_cast<double>(vdc.overvoltageThreshold()),
                          static_cast<double>(vdc.undervoltageThreshold()));
    }
};

class MaxCfgFilterClearCommand : public CommandInterface {
public:
    MaxCfgFilterClearCommand() : CommandInterface("maxcfg_filterclear", "Clear MAX22530 channel 1 filter") {}

    void execute(const ArgValue*, CommandContext&) override {
        MAX22530& adc = dcLinkVoltageSensor().adc();
        if (adc.clearFilter(0)) {
            Telemetry::printf("[SHELL] MAX channel 1 filter cleared");
        } else {
            Telemetry::printf("[SHELL] MAX filter clear failed");
        }
    }
};

class MaxCfgRawCommand : public CommandInterface {
public:
    MaxCfgRawCommand() : CommandInterface("maxcfg_raw", "Read raw MAX22530 channel 1") {}

    void execute(const ArgValue*, CommandContext&) override {
        MAX22530& adc = dcLinkVoltageSensor().adc();
        const uint16_t counts = adc.readRawCounts(0);
        const float v = adc.readRawVoltage(0);
        Telemetry::printf("[SHELL] MAX raw ch1 = 0x%03X (%lu counts), %.3f V",
                          counts, static_cast<unsigned long>(counts), static_cast<double>(v));
    }
};

class MaxCfgFilteredCommand : public CommandInterface {
public:
    MaxCfgFilteredCommand() : CommandInterface("maxcfg_filtered", "Read filtered MAX22530 channel 1") {}

    void execute(const ArgValue*, CommandContext&) override {
        MAX22530& adc = dcLinkVoltageSensor().adc();
        const uint16_t counts = adc.readFilteredCounts(0);
        const float v = adc.readFilteredVoltage(0);
        Telemetry::printf("[SHELL] MAX filtered ch1 = 0x%03X (%lu counts), %.3f V",
                          counts, static_cast<unsigned long>(counts), static_cast<double>(v));
    }
};

CommandInterface* makeOcSetCommand()             { static OcSetCommand inst; return &inst; }
CommandInterface* makeHwOcSetCommand()           { static HwOcSetCommand inst; return &inst; }
CommandInterface* makeMaxCfgOvCommand()          { static MaxCfgOvCommand inst; return &inst; }
CommandInterface* makeMaxCfgUvCommand()          { static MaxCfgUvCommand inst; return &inst; }
CommandInterface* makeMaxCfgStatusCommand()      { static MaxCfgStatusCommand inst; return &inst; }
CommandInterface* makeMaxCfgThresholdsCommand()  { static MaxCfgThresholdsCommand inst; return &inst; }
CommandInterface* makeMaxCfgFilterClearCommand() { static MaxCfgFilterClearCommand inst; return &inst; }
CommandInterface* makeMaxCfgRawCommand()         { static MaxCfgRawCommand inst; return &inst; }
CommandInterface* makeMaxCfgFilteredCommand()    { static MaxCfgFilteredCommand inst; return &inst; }
