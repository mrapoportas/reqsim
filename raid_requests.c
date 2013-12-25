#include "srs.h"

/* This file contains functions generating RAID requests as well as an
 * area for handcrafted requests. */

/* Request generator template. */
static unsigned request_generator(struct ext_raid_req **errs)
{
    unsigned reqs;
    struct ext_raid_req *err;

    reqs = 0;

    err = *errs = (struct ext_raid_req *) malloc(reqs * sizeof (struct
      ext_raid_req));

    return reqs;
}

/* A generator producing requests where each request is 1 sector longer
 * than the last. The first request is 1 sector long, while the series
 * ends with a request spanning one whole stripe. This generator can be
 * used for testing the rmw-rw cut-off comprehension. */

#define RDLEVL RAID4
#define DTDSKS 4
#define STUNIT 2048
#define FAUDSK NO_DISK
#define NATURE WRITE_REQUEST
#define OFFSET 0 /* Not a parameter. */

static unsigned req_gen_0(struct ext_raid_req **errs)
{
    unsigned req, reqs;
    struct ext_raid_req err = {{RDLEVL, DTDSKS, STUNIT, FAUDSK},
      {NATURE, OFFSET}};

    reqs = DTDSKS * (STUNIT / SECTOR);

    *errs = (struct ext_raid_req *) malloc(reqs * sizeof (struct
      ext_raid_req));

    for (req = 1; req <= reqs; ++req) {
        err.req.len = req * SECTOR;
        (*errs)[req - 1] = err;
    }

    return reqs;
}

#undef RDLEVL
#undef DTDSKS
#undef STUNIT
#undef FAUDSK
#undef NATURE
#undef OFFSET

/* A generator producing 4 requests, each touching two stripes and
 * representing a new combination of the shortest and the longest rmw
 * requests. This is used for the study of request merging possibilities. */

/* With three or fewer data disks, the longest rmw request is never over
 * STUNIT / SECTOR sectors, and that is rw territory according to the
 * length-based cut-off rule. */

#define RDLEVL RAID5
#define DTDSKS 7
#define STUNIT 2048
#define FAUDSK NO_DISK
#define NATURE WRITE_REQUEST /* Not a parameter. */
#define OR_EQUAL_TO 0 /* The inequality in the rmw-rw cut-off condition may
                         optionally have an = component. */

static unsigned req_gen_1(struct ext_raid_req **errs)
{
    unsigned max, reqs, sectors;
    struct disk_array array = {RDLEVL, DTDSKS, STUNIT, FAUDSK};
    struct raid_req req;
    struct ext_raid_req *err;

    reqs = 4;

    err = *errs = (struct ext_raid_req *) malloc(reqs * sizeof (struct
      ext_raid_req));

    /* The measure of sectors in terms of which the length-based rmw-rw
     * cut-off is defined. */
    sectors = array.striping_unit / SECTOR * (array.data_disks - 1);
    max = (sectors - ((sectors & 1) ? 1 : (OR_EQUAL_TO) ? 0 : 2)) / 2;

    /* The true number of sectors in a stripe. */
    sectors += array.striping_unit / SECTOR;

    req.nature = NATURE;

    /* Short- */
    req.offset = (sectors - 1) * SECTOR;

    /*     short */
    req.len = 2 * SECTOR;
    err[0].req = req;

    /*     long */
    req.len = SECTOR + max * SECTOR;
    err[1].req = req;

    /* Long- */
    req.offset = (sectors - max) * SECTOR;

    /*     short */
    req.len = max * SECTOR + SECTOR;
    err[2].req = req;

    /*     long */
    req.len = 2 * max * SECTOR;
    err[3].req = req;

    for (; err < *errs + reqs; ++err) err->array = array;

    return reqs;
}

#undef RDLEVL
#undef DTDSKS
#undef STUNIT
#undef FAUDSK
#undef NATURE
#undef OR_EQUAL_TO

/* A generator testing the mapping from logical order to physical order
 * for RAID5 requests. The same test can currently be expressed without a
 * generator, but will retain its current form in the hope of generators
 * someday having their parameters exposed. */

#define DTDSKS 4
#define STUNIT 2048
#define FAUDSK NO_DISK
#define NATURE WRITE_REQUEST

static unsigned req_gen_2(struct ext_raid_req **errs)
{
    unsigned reqs;
    struct ext_raid_req ext_req = {
        {RAID5, DTDSKS, STUNIT, FAUDSK},
        {NATURE, 0, DTDSKS * STUNIT * (DTDSKS + 1)}
    };

    reqs = 1;

    *errs = (struct ext_raid_req *) malloc(reqs * sizeof (struct
      ext_raid_req));

    **errs = ext_req;

    return reqs;
}

#undef DTDSKS
#undef STUNIT
#undef FAUDSK
#undef NATURE

/* A generator to test the physical mode of the RAID request header. In
 * particular, it tests the formatting for comprehension of a faulty disk
 * as well as stripe's parity disk. */

