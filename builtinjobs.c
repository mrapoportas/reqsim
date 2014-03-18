#include "reqsim.h"

/* This file hosts the simulator's built-in jobs. First are generator
 * functions which dynamically produce jobs. Generators are followed by
 * fixed job lists.
 *
 * You will find a mix of symbolic constants defined in front of the
 * various jobs. The available constants are RDLEVL, DTDSKS, STUNIT,
 * FLTSTATA, and NATURE, OFFSET, LENGTH, and they correspond directly to
 * members of struct array and struct raidreq, respectively. The idea is
 * that the constants in front of each source of jobs represent the
 * parameters of the preceded source, and if a particular member is
 * without a corresponding constant, this means that particular array or
 * request attribute must use the value hard-coded in the definition of
 * the job, as this value is considered important for the purpose of the
 * generator or list.*/

/* Job generator template. */
static unsigned jbgentemplate(struct job **jobs)
{
    unsigned jbcount;
    struct job *jb;

    jbcount = 0;

    jb = *jobs = (struct job *) malloc(jbcount * sizeof (struct job));

    if (*jobs == NULL) {
        fprintf(stderr, "Could not get memory for dynamically producing "
          "simulation jobs.\n");
        exit(5);
    }

    return jbcount;
}

/* A generator producing jobs where each request is 1 sector longer than
 * the last. The first request is 1 sector long, and the last request
 * spans one whole stripe. This generator can be used for testing
 * comprehension of the rmw-rw cut-off (see reqsim.c/process_write). */

#define RDLEVL RAID4
#define DTDSKS 4
#define STUNIT (4 * SECTOR)
#define FLTSTATA FLTFREE
#define NATURE WRITEREQ

static unsigned jbgen0(struct job **jobs)
{
    unsigned jbcount, jbnum;
    struct job jb = {{RDLEVL, DTDSKS, STUNIT, FLTSTATA}, {NATURE, 0}};

    jbcount = DTDSKS * (STUNIT / SECTOR);

    *jobs = (struct job *) malloc(jbcount * sizeof (struct job));

    if (*jobs == NULL) {
        fprintf(stderr, "Could not get memory for dynamically producing "
          "simulation jobs.\n");
        exit(6);
    }

    for (jbnum = 0; jbnum < jbcount; ++jbnum) {
        jb.req.len = (jbnum + 1) * SECTOR;
        (*jobs)[jbnum] = jb;
    }

    return jbcount;
}

#undef RDLEVL
#undef DTDSKS
#undef STUNIT
#undef FLTSTATA
#undef NATURE

/* A generator producing 4 jobs, where the RAID requests are each designed
 * to expand into a pair of stripe requests serviced under the rmw method.
 * Each pair is 1 of the 4 permutations of choosing between the shortest
 * and the longest rmw request. The generator was introduced to aid the
 * study of request merging opportunities. */

/* With three or fewer data disks, the longest rmw request is never over
 * STUNIT / SECTOR sectors, and that is rw territory according to the
 * rmw-rw cut-off. */

#define RDLEVL RAID5
#define DTDSKS 7
#define STUNIT 4 * SECTOR
#define FLTSTATA FLTFREE
#define NATURE WRITEREQ
/* OREQUALTO can be used to quickly adapt the generator in case the second
 * inequality in the rmw-rw cut-off condition receives its optional =
 * component. */
/*#define OREQUALTO*/

static unsigned jbgen1(struct job **jobs)
{
    unsigned cutoff, jbcount, max, sectors;
    struct dskarray array = {RDLEVL, DTDSKS, STUNIT, FLTSTATA};
    struct raidreq req = {NATURE};
    struct job *jb;

    jbcount = 4;

    jb = *jobs = (struct job *) malloc(jbcount * sizeof (struct job));

    if (*jobs == NULL) {
        fprintf(stderr, "Could not get memory for dynamically producing "
          "simulation jobs.\n");
        exit(7);
    }

    /* The rmw-rw cut-off, in sectors. */
    cutoff = array.stripingunit / SECTOR * (array.datadsks - 1);

    /* The longest rmw stripe request we can have in the array above. */
#ifdef OREQUALTO
    max = (cutoff - ((cutoff & 1) ? 1 : 0)) / 2;
#else
    max = (cutoff - ((cutoff & 1) ? 1 : 2)) / 2;
#endif

    /* The number of sectors in a stripe. */
    sectors = array.stripingunit / SECTOR * array.datadsks;

    /* Short-short */
    req.offset = (sectors - 1) * SECTOR;
    req.len = 2 * SECTOR;
    (*jobs)[0].req = req;

    /* Short-long */
    req.offset = (sectors - 1) * SECTOR;
    req.len = SECTOR + max * SECTOR;
    (*jobs)[1].req = req;

    /* Long-short */
    req.offset = (sectors - max) * SECTOR;
    req.len = max * SECTOR + SECTOR;
    (*jobs)[2].req = req;

    /* Long-long */
    req.offset = (sectors - max) * SECTOR;
    req.len = 2 * max * SECTOR;
    (*jobs)[3].req = req;

    for (; jb < *jobs + jbcount; ++jb) jb->array = array;

    return jbcount;
}

