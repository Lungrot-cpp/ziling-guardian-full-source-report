# MLX90640 calibration library

Copied from the user's sensor package:

`MLX90640-IIC Thermal/Mlx90640_thermal_camera/STM32/serial_output/STM32H7_MLX90640/Hardware/mlx90640/functions/MLX90640_API.cpp`

The original Melexis Apache-2.0 copyright and license notice remain in the source.
The accompanying API header replaces the STM32 `main.h` include with `stdint.h`.
The I2C adapter uses this project's memory-mapped open-drain GPIO.

Local algorithm changes: bound the three coefficient-normalization loops to
31 iterations and skip multiplication when the largest coefficient is zero.
This prevents an all-zero compensation table or corrupt EEPROM from hanging
the sensor processor. Temperature equations are unchanged.
