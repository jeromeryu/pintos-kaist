#include <stdint.h>

#define FRACTION (1<<14)

int float_plus_int(int a, int b);
int float2intround(int a);
int int2real(int a);
int float_mult_float(int a, int b);
int float_div_float(int a, int b);

int float_plus_int(int a, int b){
    return a + b * FRACTION;
}

int float2intround(int a){
    if(a >= 0){
        return (a + FRACTION / 2) / FRACTION;
    } else {
        return (a - FRACTION / 2) / FRACTION;
    }
}

int int2real(int a){
    return a*FRACTION;
}

int float_mult_float(int a, int b){
    return (int64_t)(a) * b / FRACTION;
}

int float_div_float(int a, int b){
    return (int64_t)(a) * FRACTION / b;
}
