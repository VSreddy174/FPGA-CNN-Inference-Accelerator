# 7. Debugging Issues & Engineering Learnings

This document serves as the historical development log for the CNN accelerator project. It catalogs the critical bugs encountered during development, the troubleshooting steps taken, the architectural fixes applied, and the core engineering insights gained from deploying deep learning models onto physical silicon.

---

## Debug Summary Matrix

Before diving into the technical logs of each specific error, the table below provides a quick-reference summary of the main debugging focus areas across the software, data formatting, and hardware compilation domains:

| Bug Focus | Observed Symptom | Underlying Root Cause | Implemented Architectural Fix |
| :--- | :--- | :--- | :--- |
| **1. PTQ Saturation** | 10.5% Accuracy (Random Guessing) | Exponential $127^3$ scaling factor growth clipped activations. | Switched to empirical calibration on 1,000 images to derive fixed multipliers. |
| **2. Input Typing** | Distortion/Errors on thick-lined digits | `int8` type mapping caused white pixel values (255) to wrap to -1. | Retyped input arrays to `ap_uint<8>` and applied late casting. |
| **3. Output Read** | 0.00% Physical Accuracy on PYNQ board | PYNQ read 32-bit signed negative logit bits as large unsigned integers. | Applied a `.view(np.int32)` memory-view cast during Python post-processing. |
| **4. Bus Alignment** | DMA Stream Hangs / PYNQ Linux Kernel Crashes | Dense 8-bit streaming mismatched the DMA's hardwired 32-bit channel width. | Implemented "shipping container" rule: cast input array to `np.uint32`. |
| **5. Fixed-Point Word** | Drop to 73% / 70% Accuracy during testing | Word boundaries caused Integer Overflow (<16,6>) or Underflow (<16,9>). | Upgraded intermediate accumulators and biases to 32-bit signed integers. |
| **6. Core Synthesis** | Synthesis engine stalled; estimated 2,020 DSPs | Aggressive outer-loop pipelining forced full structural unrolling. | Moved `#pragma HLS PIPELINE` to innermost loop to enforce temporal reuse. |
| **7. Vivado Integration** | `TDATA_NUM_BYTES` width errors & missing TLAST | Compiler added padding to custom 41-bit struct, hiding control pins. | Abandoned custom struct for official `hls::axis<ap_uint<32>, 0, 0, 0>` template. |

---

## Bug 1: The "10.5% Random Guessing" Saturation Failure

### Symptom & Observation
During initial testing cycles, the model's accuracy collapsed to roughly 10.5% across all MNIST evaluation sets. The system was effectively guessing at random, treating completely distinct handwritten digits as the exact same output class.

### Root Cause Analysis
The issue stemmed from an aggressive inter-layer quantization approach. In the early stages of the design, a uniform Post-Training Quantization (PTQ) scaling factor of `127` was applied and allowed to accumulate exponentially across successive layers ($127 \times 127 \times 127$). 

This exponential growth generated massive 32-bit intermediate outputs from the first convolutional layer (`conv_1`). When these large numbers were fed into the subsequent processing blocks, they completely saturated the downstream arithmetic structures. The math overwhelmed the network's dynamic range, clipping the gradients and destroying feature representation.

### Resolution & Core Learning
The uniform scaling strategy was abandoned in favor of an **activation calibration phase**. 

The model was run on a representative subset of 1,000 training images to profile the global maximum activations at every layer boundary. From these empirical peak values, precise static re-quantization constants were derived (`L1_SCALE = 0.001327` and `L2_SCALE = 0.002090`). 

By inserting specialized rescaling multipliers at the end of each layer, the hardware scales the intermediate 32-bit accumulated sums back down safely into the standard 8-bit integer range before passing them to the next block. This calibration restored the deployment accuracy to its stable **97.00%** benchmark.

---

## Bug 2: The "White Pixel Wraparound" Input Distortion

### Symptom & Observation
When streaming raw images through the AXI4-Stream interface, the accelerator output wildly incorrect classification scores for digits with thick lines, while correctly identifying thin or faint handwritten numbers.

### Root Cause Analysis
The error was traced to a type-mismatch bottleneck between the host software environment and the synthesized High-Level Synthesis (HLS) core. The input array logic was initially typed as strict 8-bit signed integers (`int8` or `ap_int<8>`). 

