# smart-home-espnow — Project Brief

## Goal
Build a relatively simple modular kit for remote control of 10-12 switches via ESP-NOW with Home Assistant integration over MQTT.

## Target Architecture

### Gateway
- MCU: ESP32-WROOM for gateway
- Ethernet: W5500 (SPI)
- Functions:
  - Discovery and communication with ESP-NOW node modules
  - Publishing states and receiving commands via MQTT
  - Home Assistant integration
  - Local web UI for configuration
  - Support for 8 onboard GPIOs, each configurable as input or output. If output, it can be used to produce series of pulses to control something (from 1 to 5 with period of 100ms).
  - DHCP or static IP (by default DHCP, but can be configured via web UI)

### Node / Switch Module
- MCU: ESP32-WROOM or ESP8266
- Functions:
  - Relay control
  - Response to momentary button press
  - Response to latching physical switch
  - Receiving commands from gateway
  - Sending current state back to gateway

## Functional Requirements
- 10-12 controllable switches
- Node states must survive gateway/node restarts gracefully
- After power-on, a node must re-register with the gateway automatically
- Home Assistant operates via MQTT
- Gateway must have a simple built-in web UI for basic configuration

## Gateway Configuration Fields
- DHCP / static IP
- IP address
- Netmask
- Gateway
- DNS
- MQTT broker host
- MQTT port
- MQTT username
- MQTT password
- MQTT base topic
- 8 local GPIOs, each with:
  - Mode: disabled / input / output
  - Pull-up / none
  - Invert
  - Pulse mode

## Current Implementation Direction
The project is built as an ESP-IDF gateway application with the following components:
- `config_store` - settings storage in NVS
- `eth_mgr` - Ethernet / W5500 driver
- `mqtt_bridge` - MQTT layer
- `espnow_master` - ESP-NOW gateway/master logic
- `node_table` - node registry table
- `local_io` - gateway local GPIO management
- `ha_discovery` - Home Assistant discovery
- `web_cfg` - web configuration UI
- `sys_status` - system status
