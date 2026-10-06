#ifndef UAI_LINUX_TEST_TMONITOR_H
#define UAI_LINUX_TEST_TMONITOR_H

typedef unsigned char UB;

inline int tm_putstring(const UB *) { return 0; }
inline int tm_printf(const UB *, ...) { return 0; }

#endif