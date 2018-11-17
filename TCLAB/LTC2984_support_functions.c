/*!



LTC2984_support_functions.cpp:
This file contains all the support functions used in the main program.


http://www.analog.com/en/products/analog-to-digital-converters/integrated-special-purpose-converters/digital-temperature-sensors/LTC2984.html

http://www.analog.com/en/products/analog-to-digital-converters/integrated-special-purpose-converters/digital-temperature-sensors/LTC2984#product-evaluationkit

$Revision: 1.7.9 $
$Date: September 7, 2018 $
Copyright (c) 2018, Analog Devices, Inc. (ADI)
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

The views and conclusions contained in the software and documentation are those
of the authors and should not be interpreted as representing official policies,
either expressed or implied, of Analog Devices, Inc.

The Analog Devices Linduino is not affiliated with the official Arduino team.
However, the Linduino is only possible because of the Arduino team's commitment
to the open-source community.  Please, visit http://www.arduino.cc and
http://store.arduino.cc , and consider a purchase that will help fund their
ongoing work.
*/

#include <atmel_start.h>
#include <stdint.h>
#include "Linduino.h"
#include "LT_SPI.h"
#include "LTC2984_configuration_constants.h"
#include "LTC2984_table_coeffs.h"
#include "LTC2984_support_functions.h"

// ***********************
// Program the part
// ***********************
void assign_channel(uint8_t chip_select, uint8_t channel_number, uint32_t channel_assignment_data)
{
  uint16_t start_address = get_start_address(CH_ADDRESS_BASE, channel_number);
  transfer_four_bytes(chip_select, WRITE_TO_RAM, start_address, channel_assignment_data);
}


void write_custom_table(uint8_t chip_select, struct table_coeffs coefficients[64], uint16_t start_address, uint8_t table_length)
{
  int8_t i;
  uint32_t coeff;

  output_low(chip_select);

  //SPI.transfer(WRITE_TO_RAM);
  //SPI.transfer(highByte(start_address));
  //SPI.transfer(lowByte(start_address));

  for (i=0; i< table_length; i++)
  {
    coeff = coefficients[i].measurement;
//     SPI.transfer((uint8_t)(coeff >> 16));
//     SPI.transfer((uint8_t)(coeff >> 8));
//     SPI.transfer((uint8_t)coeff);

    coeff = coefficients[i].temperature;
//     SPI.transfer((uint8_t)(coeff >> 16));
//     SPI.transfer((uint8_t)(coeff >> 8));
//     SPI.transfer((uint8_t)coeff);
  }
  output_high(chip_select);
}



// *****************
// Measure channel
// *****************
float measure_channel(uint8_t chip_select, uint8_t channel_number, uint8_t channel_output)
{
    convert_channel(chip_select, channel_number);
    return get_result(chip_select, channel_number, channel_output);
}


void convert_channel(uint8_t chip_select, uint8_t channel_number)
{
  // Start conversion
  transfer_byte(chip_select, WRITE_TO_RAM, COMMAND_STATUS_REGISTER, CONVERSION_CONTROL_BYTE | channel_number);

  wait_for_process_to_finish(chip_select);
}


void wait_for_process_to_finish(uint8_t chip_select)
{
  uint8_t process_finished = 0;
  uint8_t data;
  while (process_finished == 0)
  {
    data = transfer_byte(chip_select, READ_FROM_RAM, COMMAND_STATUS_REGISTER, 0);
    process_finished  = data & 0x40;
  }
}


// *********************************
// Get results
// *********************************
float get_result(uint8_t chip_select, uint8_t channel_number, uint8_t channel_output)
{
  uint32_t raw_data;
  uint8_t fault_data;
  uint16_t start_address = get_start_address(CONVERSION_RESULT_MEMORY_BASE, channel_number);
  uint32_t raw_conversion_result;

  raw_data = transfer_four_bytes(chip_select, READ_FROM_RAM, start_address, 0);

  // 24 LSB's are conversion result
  raw_conversion_result = raw_data & 0xFFFFFF;
  return print_conversion_result(raw_conversion_result, channel_output);

  // If you're interested in the raw voltage or resistance, use the following
 // if (channel_output != VOLTAGE)
 // {
 //   read_voltage_or_resistance_results(chip_select, channel_number);
 // }

  // 8 MSB's show the fault data
  //fault_data = raw_data >> 24;

}


float print_conversion_result(uint32_t raw_conversion_result, uint8_t channel_output)
{
  int32_t signed_data = raw_conversion_result;
  float scaled_result;

  // Convert the 24 LSB's into a signed 32-bit integer
  if(signed_data & 0x800000)
    signed_data = signed_data | 0xFF000000;

  // Translate and print result
  if (channel_output == TEMPERATURE)
  {
    return  (float)(signed_data) / 1024;
  }
  else if (channel_output == VOLTAGE)
  {
    return (float)(signed_data) / 2097152;
  }
  return 0;
}


void read_voltage_or_resistance_results(uint8_t chip_select, uint8_t channel_number)
{
  int32_t raw_data;
  float voltage_or_resistance_result;
  uint16_t start_address = get_start_address(VOUT_CH_BASE, channel_number);

  raw_data = transfer_four_bytes(chip_select, READ_FROM_RAM, start_address, 0);
  voltage_or_resistance_result = (float)raw_data/1024;

}



// *********************
// SPI RAM data transfer
// *********************
// To write to the RAM, set ram_read_or_write = WRITE_TO_RAM.
// To read from the RAM, set ram_read_or_write = READ_FROM_RAM.
// input_data is the data to send into the RAM. If you are reading from the part, set input_data = 0.

uint32_t transfer_four_bytes(uint8_t chip_select, uint8_t ram_read_or_write, uint16_t start_address, uint32_t input_data)
{
  uint32_t output_data;
  uint8_t tx[7], rx[7];

  tx[6] = ram_read_or_write;
//   tx[5] = highByte(start_address);
//   tx[4] = lowByte(start_address);
  tx[3] = (uint8_t)(input_data >> 24);
  tx[2] = (uint8_t)(input_data >> 16);
  tx[1] = (uint8_t)(input_data >> 8);
  tx[0] = (uint8_t) input_data;

  spi_transfer_block(chip_select, tx, rx, 7);

  output_data = (uint32_t) rx[3] << 24 |
                (uint32_t) rx[2] << 16 |
                (uint32_t) rx[1] << 8  |
                (uint32_t) rx[0];

  return output_data;
}


uint8_t transfer_byte(uint8_t chip_select, uint8_t ram_read_or_write, uint16_t start_address, uint8_t input_data)
{
  uint8_t tx[4], rx[4];

  tx[3] = ram_read_or_write;
  tx[2] = (uint8_t)(start_address >> 8);
  tx[1] = (uint8_t)start_address;
  tx[0] = input_data;
  spi_transfer_block(chip_select, tx, rx, 4);
  return rx[0];
}


// ******************************
// Misc support functions
// ******************************
uint16_t get_start_address(uint16_t base_address, uint8_t channel_number)
{
  return base_address + 4 * (channel_number-1);
}


int is_number_in_array(uint8_t number, uint8_t *array, uint8_t array_length)
// Find out if a number is an element in an array
{
  int found = 0;
  for (uint8_t i=0; i< array_length; i++)
  {
    if (number == array[i])
    {
      found = 1;
    }
  }
  return found;
}