#undef RDLEVL
#undef DTDSKS
#undef STUNIT
#undef FLTSTATA
#undef NATURE

#undef DTDSKS
#undef STUNIT
#undef FLTSTATA
#undef NATURE

/* This generator produces one job for each array fault status possible in
 * the given array. The first job describes a fault-free array, while the
 * rest each exercise a new faulty disk. Every job expands to as many
 * stripe requests as there are disks (including the parity disk) in the
 * array. This workload tests stripe rotation and stripe line
 * colouring. */

#define RDLEVL RAID5
#define DTDSKS 4
#define STUNIT 4 * SECTOR
#define NATURE WRITEREQ

static unsigned jbgen2(struct job **jobs)
{
    unsigned disk, jbcount;
    struct job *jb;
    struct dskarray array = {RDLEVL, DTDSKS, STUNIT};
    struct raidreq req = {NATURE};

    jbcount = array.datadsks + 1 + 1;

    jb = *jobs = (struct job *) malloc(jbcount * sizeof (struct job));

    if (*jobs == NULL) {
        fprintf(stderr, "Could not get memory for dynamically producing "
          "simulation jobs.\n");
        exit(8);
    }

    req.len = array.datadsks * array.stripingunit * (array.datadsks + 1);

    array.fltstata = FLTFREE;

    jb->array = array;
    jb->req = req;
    ++jb;

    for (disk = 0; disk <= array.datadsks; ++disk) {
        array.fltstata = disk;

        jb->array = array;
        jb->req = req;

        ++jb;
    }

    return jbcount;
}

#undef RDLEVL
#undef DTDSKS
#undef STUNIT
#undef NATURE

/*
|wwww|    |    |
|www |    |    |
| www|    |    |
| ww |    |    |
|wwww|wwww|    |
|wwww|ww  |    |
|  ww|wwww|    |
|  ww|ww  |    |

Stripe unit scope comprehension test.
*/

#define RDLEVL RAID4
#define FLTSTATA FLTFREE
#define NATURE WRITEREQ

static struct job jblist0[] = {
    {{RDLEVL, 2, 4 * SECTOR, FLTSTATA}, {NATURE, 0 * SECTOR, 4 * SECTOR}},
    {{RDLEVL, 2, 4 * SECTOR, FLTSTATA}, {NATURE, 0 * SECTOR, 3 * SECTOR}},
    {{RDLEVL, 2, 4 * SECTOR, FLTSTATA}, {NATURE, 1 * SECTOR, 3 * SECTOR}},
    {{RDLEVL, 2, 4 * SECTOR, FLTSTATA}, {NATURE, 1 * SECTOR, 2 * SECTOR}},
    {{RDLEVL, 2, 4 * SECTOR, FLTSTATA}, {NATURE, 0 * SECTOR, 8 * SECTOR}},
    {{RDLEVL, 2, 4 * SECTOR, FLTSTATA}, {NATURE, 0 * SECTOR, 6 * SECTOR}},
    {{RDLEVL, 2, 4 * SECTOR, FLTSTATA}, {NATURE, 2 * SECTOR, 6 * SECTOR}},
    {{RDLEVL, 2, 4 * SECTOR, FLTSTATA}, {NATURE, 2 * SECTOR, 4 * SECTOR}}
};

#undef RDLEVL
#undef FLTSTATA
#undef NATURE

