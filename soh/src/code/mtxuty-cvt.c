#include "global.h"

void MtxConv_F2L(Mtx* m1, MtxF* m2) {
    LOG_CHECK_NULL_POINTER("m1", m1);
    LOG_CHECK_NULL_POINTER("m2", m2);

    // SOH [Unbound] Mtx is float (GBI_FLOAT_MTX); the s16.16 packing loop is gone
    guMtxF2L(m2->mf, m1);
}

void MtxConv_L2F(MtxF* m1, Mtx* m2) {
    LOG_CHECK_NULL_POINTER("m1", m1);
    LOG_CHECK_NULL_POINTER("m2", m2);
    guMtxL2F(m1, m2);
}
