/* Diagnostic counters only; excluded from production and performance runs. */
extern unsigned gba_wait_diag[32];
#define GBA_WAIT_DIAG_ACTIVE 1
#define GBA_WAIT_DIAG(reason) (gba_wait_diag[(reason)]++)