/*
|    |    |    |wwww|    |    |    |    |
|    |    |    |wwww|    |    |    |    | request unit faulty
|    |    |    |wwww|    |    |    |    | off-request unit faulty (3rd disk)
|    |    |    |wwww|    |    |    |    | parity disk faulty
|    |    |    |www |    |    |    |    |
|    |    |    |www |    |    |    |    | request unit faulty
|    |    |    |www |    |    |    |    | off-request unit faulty (3rd disk)
|    |    |    |www |    |    |    |    | parity disk faulty
|    |    |    | ww |    |    |    |    |
|    |    |    | ww |    |    |    |    | request unit faulty
|    |    |    | ww |    |    |    |    | off-request unit faulty (3rd disk)
|    |    |    | ww |    |    |    |    | parity disk faulty
|    |    |    |wwww|wwww|wwww|    |    |
|    |    |    |wwww|wwww|wwww|    |    | request unit faulty (4th disk)
|    |    |    |wwww|wwww|wwww|    |    | off-request unit faulty (3rd disk)
|    |    |    |wwww|wwww|wwww|    |    | parity disk faulty
|    |    |    |wwww|wwww|www |    |    |
|    |    |    |wwww|wwww|www |    |    | request unit faulty (4th disk)
|    |    |    |wwww|wwww|www |    |    | request unit faulty (6th disk)
|    |    |    |wwww|wwww|www |    |    | off-request unit faulty (3rd disk)
|    |    |    |wwww|wwww|www |    |    | parity disk faulty
|    |    |    |   w|wwww|www |    |    |
|    |    |    |   w|wwww|www |    |    | request unit faulty (5th disk)
|    |    |    |   w|wwww|www |    |    | request unit faulty (6th disk)
|    |    |    |   w|wwww|www |    |    | off-request unit faulty (3rd disk)
|    |    |    |   w|wwww|www |    |    | parity disk faulty

Request service method selection and operation correctness test.
*/

#define RDLEVL RAID4
#define NATURE WRITEREQ

static struct job jblist1[] = {
    {{RDLEVL, 7, 4 * SECTOR, FLTFREE}, {NATURE, 12 * SECTOR,  4 * SECTOR}},
    {{RDLEVL, 7, 4 * SECTOR,       3}, {NATURE, 12 * SECTOR,  4 * SECTOR}},
    {{RDLEVL, 7, 4 * SECTOR,       2}, {NATURE, 12 * SECTOR,  4 * SECTOR}},
    {{RDLEVL, 7, 4 * SECTOR,       7}, {NATURE, 12 * SECTOR,  4 * SECTOR}},
    {{RDLEVL, 7, 4 * SECTOR, FLTFREE}, {NATURE, 12 * SECTOR,  3 * SECTOR}},
    {{RDLEVL, 7, 4 * SECTOR,       3}, {NATURE, 12 * SECTOR,  3 * SECTOR}},
    {{RDLEVL, 7, 4 * SECTOR,       2}, {NATURE, 12 * SECTOR,  3 * SECTOR}},
    {{RDLEVL, 7, 4 * SECTOR,       7}, {NATURE, 12 * SECTOR,  3 * SECTOR}},
    {{RDLEVL, 7, 4 * SECTOR, FLTFREE}, {NATURE, 13 * SECTOR,  2 * SECTOR}},
    {{RDLEVL, 7, 4 * SECTOR,       3}, {NATURE, 13 * SECTOR,  2 * SECTOR}},
    {{RDLEVL, 7, 4 * SECTOR,       2}, {NATURE, 13 * SECTOR,  2 * SECTOR}},
    {{RDLEVL, 7, 4 * SECTOR,       7}, {NATURE, 13 * SECTOR,  2 * SECTOR}},
    {{RDLEVL, 7, 4 * SECTOR, FLTFREE}, {NATURE, 12 * SECTOR, 12 * SECTOR}},
    {{RDLEVL, 7, 4 * SECTOR,       3}, {NATURE, 12 * SECTOR, 12 * SECTOR}},
    {{RDLEVL, 7, 4 * SECTOR,       2}, {NATURE, 12 * SECTOR, 12 * SECTOR}},
    {{RDLEVL, 7, 4 * SECTOR,       7}, {NATURE, 12 * SECTOR, 12 * SECTOR}},
    {{RDLEVL, 7, 4 * SECTOR, FLTFREE}, {NATURE, 12 * SECTOR, 11 * SECTOR}},
    {{RDLEVL, 7, 4 * SECTOR,       3}, {NATURE, 12 * SECTOR, 11 * SECTOR}},
    {{RDLEVL, 7, 4 * SECTOR,       5}, {NATURE, 12 * SECTOR, 11 * SECTOR}},
    {{RDLEVL, 7, 4 * SECTOR,       2}, {NATURE, 12 * SECTOR, 11 * SECTOR}},
    {{RDLEVL, 7, 4 * SECTOR,       7}, {NATURE, 12 * SECTOR, 11 * SECTOR}},
    {{RDLEVL, 7, 4 * SECTOR, FLTFREE}, {NATURE, 15 * SECTOR,  8 * SECTOR}},
    {{RDLEVL, 7, 4 * SECTOR,       4}, {NATURE, 15 * SECTOR,  8 * SECTOR}},
    {{RDLEVL, 7, 4 * SECTOR,       5}, {NATURE, 15 * SECTOR,  8 * SECTOR}},
    {{RDLEVL, 7, 4 * SECTOR,       2}, {NATURE, 15 * SECTOR,  8 * SECTOR}},
    {{RDLEVL, 7, 4 * SECTOR,       7}, {NATURE, 15 * SECTOR,  8 * SECTOR}}
};

