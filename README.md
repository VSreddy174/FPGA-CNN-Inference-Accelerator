# FPGA CNN Inference Accelerator

This project implements a hardware-optimized 2-layer sequential Convolutional Neural Network (Conv2D, MaxPool, Dense) inference accelerator targeting MNIST digit classification. Designed in C++ using High-Level Synthesis (Vitis HLS) and integrated into a Zynq-7000 SoC (PYNQ-Z2), the architecture utilizes a 32-bit AXI4-Stream datapath alongside INT8 Post-Training Quantization (PTQ) to offload heavy matrix computations from the ARM processing system to programmable logic.

## Project Overview

The system divides labor cleanly between hardware execution and software management:
* **Hardware Fabric (FPGA):** Executes the full mathematical layers (Convolution, ReLU Activation, Max Pooling, and Dense Fully-Connected Layer) utilizing hardcoded quantized integer weights (stored in 8-bit signed format) and biases (stored in 32-bit signed format).
* **Software Environment (Python/Jupyter):** Handles runtime initialization, loads the bitstream, pre-processes/quantizes raw images, coordinates streaming DMA transactions, and reads back the final prediction array to apply the final `np.argmax` function.

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

* **Software Baseline (ARM Cortex-A9):** ~1,496.53 ms
* **Hardware Accelerator (Custom HLS IP):** ~1.11 ms 
* **Measured Hardware Speedup:** **1,351x Faster**
* **Inference Accuracy:** **97.00%**

## Repository Structure

The project assets are organized into the following deployment directories:
* `/software_training/` : Google Colab Jupyter scripts covering float-to-integer training, PTQ calibration, and automated C++ weight header exporting.
* `/hls_core/` : Synthesizable C++ implementation source files, directive files, and testbenches utilized during Vitis HLS verification.
* `/vivado_soc/` : Vivado block design connectivity configuration records and system wrappers.
* `/hardware_handoff/` : Compiled deployment files including the bitstream (`.bit`) and hardware handoff description (`.hwh`).
* `/notebooks/` : Interactive PYNQ Jupyter notebooks coordinating hardware setup, data streaming, and runtime verification.
* `/docs/` : Technical notes and architectural block design block diagrams.

## Documentation Sections

The project documentation is divided into the following technical modules:
* **[1. Theory and Quantization](docs/1_Theory_and_Quantization.md):** Mathematical definitions of the layers, Google Colab reference model generation, and INT8 Post-Training Quantization rules.
* **[2. HLS Core Architecture](docs/2_HLS_Architecture.md):** High-Level Synthesis layout configurations, streaming optimizations, loop pipelining ($II=1$), and array partitioning.
* **[3. Simulation and Verification](docs/3_Simulation_and_Verification.md):** C-Simulation functional checks, Vitis resource estimation, and initial DSP block targeting reports.
* **[4. Vivado SoC Integration](docs/4_Vivado_SoC_Integration.md):** Structural board block design configurations, AXI-DMA Direct Register Mode settings, and HP0 port routing.
* **[5. Pynq Deployment and Metrics](docs/5_Pynq_Deployment_and_Metrics.md):** Memory configuration through Python drivers, contiguous buffer management, and validation results.
* **[6. Post-Implementation Analysis](docs/6_Post_Implementation_Analysis.md):** Physical layout routing constraints analysis, Worst Negative Slack evaluations, and system debug tracking logs.
* **[7. Debugging Issues](docs/7_Debugging_Issues.md):** Some Debugging issues anad learnings.
 