#define RDLEVL RAID5
#define DTDSKS 4
#define STUNIT 2048
#define NATURE WRITE_REQUEST

static unsigned req_gen_3(struct ext_raid_req **errs)
{
    unsigned disk, faudsk, reqs;
    struct ext_raid_req *err;
    struct disk_array array = {RDLEVL, DTDSKS, STUNIT};
    struct raid_req req;

    reqs = array.data_disks + 1 + 1;

    err = *errs = (struct ext_raid_req *) malloc(reqs * sizeof (struct
      ext_raid_req));

    req.nature = NATURE;
    req.offset = 0;
    req.len = array.data_disks * array.striping_unit * (array.data_disks
      + 1);

    array.faulty_disk = NO_DISK;

    err->array = array;
    err->req = req;
    ++err;

    for (disk = 0; disk <= array.data_disks; ++disk) {
        array.faulty_disk = disk;

        err->array = array;
        err->req = req;

        ++err;
    }

    return reqs;
}

#undef RDLEVL
#undef DTDSKS
#undef STUNIT
#undef NATURE

/*
|dddd|    |
|ddd |    |
| ddd|    |
| dd |    |
|dddd|dddd|
|  dd|dddd|
|dddd|dd  |
|  dd|dd  |

Stripe unit scope length comprehension test.
*/

#define RDLEVL RAID4
#define DTDSKS 2
#define STUNIT 2048
#define FAUDSK NO_DISK
#define NATURE WRITE_REQUEST

static struct ext_raid_req req_list_0[] = {
    {{RDLEVL, DTDSKS, STUNIT, FAUDSK}, {NATURE, 0 * SECTOR, 4 * SECTOR}},
    {{RDLEVL, DTDSKS, STUNIT, FAUDSK}, {NATURE, 0 * SECTOR, 3 * SECTOR}},
    {{RDLEVL, DTDSKS, STUNIT, FAUDSK}, {NATURE, 1 * SECTOR, 3 * SECTOR}},
    {{RDLEVL, DTDSKS, STUNIT, FAUDSK}, {NATURE, 1 * SECTOR, 2 * SECTOR}},
    {{RDLEVL, DTDSKS, STUNIT, FAUDSK}, {NATURE, 0 * SECTOR, 8 * SECTOR}},
    {{RDLEVL, DTDSKS, STUNIT, FAUDSK}, {NATURE, 2 * SECTOR, 6 * SECTOR}},
    {{RDLEVL, DTDSKS, STUNIT, FAUDSK}, {NATURE, 0 * SECTOR, 6 * SECTOR}},
    {{RDLEVL, DTDSKS, STUNIT, FAUDSK}, {NATURE, 2 * SECTOR, 4 * SECTOR}}
};

#undef RDLEVL
#undef DTDSKS
#undef STUNIT
#undef FAUDSK
#undef NATURE

/*
|    |    |    |xxxx|    |    |    |
                ffff
|    |    |    |xxxx|    |    |    |
           ffff
|    |    |    |xxxx|    |    |    |

|    |    |    |xxx |    |    |    |
                ffff
|    |    |    |xxx |    |    |    |
           ffff
|    |    |    |xxx |    |    |    |

|    |    |    | xx |    |    |    |
                ffff
|    |    |    | xx |    |    |    |
           ffff
|    |    |    | xx |    |    |    |

|    |    |    |xxxx|xxxx|xxxx|    |
                ffff
|    |    |    |xxxx|xxxx|xxxx|    |
           ffff
|    |    |    |xxxx|xxxx|xxxx|    |

|    |    |    |xxxx|xxxx|xxx |    |
                ffff
|    |    |    |xxxx|xxxx|xxx |    |
                          ffff
|    |    |    |xxxx|xxxx|xxx |    |
           ffff
|    |    |    |xxxx|xxxx|xxx |    |

|    |    |    |   x|xxxx|xxx |    |
                     ffff
|    |    |    |   x|xxxx|xxx |    |
                          ffff
|    |    |    |   x|xxxx|xxx |    |
           ffff
|    |    |    |   x|xxxx|xxx |    |

Request service method selection and operation correctness test.
*/

#define RDLEVL RAID4
#define DTDSKS 7
#define STUNIT 2048
#define NATURE WRITE_REQUEST

