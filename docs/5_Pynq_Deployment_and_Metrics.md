# 5. PYNQ Deployment & Performance Metrics

This document details the physical deployment framework on the PYNQ-Z2 hardware platform. It covers Python embedded driver management, contiguous memory buffer allocation, runtime AXI transaction orchestration, and the final real-world performance benchmarks.

---

## 1. Embedded Software Driver Infrastructure

The host environment runs on the embedded Linux kernel of the ARM Cortex-A9 processor utilizing the PYNQ framework. The Python notebook coordinating this layer is available in the repository at [`/notebooks/pynq_jupyter_notebook.ipynb`](../notebooks/pynq_jupyter_notebook.ipynb). The Python runtime acts as the high-level system executive manager that prepares execution vectors and interfaces with physical hardware layouts via memory-mapped input/output bindings.

At boot runtime, the system instantiates the Overlay API to download the compiled hardware bitstream configuration ([`cnn_2layer_quant.bit`](../hardware_handoff/)) onto the programmable logic fabric. This interaction dynamically reprograms the FPGA fabric and activates the hardware-mapped register structures matching the Vivado block design layout.

---

## 2. Contiguous Memory Management (`pynq.allocate`)

Standard Linux virtual memory allocation fragments memory spaces into non-contiguous physical chunks. Since the high-speed AXI DMA engine relies entirely on a strict hardware structure to maintain single-cycle bursting efficiency, data inputs and output arrays must be allocated within strict, physically sequential memory locations.

* **Buffer Reservation:** The system utilizes the PYNQ allocation API to reserve uninterrupted physical memory blocks directly within the shared onboard DDR memory pool.
* **Typing Adjustments (The 32-bit Wrapper):** To prevent AXI bus misalignment, the 784-byte image array is cast into 32-bit unsigned integers (`np.uint32`). This ensures each 8-bit pixel is padded to perfectly fit the 32-bit width of the DMA channel without triggering byte-alignment errors. The outbound classification buffer is allocated as a 10-element `np.uint32` array to receive the raw hardware bits, which are later actively parsed into signed 32-bit integers during software post-processing.

*(Note: The troubleshooting logs regarding the unsigned 0% accuracy reading bug and the 32-bit alignment bus crashes are documented comprehensively in [7. Debugging Issues](7_Debugging_Issues.md) under Bug 3 and Bug 4).*

---

## 3. Data Streaming and Interface Orchestration

Once the contiguous buffers are initialized, the Python driver orchestrates the data movement cycles to execute inference:

1. **Hardware Activation:** The execution manager interacts with the accelerator core's control register block via the AXI4-Lite port. It writes the control byte to the base address register. This triggers the hardware flags for Start and Auto-Restart, placing the accelerator pipeline into an active listening state.
2. **DMA Transmission:** The system triggers the DMA read and write channels simultaneously by specifying the physical starting pointer constraints of the contiguous buffers alongside the byte-length targets.
3. **Asynchronous Waiting (Interrupt Event):** Rather than continuously polling the CPU—which would waste processing cycles—the code execution routine yields control to the Linux kernel. It utilizes the hardware-wired PL-to-PS fabric interrupt line to handle completion. The execution thread remains suspended until the DMA's interrupt pin triggers, notifying the system that the results have been fully written back to the DDR memory pool.

---

## 4. Hardware Verification and Post-Processing

When the interrupt releases the execution thread, the raw scores are finalized within the destination buffer.

* **Two's Complement Parsing:** The hardware outputs raw 32-bit accumulations that can be deeply negative. The 10 raw unsigned values returned by the DMA are explicitly cast to signed integers utilizing Python memory-view methods (`.view(np.int32)`). This critical step ensures that 2's complement negative numbers are parsed correctly instead of causing massive unsigned positive-integer wraparound.
* **Prediction Extraction:** The software applies a fast maximum argmax search routine over the verified signed score array to return the final predicted digit (0 to 9).

---

## 5. Physical Hardware Benchmarks

The entire computing system was validated by processing an inference loop across test vector batches from the MNIST evaluation set. 

### A. Performance Latency Matrix
The comparative execution profiles across the different processing environments are structured below:

| Platform / Environment | Execution Model | Average Inference Latency | Target Speedup Factor |
| :--- | :--- | :---: | :---: |
| **ARM Cortex-A9 CPU** | Manual Python Loop | ~1,496.53 ms | 1x (Base Reference) |
| **ARM Cortex-A9 CPU** | Optimized C++ TFLite Engine | ~1.10 ms | 1,360x Faster |
| **PYNQ-Z2 FPGA Fabric** | Custom Streaming HLS IP | **~1.11 ms** | **1,351x Faster** |

### B. System Bottleneck Observation
The near-identical latency between the FPGA and the optimized TFLite library demonstrates that for small images (28x28), the system is I/O-bound by the Python execution stack and DMA driver handshake overhead, masking the true microsecond-level calculation speed of the isolated FPGA fabric blocks.

### C. Accuracy and Operating Environment
* **Accuracy Preservation:** The physical deployment achieved a final prediction accuracy of 97.00%, validating the INT8 fixed-scale calibration against the software baseline.
* **Estimated On-Chip Power (Vivado Post-Implementation):** 1.900 Watts
* **Estimated Junction Temperature:** 46.9°C (Confirms the board maintains safe operational stability under normal laboratory conditions without active cooling).
---

## 6. Real-Time Interactive Verification

To showcase the real-world utility of the accelerator, the deployment notebook features an interactive canvas interface. Users can draw digits directly within the environment. Because the physical latency is kept at a minimal ~1.11 ms, the hardware-linked pipeline extracts features, routes transactions through the DMA lanes, and updates the interface with predicted digits in real-time without introducing drawing stutter or display delays.
