# Functional Specification Document
## smart-home-espnow

**Version:** 1.0
**Date:** 2026-03-24
**Status:** Draft

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [System Overview](#2-system-overview)
3. [Architecture](#3-architecture)
4. [Gateway Specification](#4-gateway-specification)
5. [Node / Switch Module Specification](#5-node--switch-module-specification)
6. [ESP-NOW Communication Protocol](#6-esp-now-communication-protocol)
7. [MQTT Interface](#7-mqtt-interface)
8. [Home Assistant Integration](#8-home-assistant-integration)
9. [Local GPIO Management](#9-local-gpio-management)
10. [Web Configuration UI](#10-web-configuration-ui)
11. [Configuration Storage](#11-configuration-storage)
12. [State Persistence and Recovery](#12-state-persistence-and-recovery)
13. [Component Interfaces](#13-component-interfaces)

---

## 1. Introduction

### 1.1 Purpose

This document specifies the functional requirements and design for **smart-home-espnow**, a modular ESP-NOW-based smart home switch controller with Home Assistant integration via MQTT.

### 1.2 Scope

The system supports control of 10–12 remote switch nodes from a central gateway. The gateway bridges ESP-NOW (local wireless) to MQTT (home network), enabling Home Assistant automation and a local web UI for configuration.

### 1.3 Definitions

| Term | Definition |
|------|------------|
| Gateway | Central ESP32 device with Ethernet and ESP-NOW master role |
| Node | Remote ESP32/ESP8266 device controlling one relay/switch |
| ESP-NOW | Espressif peer-to-peer wireless protocol (connectionless, ~1km range) |
| MQTT | Lightweight pub/sub messaging protocol used by Home Assistant |
| HA | Home Assistant home automation platform |
| NVS | Non-Volatile Storage — ESP-IDF key/value flash storage |

---

## 2. System Overview

### 2.1 Concept

```
┌──────────────────────────────────────────────────────┐
│                   Home Network                        │
│                                                       │
│  ┌────────────┐    MQTT    ┌─────────────────┐       │
│  │    Home    │◄──────────►│    Gateway      │       │
│  │ Assistant  │            │  ESP32-WROOM    │       │
│  └────────────┘            │  + W5500 (SPI)  │       │
│                            │                 │       │
│                            │  8x Local GPIO  │       │
│                            └────────┬────────┘       │
└─────────────────────────────────────┼────────────────┘
                                      │ ESP-NOW
                    ┌─────────────────┼─────────────────┐
                    │                 │                  │
             ┌──────▼──────┐  ┌──────▼──────┐  ┌───────▼─────┐
             │   Node 1    │  │   Node 2    │  │  Node N     │
             │  ESP32/8266 │  │  ESP32/8266 │  │  ESP32/8266 │
             │   + Relay   │  │   + Relay   │  │   + Relay   │
             └─────────────┘  └─────────────┘  └─────────────┘
```

### 2.2 Key Constraints

- Maximum 10–12 switch nodes
- Gateway requires Ethernet connectivity (W5500 SPI module)
- Nodes communicate exclusively over ESP-NOW (no Wi-Fi AP/STA connection)
- All persistent configuration stored in NVS
- Node states must survive power cycles and restarts on both sides

---

## 3. Architecture

### 3.1 Gateway Software Components

| Component | Module | Responsibility |
|-----------|--------|----------------|
| Configuration Store | `config_store` | Read/write settings from NVS |
| Ethernet Manager | `eth_mgr` | W5500 SPI driver, DHCP/static IP |
| MQTT Bridge | `mqtt_bridge` | MQTT client, pub/sub, HA topics |
| ESP-NOW Master | `espnow_master` | Node discovery, command dispatch, state reception |
| Node Table | `node_table` | Registry of known nodes and their current state |
| Local I/O | `local_io` | Gateway onboard GPIO management |
| HA Discovery | `ha_discovery` | Publish Home Assistant MQTT discovery messages |
| Web Config | `web_cfg` | HTTP server serving configuration UI |
| System Status | `sys_status` | Runtime health, uptime, diagnostics |

### 3.2 Component Dependency Graph

```
web_cfg ──────────────► config_store
ha_discovery ──────────► node_table, mqtt_bridge
espnow_master ─────────► node_table, mqtt_bridge
mqtt_bridge ───────────► eth_mgr, config_store
local_io ──────────────► config_store, mqtt_bridge
eth_mgr ───────────────► config_store
sys_status ────────────► all components (read-only)
```

---

## 4. Gateway Specification

### 4.1 Hardware

| Property | Value |
|----------|-------|
| MCU | ESP32-WROOM |
| Network | W5500 Ethernet via SPI |
| Local GPIO | 8 configurable pins |
| Framework | ESP-IDF |

### 4.2 Boot Sequence

1. Initialize NVS and load configuration (`config_store`)
2. Initialize Ethernet (`eth_mgr`) — apply DHCP or static IP
3. Wait for IP address (timeout: 30 s)
4. Start MQTT client (`mqtt_bridge`)
5. Initialize local GPIO (`local_io`) with saved configuration
6. Start ESP-NOW master (`espnow_master`)
7. Publish HA discovery messages (`ha_discovery`)
8. Start web configuration server (`web_cfg`)
9. Start system status reporting (`sys_status`)

### 4.3 eth_mgr

**Purpose:** Abstract Ethernet connectivity over W5500.

**Responsibilities:**
- Initialize W5500 over SPI
- Apply network configuration from `config_store` (DHCP or static)
- Expose IP-ready event to dependent components
- Handle link loss and reconnection transparently

**Configuration parameters consumed:**

| Key | Type | Description |
|-----|------|-------------|
| `net.dhcp` | bool | Enable DHCP |
| `net.ip` | string | Static IP address |
| `net.mask` | string | Subnet mask |
| `net.gw` | string | Default gateway |
| `net.dns` | string | DNS server |

### 4.4 mqtt_bridge

**Purpose:** MQTT client bridging ESP-NOW node states and HA commands.

**Responsibilities:**
- Connect to broker using credentials from `config_store`
- Subscribe to command topics for all registered nodes and local GPIOs
- Publish state changes for nodes and local GPIOs
- Re-subscribe and republish states after reconnect
- Deliver inbound commands to `espnow_master` or `local_io`

**Behavior on broker reconnect:**
- Re-subscribe to all command topics
- Re-publish current state of all nodes and GPIOs

### 4.5 espnow_master

**Purpose:** ESP-NOW master; manages node lifecycle and message exchange.

**Responsibilities:**
- Listen for node registration broadcasts
- Register new nodes in `node_table`
- Forward ON/OFF commands from `mqtt_bridge` to the target node via ESP-NOW
- Receive state updates from nodes and update `node_table`
- Trigger MQTT state publish via `mqtt_bridge` on state change

**Node registration flow:**

```
Node boots → broadcasts REGISTER(mac, node_id)
Gateway receives → adds to node_table → sends ACK
Gateway publishes HA discovery for new node
```

**Command flow:**

```
HA publishes command → mqtt_bridge receives
→ espnow_master looks up node MAC in node_table
→ sends ESP-NOW CMD packet to node
→ node applies state → sends STATE_REPORT back
→ espnow_master updates node_table
→ mqtt_bridge publishes updated state
```

### 4.6 node_table

**Purpose:** In-memory registry of all known nodes.

**Node record fields:**

| Field | Type | Description |
|-------|------|-------------|
| `mac` | uint8[6] | ESP-NOW MAC address |
| `node_id` | uint8 | Logical ID (1–12) |
| `state` | bool | Last known relay state |
| `online` | bool | Whether node is currently reachable |
| `last_seen` | uint32 | Timestamp of last message (ms) |

**Persistence:** Node table is rebuilt from NVS on gateway restart. Nodes re-register after their own restart.

### 4.7 ha_discovery

**Purpose:** Publish MQTT discovery payloads so Home Assistant auto-discovers all entities.

- Publishes one `switch` entity per node
- Publishes one entity per enabled local GPIO (input → `binary_sensor`, output → `switch`)
- Re-publishes all discovery messages on MQTT reconnect
- Uses base topic from configuration

### 4.8 sys_status

**Purpose:** System diagnostics and health.

- Publishes uptime, free heap, and IP address periodically via MQTT
- Exposes current status via web UI

---

## 5. Node / Switch Module Specification

### 5.1 Hardware

| Property | Value |
|----------|-------|
| MCU | ESP32-WROOM or ESP8266 |
| Output | 1× relay |
| Input | 1× momentary button (optional) |
| Input | 1× latching physical switch (optional) |
| Framework | ESP-IDF (ESP32) or ESP8266 RTOS SDK |

### 5.2 Boot Sequence

1. Initialize NVS (load last known relay state)
2. Restore relay to last known state
3. Start ESP-NOW (set gateway MAC as peer)
4. Broadcast `REGISTER` message
5. Wait for `ACK` from gateway (retry with backoff)
6. Start input polling / ISR for button and switch

### 5.3 Input Handling

#### Momentary Button

- Detected on falling edge (or rising edge with pull-up)
- Toggles relay state on each press
- Sends `STATE_REPORT` to gateway after toggle
- Debounce: 50 ms minimum

#### Latching Physical Switch

- State tracked as `open` / `closed`
- Relay follows switch position (closed = ON, open = OFF), or inverted per config
- Sends `STATE_REPORT` on state change

#### Priority

Physical inputs take priority. A command from the gateway that contradicts an active latching switch is accepted but may be immediately overridden on next switch state change.

### 5.4 Node State Persistence

- Relay state written to NVS on every state change
- On power-on, relay is restored to NVS state before gateway contact is established
- This prevents unwanted state changes during gateway downtime

### 5.5 Node Re-registration

- On boot, node broadcasts `REGISTER` regardless of prior registration
- If no `ACK` received within 5 s, retries with exponential backoff (max 60 s)
- Node operates autonomously (physical inputs work) even without gateway connection

---

## 6. ESP-NOW Communication Protocol

### 6.1 Message Format

All messages are binary-packed structs sent as ESP-NOW payloads.

```c
typedef struct {
    uint8_t  msg_type;    // Message type (enum below)
    uint8_t  node_id;     // Logical node ID (1–12), 0 = gateway
    uint8_t  seq;         // Sequence number (wrap-around)
    uint8_t  payload[8];  // Type-specific payload
} espnow_msg_t;           // Total: 11 bytes
```

### 6.2 Message Types

| Value | Name | Direction | Description |
|-------|------|-----------|-------------|
| `0x01` | `REGISTER` | Node → Gateway | Node announces itself after boot |
| `0x02` | `ACK` | Gateway → Node | Gateway confirms registration |
| `0x03` | `CMD` | Gateway → Node | Relay command (ON/OFF/TOGGLE) |
| `0x04` | `STATE_REPORT` | Node → Gateway | Node reports current relay state |
| `0x05` | `PING` | Gateway → Node | Keepalive check |
| `0x06` | `PONG` | Node → Gateway | Keepalive response |

### 6.3 REGISTER Payload

```c
struct {
    uint8_t  node_id;       // Requested logical ID
    uint8_t  capabilities;  // Bitmask: bit0=relay, bit1=button, bit2=latch_sw
    uint8_t  reserved[6];
};
```

### 6.4 CMD Payload

```c
struct {
    uint8_t  action;   // 0=OFF, 1=ON, 2=TOGGLE
    uint8_t  reserved[7];
};
```

### 6.5 STATE_REPORT Payload

```c
struct {
    uint8_t  relay_state;  // 0=OFF, 1=ON
    uint8_t  input_state;  // 0=open, 1=closed (for latching switch)
    uint8_t  reserved[6];
};
```

### 6.6 Keepalive

- Gateway sends `PING` to each registered node every 30 s
- Node must respond with `PONG` within 5 s
- After 3 missed pongs, node is marked `offline` in `node_table`
- Gateway publishes `offline` availability status to MQTT

---

## 7. MQTT Interface

### 7.1 Topic Structure

Base topic is configurable (default: `espnow`).

```
{base}/node/{node_id}/state          ← gateway publishes (ON/OFF)
{base}/node/{node_id}/command        ← gateway subscribes (ON/OFF)
{base}/node/{node_id}/availability   ← gateway publishes (online/offline)

{base}/gpio/{pin}/state              ← gateway publishes
{base}/gpio/{pin}/command            ← gateway subscribes (outputs only)

{base}/status                        ← system status JSON (periodic)
```

### 7.2 Payloads

| Topic suffix | Payload values |
|-------------|----------------|
| `state` | `ON` / `OFF` |
| `command` | `ON` / `OFF` |
| `availability` | `online` / `offline` |

### 7.3 MQTT Connection Parameters

| Parameter | Default |
|-----------|---------|
| Port | 1883 |
| QoS (state/command) | 1 |
| Retain (state) | true |
| Retain (availability) | true |
| Keep-alive | 60 s |
| LWT topic | `{base}/status` |
| LWT payload | `{"status":"offline"}` |

---

## 8. Home Assistant Integration

### 8.1 Discovery Topic Pattern

```
homeassistant/switch/{node_id}/config
homeassistant/switch/gpio_{pin}/config
homeassistant/binary_sensor/gpio_{pin}/config
```

### 8.2 Switch Discovery Payload (per node)

```json
{
  "name": "Switch {node_id}",
  "unique_id": "espnow_node_{node_id}",
  "state_topic": "{base}/node/{node_id}/state",
  "command_topic": "{base}/node/{node_id}/command",
  "availability_topic": "{base}/node/{node_id}/availability",
  "payload_on": "ON",
  "payload_off": "OFF",
  "retain": true,
  "device": {
    "identifiers": ["espnow_node_{node_id}"],
    "name": "ESP-NOW Node {node_id}",
    "manufacturer": "smart-home-espnow"
  }
}
```

### 8.3 Discovery Trigger

Discovery messages are published:
- On gateway boot (after MQTT connects)
- On MQTT reconnect
- When a new node registers for the first time

---

## 9. Local GPIO Management

### 9.1 Overview

The gateway exposes 8 configurable GPIO pins for direct control without requiring a node.

### 9.2 Per-Pin Configuration

| Parameter | Values | Description |
|-----------|--------|-------------|
| `mode` | `disabled` / `input` / `output` | Pin function |
| `pull` | `none` / `pull_up` | Internal pull resistor |
| `invert` | `true` / `false` | Invert logic level |
| `pulse_mode` | `true` / `false` | Output: pulse train mode |
| `pulse_count` | 1–5 | Number of pulses per command |
| `pulse_period_ms` | 100 | Fixed pulse period (ms) |

### 9.3 Input Behavior

- Pin state sampled and debounced (50 ms)
- State change publishes to `{base}/gpio/{pin}/state`
- Invert flag applied before publish

### 9.4 Output Behavior

**Normal mode:** Pin driven HIGH/LOW based on MQTT command.

**Pulse mode:** On command receipt, the pin produces `pulse_count` pulses at 100 ms period (50 ms HIGH, 50 ms LOW), then returns to LOW.

---

## 10. Web Configuration UI

### 10.1 Purpose

Simple HTTP interface for configuring the gateway without needing MQTT or Home Assistant access.

### 10.2 Pages / Endpoints

| URL | Method | Description |
|-----|--------|-------------|
| `/` | GET | Dashboard: system status, node list |
| `/config` | GET | Configuration form |
| `/config` | POST | Save configuration, trigger restart if needed |
| `/nodes` | GET | Node registry table (read-only) |
| `/gpio` | GET | Local GPIO status |
| `/restart` | POST | Trigger gateway restart |

### 10.3 Configuration Form Fields

**Network:**
- DHCP enable (checkbox)
- IP address
- Subnet mask
- Gateway
- DNS

**MQTT:**
- Broker host
- Port
- Username
- Password (masked)
- Base topic

**Local GPIO (repeated for pins 0–7):**
- Mode: disabled / input / output
- Pull-up enable
- Invert
- Pulse mode enable
- Pulse count (1–5, shown only when pulse mode enabled)

### 10.4 Behavior

- No authentication required (local network assumed trusted)
- Configuration saved to NVS on POST
- Network/MQTT changes take effect after restart
- GPIO changes applied immediately without restart

---

## 11. Configuration Storage

### 11.1 NVS Namespace Layout

| Namespace | Keys | Description |
|-----------|------|-------------|
| `net` | `dhcp`, `ip`, `mask`, `gw`, `dns` | Network settings |
| `mqtt` | `host`, `port`, `user`, `pass`, `topic` | MQTT settings |
| `gpio{n}` | `mode`, `pull`, `invert`, `pulse`, `pcnt` | GPIO pin config (n=0–7) |
| `nodes` | `count`, `mac{n}`, `id{n}` | Known node registry |

### 11.2 Default Values

| Parameter | Default |
|-----------|---------|
| DHCP | enabled |
| MQTT port | 1883 |
| MQTT base topic | `espnow` |
| GPIO mode | disabled |
| GPIO pull | none |
| GPIO invert | false |
| GPIO pulse mode | false |
| GPIO pulse count | 1 |

---

## 12. State Persistence and Recovery

### 12.1 Gateway Restart Recovery

1. `config_store` loads all settings from NVS
2. `node_table` reloads known node MACs from NVS
3. On MQTT connect, gateway publishes `availability: online` for all known nodes
4. Nodes that have not re-registered are marked `offline` until their `REGISTER` arrives
5. HA discovery re-published for all known nodes

### 12.2 Node Restart Recovery

1. Node reads last relay state from NVS and applies it immediately
2. Node broadcasts `REGISTER` to gateway
3. Gateway updates `last_seen`, marks node `online`
4. Node sends `STATE_REPORT` so gateway and HA have current state

### 12.3 Network Outage Recovery

- `eth_mgr` handles link loss internally; reconnects when link restored
- `mqtt_bridge` re-establishes connection and re-subscribes automatically
- No manual intervention required

### 12.4 ESP-NOW Link Loss

- Gateway marks node `offline` after 3 missed pings
- MQTT `availability: offline` published for affected node
- HA shows entity as unavailable
- Recovery automatic on next `PONG` received

---

## 13. Component Interfaces

### 13.1 config_store API

```c
esp_err_t config_store_init(void);
esp_err_t config_store_get_net(net_config_t *out);
esp_err_t config_store_set_net(const net_config_t *cfg);
esp_err_t config_store_get_mqtt(mqtt_config_t *out);
esp_err_t config_store_set_mqtt(const mqtt_config_t *cfg);
esp_err_t config_store_get_gpio(int pin, gpio_cfg_t *out);
esp_err_t config_store_set_gpio(int pin, const gpio_cfg_t *cfg);
esp_err_t config_store_get_nodes(node_entry_t *entries, int *count);
esp_err_t config_store_save_nodes(const node_entry_t *entries, int count);
```

### 13.2 mqtt_bridge API

```c
esp_err_t mqtt_bridge_init(void);
esp_err_t mqtt_bridge_publish(const char *topic, const char *payload, int qos, bool retain);
esp_err_t mqtt_bridge_subscribe(const char *topic, mqtt_callback_t cb);
esp_err_t mqtt_bridge_publish_node_state(uint8_t node_id, bool state);
esp_err_t mqtt_bridge_publish_node_avail(uint8_t node_id, bool online);
esp_err_t mqtt_bridge_publish_gpio_state(int pin, bool state);
```

### 13.3 espnow_master API

```c
esp_err_t espnow_master_init(void);
esp_err_t espnow_master_send_cmd(uint8_t node_id, uint8_t action);
esp_err_t espnow_master_ping_all(void);
```

### 13.4 node_table API

```c
esp_err_t node_table_init(void);
esp_err_t node_table_register(const uint8_t *mac, uint8_t node_id, uint8_t caps);
node_entry_t *node_table_find_by_id(uint8_t node_id);
node_entry_t *node_table_find_by_mac(const uint8_t *mac);
esp_err_t node_table_update_state(uint8_t node_id, bool relay_state);
esp_err_t node_table_set_online(uint8_t node_id, bool online);
int node_table_count(void);
```

### 13.5 local_io API

```c
esp_err_t local_io_init(void);
esp_err_t local_io_set_output(int pin, bool state);
esp_err_t local_io_trigger_pulse(int pin);
bool local_io_get_input(int pin);
esp_err_t local_io_reconfigure(int pin, const gpio_cfg_t *cfg);
```

---

*End of Functional Specification Document*
