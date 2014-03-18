#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum raidlvl {RAID4, RAID5};

struct dskarray {
    enum raidlvl lvl;
    unsigned datadsks;
    unsigned stripingunit;
    /* Keeps the fault (flt) status (stat) of the array (a). A value of
     * FLTFREE means the array is fault-free. Any other value is the
     * number of the disk considered faulty.*/
    int fltstata;
};

#define FLTFREE -1

enum reqnature {READREQ, WRITEREQ};

struct raidreq {
    enum reqnature nature;
    unsigned offset; /* Absolute offset; a 64-bit value in practice. */
    unsigned len;    /* Not sure if more than 32 bits are ever needed in
                        practice. */
};

/* Simulation job: a disk array specification together with a RAID request
 * made againt it.*/
struct job {
    struct dskarray array;
    struct raidreq req;
};

struct joblist {
    unsigned jbcount;
    struct job *list;
};

#define SECTOR 512

/* vim: set cindent shiftwidth=4 expandtab: */
