#ifndef UAI_LINUX_TEST_TKERNEL_H
#define UAI_LINUX_TEST_TKERNEL_H

#include <cstdint>

using INT = int;
using ID = int;
using ER = int;
using PRI = int;
using RELTIM = unsigned int;
using SZ = unsigned int;
using FP = void (*)(INT, void *);

inline constexpr ER E_OK = 0;
inline constexpr int TA_HLNG = 1;
inline constexpr int TA_USERBUF = 2;

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

inline ID tk_get_tid() { return 1; }
inline ID tk_cre_tsk(const T_CTSK *) { return 2; }
inline ER tk_sta_tsk(ID, int) { return E_OK; }
inline ER tk_ter_tsk(ID) { return E_OK; }
inline ER tk_del_tsk(ID) { return E_OK; }
inline ER tk_dly_tsk(RELTIM) { return E_OK; }
inline ER tk_ref_tsk(ID, T_RTSK *) { return E_OK; }
inline ER tk_get_otm(SYSTIM *) { return E_OK; }
inline void tk_ext_tsk() {}

#endif