static struct ext_raid_req req_list_1[] = {
    {{RDLEVL, DTDSKS, STUNIT, NO_DISK}, {NATURE, 12 * SECTOR,  4 * SECTOR}},
    {{RDLEVL, DTDSKS, STUNIT,       3}, {NATURE, 12 * SECTOR,  4 * SECTOR}},
    {{RDLEVL, DTDSKS, STUNIT,       2}, {NATURE, 12 * SECTOR,  4 * SECTOR}},
    {{RDLEVL, DTDSKS, STUNIT,  DTDSKS}, {NATURE, 12 * SECTOR,  4 * SECTOR}},
    {{RDLEVL, DTDSKS, STUNIT, NO_DISK}, {NATURE, 12 * SECTOR,  3 * SECTOR}},
    {{RDLEVL, DTDSKS, STUNIT,       3}, {NATURE, 12 * SECTOR,  3 * SECTOR}},
    {{RDLEVL, DTDSKS, STUNIT,       2}, {NATURE, 12 * SECTOR,  3 * SECTOR}},
    {{RDLEVL, DTDSKS, STUNIT,  DTDSKS}, {NATURE, 12 * SECTOR,  3 * SECTOR}},
    {{RDLEVL, DTDSKS, STUNIT, NO_DISK}, {NATURE, 13 * SECTOR,  2 * SECTOR}},
    {{RDLEVL, DTDSKS, STUNIT,       3}, {NATURE, 13 * SECTOR,  2 * SECTOR}},
    {{RDLEVL, DTDSKS, STUNIT,       2}, {NATURE, 13 * SECTOR,  2 * SECTOR}},
    {{RDLEVL, DTDSKS, STUNIT,  DTDSKS}, {NATURE, 13 * SECTOR,  2 * SECTOR}},
    {{RDLEVL, DTDSKS, STUNIT, NO_DISK}, {NATURE, 12 * SECTOR, 12 * SECTOR}},
    {{RDLEVL, DTDSKS, STUNIT,       3}, {NATURE, 12 * SECTOR, 12 * SECTOR}},
    {{RDLEVL, DTDSKS, STUNIT,       2}, {NATURE, 12 * SECTOR, 12 * SECTOR}},
    {{RDLEVL, DTDSKS, STUNIT,  DTDSKS}, {NATURE, 12 * SECTOR, 12 * SECTOR}},
    {{RDLEVL, DTDSKS, STUNIT, NO_DISK}, {NATURE, 12 * SECTOR, 11 * SECTOR}},
    {{RDLEVL, DTDSKS, STUNIT,       3}, {NATURE, 12 * SECTOR, 11 * SECTOR}},
    {{RDLEVL, DTDSKS, STUNIT,       5}, {NATURE, 12 * SECTOR, 11 * SECTOR}},
    {{RDLEVL, DTDSKS, STUNIT,       2}, {NATURE, 12 * SECTOR, 11 * SECTOR}},
    {{RDLEVL, DTDSKS, STUNIT,  DTDSKS}, {NATURE, 12 * SECTOR, 11 * SECTOR}},
    {{RDLEVL, DTDSKS, STUNIT, NO_DISK}, {NATURE, 15 * SECTOR,  8 * SECTOR}},
    {{RDLEVL, DTDSKS, STUNIT,       4}, {NATURE, 15 * SECTOR,  8 * SECTOR}},
    {{RDLEVL, DTDSKS, STUNIT,       5}, {NATURE, 15 * SECTOR,  8 * SECTOR}},
    {{RDLEVL, DTDSKS, STUNIT,       2}, {NATURE, 15 * SECTOR,  8 * SECTOR}},
    {{RDLEVL, DTDSKS, STUNIT,  DTDSKS}, {NATURE, 15 * SECTOR,  8 * SECTOR}}
};

#undef RDLEVL
#undef DTDSKS
#undef STUNIT
#undef NATURE

/* A full-stripe write. */

#define DTDSKS 7
#define STUNIT 2048

static struct ext_raid_req req_list_2[] = {
    {{RAID4, DTDSKS, STUNIT, NO_DISK}, {WRITE_REQUEST, 0, DTDSKS * STUNIT}}
};

#undef DTDSKS
#undef STUNIT

/* One completely free RAID request. */

#define RDLEVL RAID5
#define DTDSKS 4
#define STUNIT 2048
#define FAUDSK 0
#define NATURE WRITE_REQUEST
#define OFFSET (1 * DTDSKS * STUNIT)
#define LENGTH (DTDSKS * STUNIT)

static struct ext_raid_req req_list_3[] = {
    {{RDLEVL, DTDSKS, STUNIT, FAUDSK}, {NATURE, OFFSET, LENGTH}}
};

#undef RDLEVL
#undef DTDSKS
#undef STUNIT
#undef FAUDSK
#undef NATURE
#undef OFFSET
#undef LENGTH

/* req_gen and req_list present the selection of request generators and
 * RAID request lists, respectively, currently available. */

unsigned (*req_gen[])(struct ext_raid_req **) = {
    req_gen_0, req_gen_1, req_gen_2, req_gen_3
};

struct erreqlist req_list[] = {
    {sizeof req_list_0 / sizeof (struct ext_raid_req), req_list_0},
    {sizeof req_list_1 / sizeof (struct ext_raid_req), req_list_1},
    {sizeof req_list_2 / sizeof (struct ext_raid_req), req_list_2},
    {sizeof req_list_3 / sizeof (struct ext_raid_req), req_list_3}
};

/* vim: set cindent shiftwidth=4 expandtab: */
