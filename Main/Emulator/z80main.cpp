#include <string.h>
#include <stdio.h>
#include "startup.h"

#include "z80main.h"
#include "z80input.h"
#include "Keyboard/ps2Keyboard.h"

//#include "debug/debugBand.h"

#define CYCLES_PER_STEP 69888

#define RAM_AVAILABLE 0xC000
uint8_t RamBuffer[RAM_AVAILABLE];

Z80_STATE _zxCpu;
CONTEXT _zxContext;
uint16_t _attributeCount;
SpectrumScreen* _spectrumScreen;

int _total;
int _next_total = 0;
uint8_t zx_data = 0;
uint8_t frames = 0;

extern "C"
{
    uint8_t readbyte(uint16_t addr);
    uint16_t readword(uint16_t addr);
    void writebyte(uint16_t addr, uint8_t data);
    void writeword(uint16_t addr, uint16_t data);
    uint8_t input(uint8_t portLow, uint8_t portHigh);
    void output(uint16_t port, uint8_t data);
}

void zx_setup(SpectrumScreen* spectrumScreen)
{
	_spectrumScreen = spectrumScreen;
	_attributeCount = spectrumScreen->Settings.TextColumns * spectrumScreen->Settings.TextRows;

    _zxContext.readbyte = readbyte;
    _zxContext.readword = readword;
    _zxContext.writeword = writeword;
    _zxContext.writebyte = writebyte;
    _zxContext.input = input;
    _zxContext.output = output;

    memset(indata, 0xFF, 128);

    Z80Reset(&_zxCpu);
}

void zx_reset()
{
    Z80Reset(&_zxCpu);
}

int32_t zx_loop()
{
    int32_t result = -1;

    _total += Z80Emulate(&_zxCpu, _next_total - _total, &_zxContext);

    if (_total >= _next_total)
    {
        _next_total += CYCLES_PER_STEP;

        // flash every 32 frames
        frames++;
        if (frames > 31)
        {
            frames = 0;
            for (int i = 0; i < _attributeCount; i++)
            {
                uint16_t color = _spectrumScreen->Settings.Attributes[i];
                if ((color & 0x8080) != 0)
                {
                	_spectrumScreen->Settings.Attributes[i] = __builtin_bswap16(color);
                }
            }
        }

        // Keyboard input
        int32_t scanCode = Ps2_GetScancode();
        if (scanCode > 0)
        {
            char buf[30];

            if ((scanCode & 0xFF00) == 0xF000)
            {
                // key up

                scanCode = ((scanCode & 0xFF0000) >> 8 | (scanCode & 0xFF));

                if (!OnKey(scanCode, true))
                {
                    result = scanCode;
                }

                sprintf(buf, "kup %04lx", scanCode);
                //debug_print(0, 1, buf);
            }
            else
            {
                // key down

                OnKey(scanCode, false);

                sprintf(buf, "kdn %04lx", scanCode);
                //debug_print(0, 0, buf);
            }
        }

        Z80Interrupt(&_zxCpu, 0xff, &_zxContext);
    }

    return result;
}

extern "C" uint8_t readbyte(uint16_t addr)
{
    uint8_t res;
    if (addr >= (uint16_t)0x5B00)
    {
        uint16_t offset = addr - (uint16_t)0x5B00;
        if (offset < RAM_AVAILABLE)
        {
            res = RamBuffer[offset];
        }
        else
        {
            res = 0;
        }
    }
    else if (addr >= (uint16_t)0x5800)
    {
        // Screen Attributes
        res = _spectrumScreen->ToSpectrumColor(_spectrumScreen->Settings.Attributes[addr - (uint16_t)0x5800]);
    }
    else if (addr >= (uint16_t)0x4000)
    {
        // Screen pixels
        res = _spectrumScreen->Settings.Pixels[addr - (uint16_t)0x4000];
    }
    else
    {
        res = ROM[addr];
    }
    return res;
}

extern "C" uint16_t readword(uint16_t addr)
{
    return ((readbyte(addr + 1) << 8) | readbyte(addr));
}

extern "C" void writebyte(uint16_t addr, uint8_t data)
{
    if (addr >= (uint16_t)0x5B00)
    {
        uint16_t offset = addr - (uint16_t)0x5B00;
        if (offset < RAM_AVAILABLE)
        {
            RamBuffer[offset] = data;
        }
    }
    else if (addr >= (uint16_t)0x5800)
    {
        // Screen Attributes
    	_spectrumScreen->Settings.Attributes[addr - (uint16_t)0x5800] = _spectrumScreen->FromSpectrumColor(data);
    }
    else if (addr >= (uint16_t)0x4000)
    {
        // Screen pixels
    	_spectrumScreen->Settings.Pixels[addr - (uint16_t)0x4000] = data;
    }
}

extern "C" void writeword(uint16_t addr, uint16_t data)
{
    writebyte(addr, (uint8_t)data);
    writebyte(addr + 1, (uint8_t)(data >> 8));
}

extern "C" uint8_t input(uint8_t portLow, uint8_t portHigh)
{
    if (portLow == 0xFE)
    {
        switch (portHigh)
        {
        case 0xFE:
        case 0xFD:
        case 0xFB:
        case 0xF7:
        case 0xEF:
        case 0xDF:
        case 0xBF:
        case 0x7F:
            return indata[portHigh - 0x7F];
        }
    }

    uint8_t data = zx_data;
    data |= (0xe0); /* Set bits 5-7 - as reset above */
    data &= ~0x40;
    return data;
}

extern "C" void output(uint16_t port, uint8_t data)
{
    switch (port)
    {
    case 0xFE:
    {
        // border color (no bright colors)
        uint8_t borderColor = (data & 0x7);
        *_spectrumScreen->Settings.BorderColor = _spectrumScreen->FromSpectrumColor(borderColor) >> 8;
    }
    break;
    default:
        zx_data = data;
        break;
    }
}
