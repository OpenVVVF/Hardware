#pragma once
#include <cstdint>

namespace Hardware {
    namespace Pins {
        constexpr uint8_t U_A = 16;
        constexpr uint8_t U_B = 17;
        constexpr uint8_t V_A = 18;
        constexpr uint8_t V_B = 19;
        constexpr uint8_t W_A = 20;
        constexpr uint8_t W_B = 21;
        
        //MAX2253x
        namespace SPI {
            constexpr uint8_t SCK = 10;
            constexpr uint8_t MOSI = 11;
            constexpr uint8_t MISO = 12;
        }
    }

    namespace Limits {
        namespace Switching {
            constexpr uint32_t MAX_HZ = 15000;
            constexpr uint32_t MIN_HZ = 250;
        }
        
        namespace Fundamental {
            constexpr int16_t MAX_HZ = 500;
            constexpr int16_t MIN_HZ = -500;
            constexpr uint16_t MAX_RAMP_HZ_S = 500;
        }
    }

    namespace Commutation {
        constexpr uint8_t MAX_ZONES = 12;
        constexpr uint16_t DEFAULT_HZ = 2000;
    }
}