The MNIST dataset contains background pixels valued at 0 (black) and peak foreground pixels valued up to 255 (pure white). In two's complement binary representation, an unsigned byte value of `255` translates to `-1` when interpreted as a signed integer. Consequently, the brightest parts of the image inverted into negative numbers inside the convolution multipliers, distorting the structural features of the digits.

### Resolution & Core Learning
The hardware input ports were updated to use strict 8-bit unsigned integer types (`ap_uint<8>`). This prevents any sign-bit wraparound or data corruption. 

The pixels are kept unsigned throughout the data ingestion stage and are only cast to 32-bit signed values at the exact moment of multiplication inside the parallel computing arrays. This preserves the full `0 to 255` dynamic range of the image data.

---

## Bug 3: The 0% Accuracy Output Interpretation Error

### Symptom & Observation
During early physical board testing via the PYNQ Jupyter notebook, the hardware successfully executed transactions and fired interrupts, but returned an accuracy of exactly 0.00%. Every classification output mapped to a single incorrect digit class index.

### Root Cause Analysis
The issue was caused by an interpretation mismatch between the Python environment and the FPGA hardware. The PYNQ framework's memory buffer allocator (`pynq.allocate`) reserves raw bit containers within the shared DDR memory pool using unsigned formats (`np.uint32`) by default. 

However, the hardware accelerator exports its final classification scores wrapped in standard 32-bit signed two's complement format (`int32`), meaning poor prediction classes frequently carry negative accumulation values. Because Python read these bits as raw unsigned integers, negative numbers wrapped around into massive positive values (e.g., `-2,000` was interpreted as `4,294,930,569`). This caused the software's `np.argmax()` function to latch onto the wrong output class.

### Resolution & Core Learning
To bridge this gap, a dedicated post-processing step was introduced into the Jupyter driver script. Before performing the argmax extraction, the raw unsigned data array returned by the DMA engine is converted into a signed format using memory-view methods:

`signed_scores = outbound_buffer.view(np.int32)`

Reinterpreting the raw bit allocations as signed 32-bit integers allows the software to correctly parse the negative classification scores. This adjustment completely resolved the interpretation error, matching the 97.00% functional accuracy seen in the hardware simulation.

---

## Bug 4: The AXI Bus Alignment Crash (The "Shipping Container" Solution)

### Symptom & Observation
Attempts to stream a raw 784-byte pixel array directly through the DMA read channel caused the system to hang indefinitely or caused the PYNQ Linux kernel to crash with a bus alignment fault.

### Root Cause Analysis
The AXI Direct Memory Access (DMA) core configured in the Vivado Block Design is hardwired to a strict **32-bit stream data width**. When the Python host attempted to stream a dense pack of 8-bit integers (`np.uint8`) down a 32-bit wide hardware highway, the interface mismatched. The DMA expected each data transfer to align with 4-byte boundaries, leading to metadata corruption, byte alignment errors, and TDATA bus failures.

### Resolution & Core Learning
To establish bus stability without introducing complex bit-shifting logic in hardware, a **"shipping container" alignment rule** was implemented. The software driver casts the flattened 784-element image array into 32-bit unsigned integers (`np.uint32`) before initiating the transfer. 

This padding ensures that each individual 8-bit pixel travels inside its own 32-bit data envelope, perfectly matching the width of the AXI DMA channel. The hardware core (`cnn_accel`) reads the full 32-bit packet, extracts the lower 8 bits, and discards the remaining 24 bits of padding. This approach trades minor AXI bandwidth efficiency for absolute datapath stability, eliminating bus alignment crashes entirely.

---

## Bug 5: The <16, 6> vs <16, 9> Fixed-Point Dilemma (Overflow vs. Underflow)

### Symptom & Observation
During the initial transition from floating-point matrices to a 16-bit fixed-point format (`ap_fixed`), the physical hardware inference accuracy dropped to 73%. Attempting a quick bit-width adjustment degraded it further down to 70%, rendering the network functionally useless.

