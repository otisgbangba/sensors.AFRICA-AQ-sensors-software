#ifndef _SPH0645_H
#define _SPH0645_H

extern "C" {
#include "user_interface.h"
#include "i2s_reg.h"
#include "slc_register.h"
#include "esp8266_peri.h"
void rom_i2c_writeReg_Mask(int, int, int, int, int, int);
}

//#define DEBUG

#define I2S_CLK_FREQ      160000000  // Hz
#define I2S_24BIT         3     // I2S 24 bit half data
#define I2S_LEFT          2     // I2S RX Left channel

#define I2SI_DATA         12    // I2S data on GPIO12
#define I2SI_BCK          13    // I2S clk on GPIO13
#define I2SI_WS           14    // I2S select on GPIO14

#define SLC_BUF_CNT       8     // Number of buffers in the I2S circular buffer
#define SLC_BUF_LEN       64    // Length of one buffer, in 32-bit words.

/**
 * Convert I2S data.
 * Data is 18 bit signed, MSBit first, two's complement.
 * Note: We can only send 31 cycles from ESP8266 so we only
 * shift by 13 instead of 14.
 * The 240200 is a magic calibration number I haven't figured
 * out yet.
 */
#define convert(sample) (((int32_t)(sample) >> 13) - 240200)

#endif //_SPH0645_H