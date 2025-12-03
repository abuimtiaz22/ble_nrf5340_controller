# BLE-Controlled Embedded System for CMOS LD–SPAD–TDC Module  
nRF5340 Firmware | GPIO Timing | BLE Interface | Battery-Powered Control Board

This project implements the embedded firmware and host scripts for a **wireless control board** operating a CMOS optical transceiver containing a Laser Driver (LD), SPAD array, TDC, and time-gating circuitry.  
The system is built around the **Raytac MDBT53-P1M module (nRF5340)** and communicates over BLE with a MATLAB or Python host.

---

## 🔌 Hardware Overview

The control board is powered by a **3.7 V Li-ion battery** and generates all necessary supply rails for the CMOS chip:

- **5 V output** from DC/DC converter  
- **Up to 25 V high-voltage rail** for SPAD bias (boost regulator)  
- **Low-noise analog supply** via LDO  
- **nRF5340 module** (BLE + GPIO control)  
- **I/O header** for 14-bit data input and timing/control signals  

The board replaces wired lab control and enables portable optical measurements.

---

## 📡 Firmware Features (nRF Connect SDK / Zephyr)

- Custom **BLE service** (16-bit UUIDs) for command + data transfer  
- **GPIO timing generation** for LD trigger, pulse, reset, and custom gating  
- **Clock generation** for synchronous SPAD/TDC operation  
- **Parallel data capture**: 14 lines × 128 samples  
- **Binary data packaging** into BLE notifications  
- **Low-latency loops** for optical timing experiments  

### Supported Command Loops
- **01:** Reset, Reset Counter, Bias and Stability Control  
- **02:** 96-bit GPIO waveform output  
- **03:** Clock + alternating pulse/reset generation  
- **04:** 14×128 parallel SPAD/TDC data acquisition and BLE transfer  

---

## 🧪 Host-Side Tools

### MATLAB
- BLE connect/write/notify  
- Data reconstruction  
- 96-bit signal plotting  
- 14×128 matrix visualization  

### Python (Bleak)
- BLE command sending  
- Notification parsing  
- Logging & analysis  

---

## 🛠 Technologies Used
- **nRF5340 (MDBT53-P1M)**  
- **C (Zephyr RTOS / nRF Connect SDK)**  
- **BLE GATT**  
- **MATLAB / Python**  
- **Custom CMOS LD–SPAD–TDC chip**  
- **Battery-powered mixed-signal hardware**

---

## 📄 Purpose

This embedded system provides a compact, wireless interface for controlling and reading a CMOS optical sensing chip used in **time-domain diffuse optics** research.  
It demonstrates practical experience in BLE firmware, GPIO timing, mixed-signal board integration, and embedded data acquisition.

