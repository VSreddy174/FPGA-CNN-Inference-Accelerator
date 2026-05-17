# FPGA CNN Inference Accelerator

This project implements a hardware-optimized 2-layer Convolutional Neural Network (Conv2D, MaxPool, Dense) inference accelerator targeting MNIST digit classification. Designed in C++ using High-Level Synthesis (Vitis HLS) and integrated into a Zynq-7000 SoC (PYNQ-Z2), the architecture utilizes a 32-bit AXI4-Stream datapath alongside INT8 Post-Training Quantization (PTQ) to offload heavy matrix computations from the ARM processing system to programmable logic.

## Project Overview

The system divides labor cleanly between hardware execution and software management:
* **Hardware Fabric (FPGA):** Executes the full mathematical layers (2D Convolution, ReLU Activation, Max Pooling, and Dense Fully-Connected Layer) utilizing hardcoded quantized integer weights.
* **Software Environment (Python/Jupyter):** Handles runtime initialization, loads the bitstream, pre-processes/quantizes raw images, coordinates streaming DMA transactions, and reads back the final prediction array to apply the final `argmax` function.

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
| :--- | :--- | :--- | :--- |
| **Slice LUTs** | 21,943 | 53,200 | 41.25% |
| **Slice Registers** | 13,656 | 106,400 | 12.83% |
| **Block RAM Tile** | 7.0 | 140 | 5.00% |
| **DSPs** | 220 | 220 | 100.00% |

## Performance Results

Performance and functionality were validated directly on physical hardware by comparing the streaming HLS IP block against the embedded ARM Cortex-A9 software baseline running an image processing loop.

* **Software Baseline (ARM Cortex-A9):** ~1,351.95 ms
* **Hardware Accelerator (Custom HLS IP):** ~1.10 ms 
* **Measured Hardware Speedup:** **1,351x Faster**
* **Inference Accuracy:** **97.00%**


