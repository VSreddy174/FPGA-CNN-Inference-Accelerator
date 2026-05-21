# FPGA CNN Inference Accelerator

This project implements a hardware-optimized 2-layer sequential Convolutional Neural Network (Conv2D, MaxPool, Dense) inference accelerator targeting MNIST digit classification. Designed in C++ using High-Level Synthesis (Vitis HLS) and integrated into a Zynq-7000 SoC (PYNQ-Z2), the architecture utilizes a 32-bit AXI4-Stream datapath alongside INT8 Post-Training Quantization (PTQ) to offload heavy matrix computations from the ARM processing system to programmable logic.

---

## Project Overview

The system divides labor cleanly between hardware execution and software management:
* **Hardware Fabric (FPGA):** Executes the full mathematical layers (Convolution, ReLU Activation, Max Pooling, and Dense Fully-Connected Layer) utilizing hardcoded quantized integer weights (stored in 8-bit signed format) and biases (stored in 32-bit signed format).
* **Software Environment (Python/Jupyter):** Handles runtime initialization, loads the bitstream, pre-processes/quantizes raw images, coordinates streaming DMA transactions, and reads back the final prediction array to apply the final `np.argmax` function.

---

## Technical Specifications

The core architecture maps a 5,258-parameter network onto the programmable logic fabric, synthesized and implemented via Vivado.

### Timing & Power Summary
* **Operating Frequency:** 100 MHz (Clocked via FCLK_CLK0)
* **Worst Negative Slack (WNS):** -0.426 ns (Post-Implementation routing congestion at 100% DSP capacity; meets timing cleanly at +1.293 ns in Vivado Synthesis)
* **Worst Hold Slack (WHS):** +0.022 ns (Timing Met)
* **Total On-Chip Power:** 1.900 W 
* **Junction Temperature:** 46.9°C

### Post-Implementation Resource Utilization
The architecture maximizes the execution capacity of the Zynq chip by utilizing every single available onboard multiplication engine.

| Resource | Used | Available | Utilization % |
| :--- | :---: | :---: | :---: |
| **Slice LUTs** | 21,943 | 53,200 | 41.25% |
| **Slice Registers** | 13,656 | 106,400 | 12.83% |
| **Block RAM Tile** | 7.0 | 140 | 5.00% |
| **DSPs** | 220 | 220 | 100.00% |

---

## Performance Results

Performance and functionality were validated directly on physical hardware by comparing the streaming HLS IP block against the embedded ARM Cortex-A9 software baseline running an image processing loop.

| Benchmark Metric | Platform / Implementation | Target Execution / Latency | Performance Impact |
| :--- | :--- | :---: | :---: |
| **Software Baseline** | Manual Python (ARM Cortex-A9) | ~1,496.53 ms | Reference |
| **Optimized Software** | TFLite Library (C++ Engine) | ~1.10 ms | Core I/O Bound |
| **Hardware Accelerator** | Custom Streaming HLS IP (FPGA) | ~1.11 ms | **1,351x Speedup** |
| **Inference Accuracy** | Physical PYNQ Deployment | **97.00%** | Precision Preserved |

---

## Repository Structure

The physical assets of this project are organized into dedicated deployment directories:
* [`/software_training/`](software_training/) : Google Colab Jupyter scripts covering float-to-integer training, PTQ calibration, and automated C++ weight header exporting.
* [`/hls_core/`](hls_core/) : Synthesizable C++ implementation source files, directive files, and testbenches utilized during Vitis HLS verification.
* [`/vivado_soc/`](vivado_soc/) : Vivado block design connectivity configuration records and system wrappers.
* [`/hardware_handoff/`](hardware_handoff/) : Compiled deployment files including the bitstream (`.bit`) and hardware handoff description (`.hwh`).
* [`/notebooks/`](notebooks/) : Interactive PYNQ Jupyter notebooks coordinating hardware setup, data streaming, and runtime verification.
* [`/docs/`](docs/) : Technical notes, design specifications, and architectural block design diagrams.

---

## Documentation Sections

The project documentation is divided into the following technical modules, outlining the engineering cycle from mathematical model down to physical verification:

* **[1. Theory and Quantization](docs/1_Theory_and_Quantization.md):** Mathematical definitions of the network layers, Google Colab reference model generation, and calibrated INT8 Post-Training Quantization rules.
* **[2. HLS Core Architecture](docs/2_HLS_Architecture.md):** High-Level Synthesis interface binding layout, C++ hardware loop pipelining ($II=1$), and multi-channel array partitioning.
* **[3. Simulation and Verification](docs/3_Simulation_and_Verification.md):** C-Simulation single-vector functional testbench checks, Vitis pre-synthesis resource estimations, and AXI streaming interface compliance validations.
* **[4. Vivado SoC Integration](docs/4_Vivado_SoC_Integration.md):** Structural board block design configurations, AXI-DMA Direct Register Mode settings, PL-PS interrupt wiring (`IRQ_F2P`), and HP0 port routing maps.
* **[5. Pynq Deployment and Metrics](docs/5_Pynq_Deployment_and_Metrics.md):** Physical memory configuration via Python runtime drivers, contiguous buffer management, I/O bottleneck evaluations, and interactive canvas canvas testing.
* **[6. Post-Implementation Analysis](docs/6_Post_Implementation_Analysis.md):** Physical layout routing constraints analysis, Stage-by-Stage setup timing slack tracking, power/thermal metrics, and 186-count DSP Design Rule Check (DRC) evaluations.
* **[7. Debugging Issues](docs/7_Debugging_Issues.md):** Complete architectural trouble log capturing the 7 critical software, data-formatting, and hardware-level compilation bugs solved over the project lifecycle.


*Note:* The core sequential documentation network ([Modules 1 through 7](docs/)) charts the end-to-end production deployment flow targeted directly onto the physical PYNQ-Z2 hardware layout, whereas isolated architectural optimizations and hardware portability experiments are tracked separately inside the [`/other_expts/`](other_expts/) research directory.
