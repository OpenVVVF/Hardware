#include "Inverter/InverterMain.h"
#include "main.h"

namespace InverterMain {

static void init()
{
}

static void loop()
{
}

} // namespace InverterMain

extern "C" void InverterMain_Run(void)
{
    InverterMain::init();
    while (1) {
        InverterMain::loop();
    }
}
