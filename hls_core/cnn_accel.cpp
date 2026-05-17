#include "cnn_accel.h"
#include "weights_quant.h"

void cnn_accel(hls::stream<axis_t> &in_stream, hls::stream<axis_t> &out_stream) {
    #pragma HLS INTERFACE s_axilite port=return
    #pragma HLS INTERFACE axis port=in_stream
    #pragma HLS INTERFACE axis port=out_stream

    // Internal Buffers - (Preserving your high 213 DSP performance)
    data_t l1_pool[13][13][8];
    #pragma HLS ARRAY_PARTITION variable=l1_pool dim=3 complete
    data_t l2_pool[5][5][16];
    #pragma HLS ARRAY_PARTITION variable=l2_pool dim=3 complete
    pixel_t img[28][28];

    // --- 1. LOAD INPUT ---
    Load_Loop: for(int i = 0; i < 28; i++) {
        for(int j = 0; j < 28; j++) {
            #pragma HLS PIPELINE II=1
            axis_t temp = in_stream.read();
            img[i][j] = (pixel_t)temp.data; 
        }
    }

    // --- 2. LAYER 1: CONV (8 Filters) + POOLING ---
    L1_Filters: for(int f = 0; f < 8; f++) {
        L1_Rows: for(int r = 0; r < 13; r++) {
            L1_Cols: for(int c = 0; c < 13; c++) {
                #pragma HLS PIPELINE II=1
                data_t max_val = -128; 
                for(int pr = 0; pr < 2; pr++) {
                    for(int pc = 0; pc < 2; pc++) {
                        acc_t sum = conv_1_b[f];
                        for(int kr = 0; kr < 3; kr++) {
                            for(int kc = 0; kc < 3; kc++) {
                                int ir = (r * 2 + pr) + kr;
                                int ic = (c * 2 + pc) + kc;
                                if (ir < 28 && ic < 28) {
                                    int w_idx = (kr * 24) + (kc * 8) + f;
                                    sum += (acc_t)img[ir][ic] * (acc_t)conv_1_w[w_idx];
                                }
                            }
                        }
                        if(sum < 0) sum = 0; // ReLU
                        acc_t scaled = sum * L1_SCALE;
                        if(scaled > 127) scaled = 127; 
                        data_t val = (data_t)scaled;
                        if(val > max_val) max_val = val;
                    }
                }
                l1_pool[r][c][f] = max_val;
            }
        }
    }

    // --- 3. LAYER 2: CONV (16 Filters) + POOLING ---
    L2_Filters: for(int f = 0; f < 16; f++) {
        L2_Rows: for(int r = 0; r < 5; r++) {
            L2_Cols: for(int c = 0; c < 5; c++) {
                #pragma HLS PIPELINE II=1 
                data_t max_val = -128;
                for(int pr = 0; pr < 2; pr++) {
                    for(int pc = 0; pc < 2; pc++) {
                        acc_t sum = conv_2_b[f];
                        for(int kr = 0; kr < 3; kr++) {
                            for(int kc = 0; kc < 3; kc++) {
                                for(int ch = 0; ch < 8; ch++) {
                                    int w_idx = (kr * 384) + (kc * 128) + (ch * 16) + f;
                                    int ir = (r * 2 + pr) + kr;
                                    int ic = (c * 2 + pc) + kc;
                                    sum += (acc_t)l1_pool[ir][ic][ch] * (acc_t)conv_2_w[w_idx];
                                }
                            }
                        }
                        if(sum < 0) sum = 0; // ReLU
                        acc_t scaled = sum * L2_SCALE;
                        if(scaled > 127) scaled = 127;
                        data_t val = (data_t)scaled;
                        if(val > max_val) max_val = val;
                    }
                }
                l2_pool[r][c][f] = max_val;
            }
        }
    }

    // --- 4. DENSE LAYER & OUTPUT ---
    Dense_Loop: for(int i = 0; i < 10; i++) {
        acc_t sum = output_dense_b[i];
        for(int j = 0; j < 400; j++) {
            #pragma HLS PIPELINE II=1
            int dr = (j / 16) / 5;
            int dc = (j / 16) % 5;
            int df = j % 16;
            int w_idx = (j * 10) + i;
            sum += (acc_t)l2_pool[dr][dc][df] * (acc_t)output_dense_w[w_idx];
        }

        // --- Stream Out using official axis type ---
        axis_t out_pkt;
        out_pkt.data = (ap_uint<32>)sum;
        out_pkt.keep = 0xF;
        out_pkt.strb = 0xF;
        out_pkt.last = (i == 9);
        out_stream.write(out_pkt);
    }
}