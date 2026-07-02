#include "Inverter/Calibration/MotorCalibration.h"

namespace Inverter {

static MotorCalibration s_instance;

MotorCalibration& MotorCalibration::instance() {
    return s_instance;
}

MotorCalibration& motorCalibration() {
    return MotorCalibration::instance();
}

} // namespace Inverter