### Root Cause Analysis
This behavior highlighted the classic hardware engineering trade-off between Dynamic Range and Fractional Precision within constrained word lengths:
* **The 73% Accuracy Drop (`ap_fixed<16, 6>`):** This layout allocated 6 bits to the integer portion (range $\pm32$) and 10 bits to the fractional portion. Because the final Dense layer aggregates 400 flattened elements, intermediate unscaled dot-products easily exceeded the absolute boundary of 32. This caused catastrophic Integer Overflow, wrapping confident positive prediction sums into arbitrary negative garbage values.
* **The 70% Accuracy Drop (`ap_fixed<16, 9>`):** To eliminate the overflow, the word layout was modified to provide 9 integer bits (range $\pm256$). However, this left only 7 bits for the fractional portion ($2^{-7}$ resolution step size). This resulted in Quantization Underflow, where small but highly critical network weights fell below the resolution limit and were truncated to exactly zero, erasing fine-grained features from the network's parameters.

### Resolution & Core Learning
The evaluation proved that a 16-bit total word boundary was mathematically insufficient to safely bridge the dynamic range requirements of a 400-input dense accumulation layer without data degradation. The system architecture was upgraded to utilize 32-bit signed integer accumulators (`int32`), providing extensive numerical safety boundaries while maintaining the area benefits of 8-bit quantized weights.

---

## Bug 6: The 2,020 DSP Resource Explosion

### Symptom & Observation
During Vitis HLS C-Synthesis, the compilation process stalled indefinitely, consuming excessive host memory. When it finally completed, the synthesis report demanded 2,020 DSP slices and over 350,000 LUTs—exceeding the PYNQ-Z2 board's physical chip capacity by nearly 1,000%.

### Root Cause Analysis
The issue was triggered by an aggressive loop pipelining configuration applied to the outer dimension of the Fully Connected (Dense) layer. By placing the `#pragma HLS PIPELINE` directive incorrectly on the outer class loop (iterating from 0 to 9) while leaving the inner loop unguided, the synthesis engine attempted to fully unroll the 400-iteration inner loop. The compiler tried to construct 400 independent physical 32-bit multiplier circuits to process the entire Dense layer simultaneously in a single clock cycle, crashing the resource budget.

### Resolution & Core Learning
To balance the resource footprint, the optimization structure was inverted. The `#pragma HLS PIPELINE II=1` directive was moved specifically to the innermost calculation loop of the Dense layer, while the outer class dimension loop was allowed to execute sequentially. 

This configuration shifted the design from spatial parallelism to temporal reuse: a smaller, highly optimized set of DSP slices is reused 400 times sequentially per class rather than creating 400 multipliers physically on the silicon. This spatial restructuring successfully brought the total DSP utilization down to an efficient, timing-clean **100% (220/220 blocks)**.

---

## Bug 7: The 48-bit AXI Mismatch & Vivado "Out of Date" Loop

### Symptom & Observation
Within the Vivado Block Design environment, connecting the customized HLS IP core to the Xilinx AXI DMA core threw critical connectivity warnings. The tool flagged a severe `TDATA_NUM_BYTES` mismatch error (stating the IP core was exporting 6 or 8 bytes while the DMA demanded a strict 4-byte width) alongside a "Missing TLAST port" error, blocking bitstream generation.

### Root Cause Analysis
The C++ core source code initially utilized a custom data structure (`axis_t`) to bundle the 32-bit data payload with the required AXI side-channel signal flags (`keep`, `strb`, and `last`), creating a cumulative packet size of 41 bits. 

The HLS compiler treated this user-defined struct as a raw data blob. To align with standard on-chip internal memory boundaries, it automatically padded the struct up to the nearest power-of-two byte boundary—translating it physically to 48 bits (6 bytes) or 64 bits (8 bytes). Because of this hidden padding, Vivado's automated validation tools could not isolate the internal `.last` field or map it to the physical `TLAST` hardware pin required to negotiate streaming bounds with the DMA.

### Resolution & Core Learning
The custom user struct was completely removed from the HLS source. It was replaced with the official Xilinx native streaming template:

`typedef hls::axis<ap_uint<32>, 0, 0, 0> axis_t;`

This hardware-aware utility ensures a strict 32-bit data payload container with zero hidden padding bytes. Furthermore, it automatically maps the internal control flags directly to their standardized physical AXI4-Stream hardware pins (including `TLAST` and `TKEEP`). This allowed Vivado to cleanly validate interface widths and resolve the protocol errors instantly.
