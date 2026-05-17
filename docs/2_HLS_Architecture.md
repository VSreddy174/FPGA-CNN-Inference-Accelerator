# 2. High-Level Synthesis (HLS) Core Architecture

This document describes the synthesizable C++ hardware architecture developed in Vitis HLS for the CNN inference accelerator core. It details the hardware-software interface configurations, memory layout strategies, and specific optimization directives implemented to create a high-throughput streaming pipeline.

---

## 1. Hardware-Software Interface Design

The accelerator core communicates with the host processing system through a standardized hybrid interface model, decoupling the heavy computing data path from runtime control management.

* **Control Path (AXI4-Lite):** The module uses an `s_axilite` control interface. This maps the accelerator's internal execution state registers (`start`, `ready`, `idle`, and `done`) to specific memory addresses on the ARM processor. This layout allows the host CPU to monitor, trigger, and reset the IP core during system runtime via software drivers.
* **Data Path (AXI4-Stream):** The input data ingestion and output score retrieval bypass general register control entirely, utilizing the `axis` protocol. Data streams directly from the off-chip DDR memory through dedicated Direct Memory Access (DMA) channels into the hardware fabric, achieving maximum throughput.

---

## 2. Layer-by-Layer Hardware Engine Implementation

The hardware computing engine is designed as a deep continuous processing pipe, executing layer-by-layer neural matrix math as a unified streaming dataflow.

### A. AXI-Stream Packet Parsing & Input Buffering
The input data port ingests incoming 32-bit packets from the AXI-Stream bus. Since the images are pre-processed as 8-bit integers, the hardware core parses each 32-bit word into four independent 8-bit inputs. These unmapped values are temporarily cached into high-speed internal line buffers to constantly supply the downstream calculation nodes.

### B. Convolutional Layer 1 Core (`conv_1`)
The first computing block applies 8 separate 3x3 filter channels to the 28x28x1 input data.
* **Line Buffer & Sliding Window:** The core implements dual line-buffer memory arrays acting as internal FIFO queues alongside a 3x3 register matrix. This layout tracks a full neighborhood of pixels simultaneously, producing a new 3x3 sliding spatial frame every clock cycle.
* **Parallel Convolution Math:** The 9 registers of the neighborhood window feed concurrently into 8 parallel math lines. Each line contains specialized arithmetic units executing signed 8-bit weight multiplications and accumulating into a 32-bit signed destination buffer where the `conv_1` bias is added.
* **Streaming Rectified Linear Unit (ReLU):** The accumulated 32-bit result immediately undergoes a sign-bit check. If the bit is negative, the hardware cuts the value to zero on the fly, avoiding any latency or external memory loopbacks.
* **In-Line Max Pooling:** The activated values enter a spatial downsampling node. This block monitors adjacent $2 \times 2$ pixel neighborhoods, isolates the maximum value, and discards redundant activation frames, compressing the matrix size from 26x26x8 down to a 13x13x8 feature map.

### C. Convolutional Layer 2 Core (`conv_2`)
The second stage ingests the 13x13x8 feature map from the previous block, applying 16 independent 3x3 filter windows.
* **Multi-Channel Accumulation:** Because `conv_2` features multi-channel interactions, the arithmetic units are unrolled to scale across the 8 incoming channels. Every execution step performs a dense dot-product across the third dimension of the feature array.
* **Pipelined Filtering:** Similar to the first layer, this stage routes calculated 32-bit results through an embedded ReLU threshold and a second 2x2 Max Pooling module. This downsizes the spatial representation to a 5x5x16 feature map.

### D. Fully Connected Output Layer (`output_dense`)
The final block acts as the classification layer, mapping the 400 flattened features (5 multiplied by 5 multiplied by 16) onto the 10 target digit classes (0-9).
* **Flattening Logic:** The spatial multi-channel feature maps are unrolled into a single 1-dimensional array, streaming sequentially into the dense computing array.
* **Concurrent Class Accumulation:** To remove performance bottlenecks, the 10 accumulation nodes corresponding to each digit class are completely duplicated in hardware. As each of the 400 input elements streams through, it is concurrently multiplied by 10 distinct 8-bit weights.
* **Raw 32-bit Score Queue:** The results are accumulated inside 10 independent 32-bit signed variables along with their respective dense biases. These raw 32-bit scores are queued straight into the outbound AXI-Stream register block, bypassing the training-only Softmax activation layer to save FPGA fabric space.

---

## 3. Memory Layout & Optimization Directives

To enforce single-cycle execution speeds across the nested mathematical loops, specific pragma instructions are injected into the C++ source file to override standard sequential compiler choices:

* **Loop Pipelining (`#pragma HLS PIPELINE II=1`):** Applied to the core convolution and fully connected execution loops. This instructs Vitis HLS to achieve an Initiation Interval of 1 ($II=1$), forcing the hardware to ingest a new input element and calculate a new multi-accumulation step every single clock cycle.
* **Array Partitioning (`#pragma HLS ARRAY_PARTITION complete`):** Applied to internal weight buffers. By completely breaking down large multi-dimensional weight storage blocks into individual hardware registers, memory interface limits are eliminated. This allows the arithmetic units to perform multiple concurrent read operations in parallel without waiting on standard dual-port RAM access limits.
* **Loop Unrolling (`#pragma HLS UNROLL`):** Applied to filter channel and output class dimension loops, forcing the hardware to implement multiple physical multipliers to handle the computational load simultaneously.
