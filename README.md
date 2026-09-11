# smartOptSensor

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Platform: ESP32-C3](https://img.shields.io/badge/Platform-ESP32--C3-orange.svg)](https://www.espressif.com/en/products/socs/esp32-c3)
[![Protocol: Matter](https://img.shields.io/badge/Protocol-Matter-green.svg)](https://csa-iot.org/all-solutions/matter/)
[![Language: English / 日本語](https://img.shields.io/badge/Language-English%20%2F%20日本語-blue)](README_ja.md)

**English** | [日本語 (Japanese)](README_ja.md)

Matter-compatible Ambient Light Sensor firmware built on **ESP32-C3** using a CdS photoresistor (light-dependent resistor / LDR).  
It measures ambient light via a voltage divider circuit on the ADC, converts the readings to Illuminance (Lux) and Matter standard `IlluminanceMeasurement` cluster attributes (`MeasuredValue`), and publishes them over Matter over Wi-Fi.

---

## 1. System Specifications

- **MCU**: ESP32-C3 (RISC-V single-core, 160MHz)
- **Connectivity**: Matter over Wi-Fi (2.4GHz 802.11 b/g/n)
- **Matter Device Type**: Light Sensor (`0x0106`)
- **Matter Cluster**: Illuminance Measurement (`0x0400`)
- **Sensor**: CdS Photoresistor (GL5528 or standard photoconductive cell)
- **Sampling & Reporting**:
  - 1,000ms (1s) sampling cycle
  - 10-sample multi-sample averaging with 1ms delay for AC flicker mitigation
  - Optimized Matter attribute updates: Deadband control ($\Delta \ge 50$) + 60s periodic heartbeat
- **Factory Reset**: Hold GPIO9 button for 3 seconds or run `idf.py erase-flash`

---

## 2. Circuit Diagram & Pinout

### Pin Mapping
| ESP32-C3 Pin | Connected To | Description |
| :--- | :--- | :--- |
| **3.3V** | One leg of CdS cell | Power Supply |
| **GPIO1 (ADC1_CH1)** | Junction between CdS and 10kΩ resistor | ADC Voltage Measurement Input |
| **GND** | One leg of 10kΩ resistor | Ground |

### Circuit Schematic

```text
       +3.3V
         |
       +---+
       |   |
       |CdS| (Photoresistor / LDR)
       |   |
       +---+
         |
         +--------------------> GPIO1 (ESP32-C3 ADC1_CH1)
         |
       +---+
       |   |
       |10k| (Fixed Resistor 10kΩ)
       |   |
       +---+
         |
        GND
```

#### Operating Principle

- **Bright ambient**: CdS resistance decreases ($\le 1\text{k}\Omega$) $\rightarrow$ GPIO1 voltage rises towards 3.3V.
- **Dark ambient**: CdS resistance increases ($100\text{k}\Omega \sim 1\text{M}\Omega$) $\rightarrow$ GPIO1 voltage drops towards 0V.

---

## 3. Calculation & Conversion Formulas

1. **ADC Voltage Measurement ($V_{out}$)**:  
   Calibrated voltage (mV) acquired from ESP32-C3 ADC1_CH1 (GPIO1) with curve-fitting calibration.

2. **CdS Resistance Calculation ($R_{CdS}$)**:  
   $$R_{CdS} = 10000 \times \left( \frac{3300}{V_{out}} - 1 \right) \quad [\Omega]$$

3. **Illuminance Calculation ($\text{Lux}$)**:  
   Calculated using empirical calibration points:
   - Under indoor lighting: $\text{RAW} \approx 2600$, $R_{CdS} \approx 7.25\text{k}\Omega \rightarrow 300\text{ Lux}$
   - Dark environment: $\text{RAW} \approx 660$, $R_{CdS} \approx 58.7\text{k}\Omega \rightarrow 1\text{ Lux}$  
   $$\text{Lux} = 300.0 \times \left( \frac{7250}{R_{CdS}} \right)^{2.727}$$

4. **Matter MeasuredValue Conversion**:  
   Conforms to the Matter Specification (Cluster `0x0400`, Nullable uint16):  
   $$\text{MeasuredValue} = 10000 \times \log_{10}(\text{Lux}) + 1$$

---

## 4. Build, Flash & Commissioning Setup

### 1. Load Environment Variables

```bash
source ~/esp/esp-idf/export.sh
source ~/esp/esp-matter/export.sh
```

### 2. Build Firmware

```bash
idf.py set-target esp32c3
idf.py build
```

### 3. Initial Flash & Matter Commissioning Reset

When commissioning for the first time or clearing previous fabric credentials, erase the flash completely:

```bash
# Erase flash memory (clears previous fabric / pairing info)
idf.py -p /dev/ttyACM0 erase-flash

# Flash firmware and launch serial monitor
idf.py -p /dev/ttyACM0 flash monitor
```

---

## 5. Matter Pairing (Onboarding) Guide

Once booted, the serial monitor displays onboarding codes:

1. **Manual Pairing Code**:  
   Enter the 11-digit code (e.g., `34970112332`) into your smart home app.
2. **QR Code**:  
   Open the printed URL (e.g., `https://project-chip.github.io/connectedhomeip/qrcode.html?data=...`) in a browser and scan the QR code.
3. **Commissioning Steps**:  
   - Ensure your smartphone is connected to the same 2.4GHz Wi-Fi network configured in `main/wifi_creds.h`.
   - In Google Home, Apple Home, or your preferred Matter controller app, select **Add Device** $\rightarrow$ **Matter Device**, and enter the code or scan the QR code. The device will be registered as a **Light Sensor**.

---

## 6. Serial Output Diagnostics

During runtime, diagnostic logs are output every 1000ms (115200 baud). Matter attribute updates are published when the illuminance change exceeds the deadband ($\ge 50$) or when the 60-second heartbeat interval expires.

```text
I (1250) app_main: [CdS Sensor] RAW: 2609 | Volt: 1913 mV | Res:  7250.4 Ohm | Lux:   300.0 | MatterVal: 24772
I (1260) esp_matter_attribute: ********** W : Endpoint 0x0001's Cluster 0x00000400's Attribute 0x00000000 is 24772 **********
I (2250) app_main: [CdS Sensor] RAW: 2610 | Volt: 1914 mV | Res:  7245.0 Ohm | Lux:   300.5 | MatterVal: 24779
```

- **RAW**: 12-bit raw ADC reading (0 ~ 4095)
- **Volt**: Measured voltage (mV)
- **Res**: Calculated CdS resistance ($\Omega$)
- **Lux**: Calibrated illuminance ($\text{Lux}$)
- **MatterVal**: Matter `MeasuredValue` attribute

---

## 7. Wi-Fi Configuration

Pre-configure Wi-Fi SSID and Password in `main/wifi_creds.h` for automatic connection.

---

## 8. Google Home Script Editor Example

```yaml
metadata:
  name: Broadcast when dark
  description: Broadcast an announcement when light level drops below 50 lux
automations:
  - starters:
      - type: device.state.SensorState
        device: Light Level - Living Room
        # Note: Sensor state name may vary depending on ecosystem configuration
        state: currentSensorStateData.LightLevel.rawValue
        lessThanOrEqualTo: 50
    actions:
      - type: assistant.command.Broadcast
        message: "It has become dark"
```

---

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
