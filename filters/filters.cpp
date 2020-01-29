#include "filters.h"

short RX_hilbert45[HILBERT_COEFFS] = {
#include "RX_hilbert_45.h" 
};

short RX_hilbertm45[HILBERT_COEFFS] = {
#include "RX_hilbert_m45.h" 
};

short firbpf_usb[BPF_COEFFS] = {
#include "fir_usb.h" 
};

short firbpf_lsb[BPF_COEFFS] = {
#include "fir_lsb.h" 
};

