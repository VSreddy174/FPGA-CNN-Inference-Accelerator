#ifndef CNN_ACCEL_H
#define CNN_ACCEL_H

#include <ap_int.h>
#include <hls_stream.h>
#include <ap_axi_sdata.h>

// --- Integer Types ---
typedef ap_int<8> data_t;
typedef ap_uint<8> pixel_t;
typedef ap_int<32> acc_t;

// --- MANDATORY: Use the official HLS axis type ---
// This guarantees TDATA is 32-bit and TLAST/TKEEP/TSTRB are correctly mapped.
typedef hls::axis<ap_uint<32>, 0, 0, 0> axis_t;

void cnn_accel(hls::stream<axis_t> &in_stream, hls::stream<axis_t> &out_stream);

#endif