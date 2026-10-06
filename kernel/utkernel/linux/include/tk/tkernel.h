#pragma once

#include <cstdint>

using INT = int;
using UINT = unsigned int;
using ID = int;
using ER = int;
using PRI = int;
using RELTIM = unsigned int;
using SZ = unsigned int;
using TMO = int;
using FP = void (*)(INT, void *);

inline constexpr ER E_OK = 0;
inline constexpr ER E_TMOUT = -50;
inline constexpr int TA_HLNG = 1;
inline constexpr int TA_USERBUF = 2;
inline constexpr int TA_TFIFO = 4;
inline constexpr TMO TMO_POL = 0;
inline constexpr TMO TMO_FEVR = -1;

struct T_CMBF {
    int mbfatr = 0;
    SZ bufsz = 0;
    SZ maxmsz = 0;
    void *bufptr = nullptr;
};

ID tk_cre_mbf(const T_CMBF *config);
ER tk_snd_mbf(ID queue, const void *message, SZ size, TMO timeout);
INT tk_rcv_mbf(ID queue, void *message, TMO timeout);

inline constexpr int TA_INHERIT = 0x02;
struct T_CMTX {
    void *exinf = nullptr;
    int mtxatr = 0;
    PRI ceilpri = 0;
};
/* Defined by the test that needs mutex behaviour. */
ID tk_cre_mtx(const T_CMTX *config);
ER tk_loc_mtx(ID mutex, TMO timeout);
ER tk_unl_mtx(ID mutex);

struct T_CTSK {
    void *exinf = nullptr;
    int tskatr = 0;
    FP task = nullptr;
    PRI itskpri = 0;
    SZ stksz = 0;
    void *bufptr = nullptr;
};
struct T_RTSK {
    unsigned int tskstat = 0;
    unsigned int tskwait = 0;
    ID wid = 0;
    PRI tskpri = 0;
    PRI tskbpri = 0;
};
struct SYSTIM { std::uint32_t lo = 0U; };

namespace uai::test::kernel {
inline T_CTSK created_task{};
inline void (*delay_hook)(RELTIM) = nullptr;
}

inline ID tk_get_tid() { return 1; }
inline ID tk_cre_tsk(const T_CTSK *task)
{
    uai::test::kernel::created_task = *task;
    return 2;
}
inline ER tk_sta_tsk(ID, int) { return E_OK; }
inline ER tk_ter_tsk(ID) { return E_OK; }
inline ER tk_del_tsk(ID) { return E_OK; }
inline ER tk_dly_tsk(RELTIM delay)
{
    if (uai::test::kernel::delay_hook != nullptr) {
        uai::test::kernel::delay_hook(delay);
    }
    return E_OK;
}
inline ER tk_ref_tsk(ID, T_RTSK *) { return E_OK; }
inline ER tk_get_otm(SYSTIM *) { return E_OK; }
inline void tk_ext_tsk() {}
