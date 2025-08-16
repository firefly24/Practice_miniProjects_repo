# 🚀 Embedded AI & Robotics Roadmap

This repository tracks my **learning path** from Embedded Linux & C++ → CUDA → AI on Jetson → Robotics/UAV.  
The goal is to transition into **Embedded AI / Robotics Engineer** roles.  

---

## 🎯 Roadmap Milestones

### 📍 Milestone 1: Core Embedded Linux & Multithreading
- [ ] Learn **C++ multithreading basics** (`std::thread`, mutex, condition variables)  
- [ ] Implement **producer-consumer model**  
- [ ] Write a simple **thread pool**  
- [ ] Implement **Linux character driver**  
- [ ] Add **sysfs interface** to driver  
- [ ] Write an **I2C/SPI driver (simulated if no hardware)**  
- [ ] Handle **interrupts (IRQ handling)**  
**Progress:** ![Progress](https://img.shields.io/badge/Progress-0%25-red)

---

### 📍 Milestone 2: GPU & CUDA Programming
- [ ] Understand **CUDA execution model** (threads, blocks, grids)  
- [ ] Write a **vector addition kernel**  
- [ ] Implement **matrix multiplication in CUDA**  
- [ ] Apply **image filters** using CUDA  
- [ ] Learn **CUDA memory management** (global, shared, pinned)  
- [ ] Profile & optimize CUDA programs  
**Progress:** ![Progress](https://img.shields.io/badge/Progress-0%25-red)

---

### 📍 Milestone 3: AI on Embedded
- [ ] Learn **Python basics** (NumPy, file I/O, classes)  
- [ ] Implement basic **OpenCV image operations**  
- [ ] Run an **ONNX model** on Jetson Nano  
- [ ] Convert & optimize a model with **TensorRT**  
- [ ] Benchmark **CPU vs GPU inference performance**  
**Progress:** ![Progress](https://img.shields.io/badge/Progress-0%25-red)

---

### 📍 Milestone 4: Robotics / UAV
- [ ] Install & configure **ROS2** on Jetson  
- [ ] Implement **publisher/subscriber** nodes  
- [ ] Control **turtlesim / basic robot sim** in ROS2  
- [ ] Run **PX4 SITL** simulation with Gazebo  
- [ ] Implement **trajectory following** with MAVSDK  
- [ ] Add **computer vision object tracking** to UAV demo  
**Progress:** ![Progress](https://img.shields.io/badge/Progress-0%25-red)

---

## 📦 Repo Structure (planned)

├── milestone-1-linux-mt/
│ ├── threading-experiments/
│ ├── drivers/
│ └── docs/
├── milestone-2-cuda/
│ ├── vector-add/
│ ├── matrix-mul/
│ ├── filters/
│ └── profiling/
├── milestone-3-ai-embedded/
│ ├── opencv-basics/
│ ├── onnx-runtime/
│ ├── tensorrt/
│ └── benchmarks/
├── milestone-4-robotics/
│ ├── ros2-demos/
│ ├── px4-sitl/
│ ├── trajectory-control/
│ └── vision-tracking/
└── README.md



---

## 📅 Plan

1. Work on issues milestone by milestone.  
2. Update **checklists** in this README as I complete tasks.  
3. Document learnings inside `docs/` folders.  

---

## 🌟 End Goal

By completing this roadmap, I’ll have:  
- Hands-on Linux driver & multithreading experience  
- CUDA programming & GPU optimization skills  
- AI deployment knowledge on Jetson Nano  
- ROS2 + PX4 projects demonstrating UAV control  

👉 Ready to apply for roles in **Embedded Linux, Edge AI, or Robotics/UAV Engineering**.  

---

💡 *This roadmap is iterative — tasks may evolve as I learn more.*