#undef RDLEVL
#undef NATURE

/* A full-stripe write. */

#define RDLEVL RAID4
#define DTDSKS 7
#define STUNIT 4 * SECTOR
#define FLTSTATA FLTFREE
#define NATURE WRITEREQ

static struct job jblist2[] = {
    {{RDLEVL, DTDSKS, STUNIT, FLTSTATA}, {NATURE, 0, DTDSKS * STUNIT}}
};

#undef RDLEVL
#undef DTDSKS
#undef STUNIT
#undef FLTSTATA
#undef NATURE

/* One completely free job. */

#define RDLEVL RAID5
#define DTDSKS 4
#define STUNIT (4 * SECTOR)
#define FLTSTATA FLTFREE
#define NATURE WRITEREQ
#define OFFSET (0 * DTDSKS * STUNIT + DTDSKS * STUNIT - STUNIT - SECTOR)
#define LENGTH (STUNIT + SECTOR + 2 * DTDSKS * STUNIT + 3 * STUNIT)

static struct job jblist3[] = {
    {{RDLEVL, DTDSKS, STUNIT, FLTSTATA}, {NATURE, OFFSET, LENGTH}}
};

#undef RDLEVL
#undef DTDSKS
#undef STUNIT
#undef FLTSTATA
#undef NATURE
#undef OFFSET
#undef LENGTH

/* Jobs for the demo. */

#define DTDSKS 6
#define STUNIT (4 * SECTOR)
#define OFFSET (0 * DTDSKS * STUNIT + DTDSKS * STUNIT - STUNIT - SECTOR)
#define LENGTH (STUNIT + SECTOR + 2 * DTDSKS * STUNIT + 3 * STUNIT)

static struct job jblist4[] = {
    {{RAID4, DTDSKS, STUNIT, FLTFREE}, {WRITEREQ, OFFSET, LENGTH}},
    {{RAID4, DTDSKS, STUNIT,       4}, {WRITEREQ, OFFSET, LENGTH}},
    {{RAID4, DTDSKS, STUNIT, FLTFREE}, { READREQ, OFFSET, LENGTH}},
    {{RAID5, DTDSKS, STUNIT, FLTFREE}, { READREQ, OFFSET, LENGTH}}
};

#undef DTDSKS
#undef STUNIT
#undef OFFSET
#undef LENGTH

/* jbgen and jblist present the selection of job generators and job lists,
 * respectively, currently available. */

unsigned (*jbgen[])(struct job **) = {
    jbgen0, jbgen1, jbgen2
};

struct joblist jblist[] = {
    {sizeof jblist0 / sizeof (struct job), jblist0},
    {sizeof jblist1 / sizeof (struct job), jblist1},
    {sizeof jblist2 / sizeof (struct job), jblist2},
    {sizeof jblist3 / sizeof (struct job), jblist3},
    {sizeof jblist4 / sizeof (struct job), jblist4}
};

/* vim: set cindent shiftwidth=4 expandtab: */
