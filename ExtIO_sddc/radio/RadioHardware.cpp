#include "RadioHandler.h"

bool RadioHardware::FX3SetGPIO(int32_t mask)
{
    gpios |= mask;

    return Fx3->Control(GPIOFX3, (uint8_t*)&gpios);
}

bool RadioHardware::FX3UnsetGPIO(int32_t mask)
{
    gpios &= ~mask;

    return Fx3->Control(GPIOFX3, (uint8_t*)&gpios);
}

RadioHardware::~RadioHardware()
{
    FX3SetGPIO(SHDWN);

    Fx3->Control(RESETFX3);

    delete Fx3;
}