# embedded-ai-roadmap
# 🚀 Embedded AI & Concurrency Learning Journey  

This repository is my structured roadmap to transition into **Embedded Linux, Concurrency, CUDA, and AI on Jetson Nano**.  
It’s organized into milestones, each with tasks and projects to complete.  

---

## 🎯 Roadmap  

### Milestone 1: Concurrency Foundations (Weeks 1–4)  
- [ ] Learn `std::thread`, `join`, `detach`.  
- [ ] Implement Producer–Consumer with `std::mutex` + `std::condition_variable`.  
- [ ] Implement Thread Pool.  
- [ ] Implement Reader–Writer example.  
- [ ] Practice with Futures & Promises.  
- [ ] Profile with Valgrind (helgrind, DRD) and measure timing with `std::chrono`.  

### Milestone 2: Debugging & Profiling (Week 5)  
- [ ] Install & run Valgrind on multithreaded programs.  
- [ ] Learn `perf` basics (CPU hotspots).  
- [ ] Learn NVIDIA Nsight Systems.  

### Milestone 3: CUDA Basics (Weeks 6–8)  
- [ ] Write CUDA vector addition kernel.  
- [ ] Write CUDA matrix multiplication kernel.  
- [ ] Explore CUDA memory types (global, shared, pinned).  
- [ ] Use Nsight Compute for GPU profiling.  

### Milestone 4: Jetson Nano Setup & CUDA Projects (Weeks 9–12)  
- [ ] Flash JetPack to Jetson Nano.  
- [ ] Run CUDA sample (`deviceQuery`).  
- [ ] Implement vector addition & matrix multiplication on Nano.  
- [ ] Implement image blur/convolution filter on Nano.  

### Milestone 5: Linux Device Drivers (Weeks 13–16)  
- [ ] Write “Hello World” kernel module.  
- [ ] Implement character driver (read/write).  
- [ ] Handle interrupts with button/LED.  
- [ ] Debug with `dmesg` and `strace`.  

### Milestone 6: Jetson Driver Project – MIPI Camera (Weeks 17–20)  
- [ ] Study Jetson camera driver architecture.  
- [ ] Load a V4L2-based camera driver.  
- [ ] Capture frames using the driver.  
- [ ] (Stretch) Integrate CUDA for preprocessing frames.  

### Milestone 7: Python + OpenCV (Weeks 21–22)  
- [ ] Install OpenCV with CUDA support.  
- [ ] Capture camera feed in Python.  
- [ ] Apply filters, edge detection, face detection.  
- [ ] Benchmark CPU vs GPU processing.  

### Milestone 8: TensorRT + ONNX (Weeks 23–26)  
- [ ] Convert PyTorch/TensorFlow model → ONNX.  
- [ ] Run inference with ONNX Runtime.  
- [ ] Optimize inference with TensorRT.  
- [ ] Benchmark performance improvements.  

---

## 📂 Structure  

