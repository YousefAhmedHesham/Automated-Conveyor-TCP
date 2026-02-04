# Automated Conveyor Belt with Custom TCP Communication 🏭📶

![ESP32](https://img.shields.io/badge/Hardware-ESP32-green) ![Language](https://img.shields.io/badge/Code-C++%20%7C%20Python%20%7C%20MATLAB-blue) ![Protocol](https://img.shields.io/badge/Protocol-Custom%20TCP%20%28JSON%29-orange)

## 📖 Project Overview
This project implements an **Automated Conveyor Belt System** driven by a DC motor with encoder feedback and monitored by IR/Limit sensors. The core innovation is a **robust, custom TCP protocol** designed to handle network congestion and active fault reporting.

The system features a 3-tier architecture:
1.  **ESP32 (Server):** Controls hardware, broadcasts status, and reports faults.
2.  **Raspberry Pi/Jetson (Gateway):** Acts as a middleware bridge, visualizing telemetry and simulating network lag.
3.  **MATLAB (Client GUI):** Provides a dashboard for remote control (PWM, Targets) and live data visualization.

## 📺 Project Video
https://github.com/user-attachments/assets/94d9ce3a-01ec-4b65-80c5-724a0d5f85d1



## ✨ Key Features
* **Custom TCP Protocol:** JSON-based communication with Sequence IDs, Timestamps, and Acknowledgment (ACK) logic to calculate Round-Trip Time (RTT).
* **Active Fault Reporting:** The ESP32 proactively pushes `FAULT` packets for Motor Stalls, Sensor Failures, or ACK Timeouts, rather than waiting for a poll.
* **Congestion Control:** The system dynamically adjusts the data transmission interval (200ms - 5000ms) based on network latency (RTT).
* **Precise Automation:**
    * **Homing Sequence:** Automatic calibration using a limit switch.
    * **Item Counting:** IR sensor integration for counting sorted items against a target.
    * **PID/Speed Control:** Open-loop PWM control with encoder feedback for speed monitoring.

## 🛠 System Architecture

### Hardware Wiring (ESP32)
| Component | ESP32 Pin | Function |
| :--- | :--- | :--- |
| **Encoder A** | GPIO 34 | Speed/Position Feedback |
| **Encoder B** | GPIO 35 | Speed/Position Feedback |
| **IR Sensor** | GPIO 32 | Item Detection |
| **Limit Switch** | GPIO 33 | Homing Calibration |
| **Motor ENA** | GPIO 25 | PWM Speed Control |
| **Motor IN1** | GPIO 22 | Direction Control |
| **Motor IN2** | GPIO 23 | Direction Control |

### Network Topology
```mermaid
graph LR
    A[ESP32 Server] -- TCP/JSON --> B[Jetson/Pi Gateway]
    B -- TCP/JSON --> C[MATLAB Client]
    C -- Commands (Start/Stop/PWM) --> B
    B -- Commands --> A
