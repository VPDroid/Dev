#include "shared.rsh"

struct RetStruct {
    uint32_t i0;
    uint32_t i1;
    uint32_t i2;
    uint32_t i3;
    uint32_t i4;
    uint32_t i5;
    uint32_t i6;
    uint32_t i7;
};

rs_allocation ain0, ain1, ain2;
rs_allocation ain3;

rs_allocation aout0, aout1, aout2, aout3;

uint32_t dimX;

static bool failed = false;

uint32_t RS_KERNEL init_uint32_alloc(uint32_t x) {
    return x;
}

uint16_t RS_KERNEL init_uint16_alloc(uint32_t x) {
    return x;
}

uint32_t RS_KERNEL sum2(uint32_t in0, uint32_t in1, uint32_t x) {
    _RS_ASSERT(in0 == x);
    _RS_ASSERT(in1 == x);

    return in0 + in1;
}

struct RetStruct RS_KERNEL
sum2_struct(uint32_t in0, uint32_t in1, uint32_t x) {

    _RS_ASSERT(in0 == x);
    _RS_ASSERT(in1 == x);

    struct RetStruct retval;

    retval.i0 = in0 + in1;
    retval.i1 = in0 + in1;
    retval.i2 = in0 + in1;
    retval.i3 = in0 + in1;
    retval.i4 = in0 + in1;
    retval.i5 = in0 + in1;
    retval.i6 = in0 + in1;
    retval.i7 = in0 + in1;

    return retval;
}

uint32_t RS_KERNEL sum3(uint32_t in0, uint32_t in1, uint32_t in2, uint32_t x) {
    _RS_ASSERT(in0 == x);
    _RS_ASSERT(in1 == x);
    _RS_ASSERT(in2 == x);

    return in0 + in1 + in2;
}


uint32_t RS_KERNEL sum_mixed(uint32_t in0, uint16_t in1, uint32_t x) {
    _RS_ASSERT(in0 == x);
    _RS_ASSERT(in1 == x);

    return in0 + in1;
}

static bool test_sum2_output() {
    bool failed = false;
    uint32_t i;

    for (i = 0; i < dimX; i++) {
        _RS_ASSERT(rsGetElementAt_uint(aout0, i) ==
                   (rsGetElementAt_uint(ain0, i) +
                    rsGetElementAt_uint(ain1, i)));
    }

    if (failed) {
        rsDebug("test_sum2_output FAILED", 0);
    }
    else {
        rsDebug("test_sum2_output PASSED", 0);
    }

    return failed;
}

static bool test_sum3_output() {
    bool failed = false;
    uint32_t i;

    for (i = 0; i < dimX; i++) {
        _RS_ASSERT(rsGetElementAt_uint(aout1, i) ==
                   (rsGetElementAt_uint(ain0, i) +
                    rsGetElementAt_uint(ain1, i) +
                    rsGetElementAt_uint(ain2, i)));
    }

    if (failed) {
        rsDebug("test_sum3_output FAILED", 0);
    }
    else {
        rsDebug("test_sum3_output PASSED", 0);
    }

    return failed;
}

static bool test_sum_mixed_output() {
    bool failed = false;
    uint32_t i;

    for (i = 0; i < dimX; i++) {
        _RS_ASSERT(rsGetElementAt_uint(aout2, i) ==
                   (rsGetElementAt_uint(ain0, i) +
                    rsGetElementAt_ushort(ain3, i)));
    }

    if (failed) {
        rsDebug("test_sum_mixed_output FAILED", 0);
    }
    else {
        rsDebug("test_sum_mixed_output PASSED", 0);
    }

    return failed;
}

static bool test_sum2_struct_output() {
    bool failed = false;
    uint32_t i;

    for (i = 0; i < dimX; i++) {
        struct RetStruct *result = (struct RetStruct*)rsGetElementAt(aout3, i);

        uint32_t sum = rsGetElementAt_uint(ain0, i) +
                       rsGetElementAt_uint(ain1, i);

        _RS_ASSERT(result->i0 == sum);
        _RS_ASSERT(result->i1 == sum);
        _RS_ASSERT(result->i2 == sum);
        _RS_ASSERT(result->i3 == sum);
        _RS_ASSERT(result->i4 == sum);
        _RS_ASSERT(result->i5 == sum);
        _RS_ASSERT(result->i6 == sum);
        _RS_ASSERT(result->i7 == sum);
    }

    if (failed) {
        rsDebug("test_sum2_struct_output FAILED", 0);
    }
    else {
        rsDebug("test_sum2_struct_output PASSED", 0);
    }

    return failed;
}

void test_outputs() {
    failed |= test_sum2_output();
    failed |= test_sum3_output();
    failed |= test_sum_mixed_output();
    failed |= test_sum2_struct_output();
}

void check_test_results() {
    if (failed) {
        rsSendToClientBlocking(RS_MSG_TEST_FAILED);
    } else {
        rsSendToClientBlocking(RS_MSG_TEST_PASSED);
    }
}
