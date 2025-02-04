# Grove PM2.5 Monitor BLE Project

A [PlatformIO](https://platformio.org/project) project for the Seeed Wio Terminal that reads data from a Grove HM3301 PM2.5 laser dust sensor and broadcasts the readings over Bluetooth Low Energy (BLE).

<img src="docs/images/wio_terminal.jpg" alt="drawing" width="200"/>

## Hardware Requirements

- [Seeed Wio Terminal](https://wiki.seeedstudio.com/Wio_Terminal_Intro/)
- [Grove HM3301 Laser PM2.5 Sensor](https://wiki.seeedstudio.com/Grove-Laser_PM2.5_Sensor-HM3301/)
- I2C grove connection

## Features

- Real-time particulate matter measurements
  - PM1.0
  - PM2.5
  - PM10.0
- BLE connectivity for wireless data transmission
- TFT display showing current readings
- I2C device auto-detection
- Debug information display

## BLE Client/Server Example App
- [Flutter PM2.5 Monitor App](https://github.com/IoT-gamer/flutter_pm2_5_monitor_app)

## Dependencies

- [Seeed_HM330X (Grove PM2.5 Sensor Library)](https://github.com/Seeed-Studio/Seeed_PM2_5_sensor_HM3301)
- [Seeed_Arduino_rpcBLE](https://github.com/Seeed-Studio/Seeed_Arduino_rpcBLE)
- [Seeed_Arduino_rpcUnified](https://github.com/Seeed-Studio/Seeed_Arduino_rpcUnified)