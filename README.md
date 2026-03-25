# ParkIQ
> **Stop wasting time searching for parking. SmartPark finds it for you — instantly.**

![ESP32](https://img.shields.io/badge/Hardware-ESP32-blue?style=for-the-badge)
![LoRa](https://img.shields.io/badge/Wireless-LoRa%20433MHz-green?style=for-the-badge)
![MQTT](https://img.shields.io/badge/Protocol-MQTT-orange?style=for-the-badge)
![Status](https://img.shields.io/badge/Status-Live-brightgreen?style=for-the-badge)

---

## 💡 The Problem

Every day, drivers waste **30+ minutes** circling parking lots — burning fuel, wasting time, and causing traffic congestion. There had to be a smarter way.

## ✅ The Solution — SmartPark

**SmartPark** is a real-time IoT smart parking system that lets users **see**, **book**, and **access** parking slots from their phone — before they even arrive.

No more guessing. No more waiting. Just park.

---

## 🚀 What Makes SmartPark Special?

| Feature | What it means for you |
|---|---|
| 📡 **Long-range LoRa 433MHz** | Works across large parking areas without WiFi |
| ⚡ **Real-time slot updates** | Know if a slot is free in under 1 second |
| 📲 **Remote booking** | Reserve your spot from anywhere |
| 🔁 **Automatic gate control** | Servo-powered barrier opens only for you |
| 📊 **Live IoT Dashboard** | Beautiful real-time view on Adafruit IO |
| 🔋 **Low power design** | Runs for hours on minimal power |

---

## 🛠️ Built With
```
ESP32 × 2  •  LoRa Ra-02 433MHz  •  IR Sensors  •  Servo Motor  •  Adafruit IO  •  MQTT
```

---

## ⚙️ How It Works
```
🔍 IR Sensor detects car
        ↓
📡 ESP32 TX sends data via LoRa
        ↓
📶 ESP32 RX receives & pushes to cloud
        ↓
📊 Adafruit IO dashboard updates live
        ↓
👆 User books slot from dashboard
        ↓
🚧 Servo gate opens automatically
```

Simple. Fast. Smart.

---

## 📊 Live Dashboard (Adafruit IO)

The dashboard gives you **3 powerful feeds:**

- 🟢 `parking-status` — Which slots are free right now
- 🔖 `booking-status` — Manage your reservation
- ⚙️ `servo-angle` — Gate control (automatic)

---

## 🧰 Quick Start
```bash
git clone https://github.com/mukesh2311/SmartPark-IoT.git
```

1. Open `TX_Node.ino` → Upload to **ESP32 #1**
2. Open `RX_Node.ino` → Add your WiFi & Adafruit credentials → Upload to **ESP32 #2**
3. Open your **Adafruit IO dashboard**
4. Watch it work live ✅

---

## 📁 Project Structure
```
SmartPark-IoT/
├── TX_Node/          # Sensor + LoRa transmitter
├── RX_Node/          # LoRa receiver + Cloud + Gate
├── Dashboard/        # Adafruit IO setup guide
└── Docs/             # Circuit diagrams
```

---

## 👨‍💻 Built By

**Mukesh** — Embedded Systems & IoT Developer
🔗 [github.com/mukesh2311](https://github.com/mukesh2311)

---

## 📄 License

MIT License — Open source & free to use.

---

> ⭐ **If SmartPark saved you time, drop a star!** It means a lot.
