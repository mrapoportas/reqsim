#include "srs.h"

/* This file contains functions generating stripe requests as well as an
 * area for handcrafted requests. */

/* Request generator template. */
unsigned int request_generator(struct stripe_request **srs)
{
    unsigned int requests;
    struct stripe_request *sr;

    requests = 0;

    *srs = (struct stripe_request *) malloc(requests * sizeof (struct
                stripe_request));

    sr = *srs;

    return requests;
}

/* A generator for testing the rmw-rw cut-off comprehension. */

#define DATA_DISKS 4
#define STRIPING_UNIT (4 * SECTOR)
#define FAULTY_UNIT NO_UNIT
#define NATURE WRITE_REQUEST
#define OFFSET 0

unsigned int request_generator_0(struct stripe_request **srs)
{

    unsigned int requests, unit_sectors;
    unsigned int kills_left, kill_limit, kill_target, n, request_units;
    struct stripe_request *sr;

    unit_sectors = STRIPING_UNIT / SECTOR;

    request_units = 2;

    kill_limit = ((request_units == 1) ? 1 : 2) * (unit_sectors - 1);

    requests = kill_limit + 1;

    *srs = (struct stripe_request *) malloc(requests * sizeof (struct
           stripe_request));

    sr = *srs;

    for (kill_target = 0; kill_target <= kill_limit; ++kill_target) {
        sr->data_disks = DATA_DISKS;
        sr->striping_unit = STRIPING_UNIT;
        sr->faulty_unit = FAULTY_UNIT;
        sr->nature = NATURE;
        sr->offset = OFFSET;
        sr->length = request_units * STRIPING_UNIT;

        kills_left = kill_target;
        n = (kills_left < unit_sectors - 1) ? kills_left : unit_sectors - 1;
        if (n) {
            sr->offset += n * SECTOR;
            sr->length -= n * SECTOR;
        }
        kills_left -= n;
        n = (kills_left < unit_sectors - 1) ? kills_left : unit_sectors - 1;
        if (n) {
            sr->length -= n * SECTOR;
        }
        ++sr;
    }

    return requests;
}

#undef DATA_DISKS
#undef STRIPING_UNIT
#undef FAULTY_UNIT
#undef NATURE
#undef OFFSET

/* A generator for testing the effectiveness of a certain modification to
 * the rw method. */

#define DATA_DISKS 4
#define STRIPING_UNIT (4 * SECTOR)
#define FAULTY_UNIT NO_UNIT
#define NATURE WRITE_REQUEST

unsigned int request_generator_1(struct stripe_request **srs)
{

    unsigned int requests;
    struct stripe_request *sr;
    unsigned int request_units;
    struct unit_scope scope;

    requests = (DATA_DISKS - 1) * (STRIPING_UNIT / SECTOR - 1);

    *srs = (struct stripe_request *) malloc(requests * sizeof (struct
                stripe_request));

    sr = *srs;

    for (request_units = 1; request_units < DATA_DISKS; ++request_units) {
        scope.length = SECTOR;
        for (; scope.length < STRIPING_UNIT; scope.length += SECTOR) {
            scope.offset = STRIPING_UNIT - scope.length;

            sr->data_disks = DATA_DISKS;
            sr->striping_unit = STRIPING_UNIT;
            sr->faulty_unit = FAULTY_UNIT;
            sr->nature = NATURE;
            sr->offset = (DATA_DISKS - request_units) * STRIPING_UNIT +
                scope.offset;
            sr->length = (request_units - 1) * STRIPING_UNIT + scope.length;

            ++sr;
        }
    }

    return requests;
}

#undef DATA_DISKS
#undef STRIPING_UNIT
#undef FAULTY_UNIT
#undef NATURE

/* A generator for exploring request merging opportunities. */
/* Note, current programming for rmw requests. */

#define DATA_DISKS 2
#define STRIPING_UNIT (4 * SECTOR)
#define FAULTY_UNIT NO_UNIT
#define NATURE WRITE_REQUEST
/* The inequality in the rmw-rw cut-off condition may optionally have an =
 * component. */
#define OR_EQUAL_TO 0

unsigned int request_generator_2(struct stripe_request **srs)
{
    unsigned int requests;
    struct stripe_request *sr;
    unsigned int max, or_equal_to, sectors;

    requests = 8;

    *srs = (struct stripe_request *) malloc(requests * sizeof (struct
                stripe_request));

    sr = *srs;

    /* Ignoring request_units == 1 && data_disks > 3 for now. */
    sectors = STRIPING_UNIT / SECTOR * (DATA_DISKS - 1);
    max = (sectors - ((sectors & 1) ? 1 : (OR_EQUAL_TO) ? 0 : 2)) / 2;

    /* The true number of sectors in a stripe. */
    sectors += STRIPING_UNIT / SECTOR;

    /* Short-short */
    sr->offset = (sectors - 1) * SECTOR;
    sr->length = SECTOR;
    ++sr;
    sr->offset = 0;
    sr->length = SECTOR;
    ++sr;

    /* Short-long */
    sr->offset = (sectors - 1) * SECTOR;
    sr->length = SECTOR;
    ++sr;
    sr->offset = 0;
    sr->length = max * SECTOR;
    ++sr;

    /* Long-short */
    sr->offset = (sectors - max) * SECTOR;
    sr->length = max * SECTOR;
    ++sr;
    sr->offset = 0;
    sr->length = SECTOR;
    ++sr;

    /* Long-long */
    sr->offset = (sectors - max) * SECTOR;
    sr->length = max * SECTOR;
    ++sr;
    sr->offset = 0;
    sr->length = max * SECTOR;

    for (sr = *srs; sr <= *srs + 7; ++sr) {
        sr->data_disks = DATA_DISKS;
        sr->striping_unit = STRIPING_UNIT;
        sr->faulty_unit = FAULTY_UNIT;
        sr->nature = NATURE;
    }

    return requests;
}

#undef DATA_DISKS
#undef STRIPING_UNIT
#undef FAULTY_UNIT
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

#define DATA_DISKS 2
#define STRIPING_UNIT (4 * SECTOR)
#define FAULTY_UNIT NO_UNIT
#define NATURE WRITE_REQUEST

struct stripe_request list0[] = {
    {DATA_DISKS, STRIPING_UNIT, FAULTY_UNIT, NATURE, 0 * SECTOR, 4 * SECTOR},
    {DATA_DISKS, STRIPING_UNIT, FAULTY_UNIT, NATURE, 0 * SECTOR, 3 * SECTOR},
    {DATA_DISKS, STRIPING_UNIT, FAULTY_UNIT, NATURE, 1 * SECTOR, 3 * SECTOR},
    {DATA_DISKS, STRIPING_UNIT, FAULTY_UNIT, NATURE, 1 * SECTOR, 2 * SECTOR},
    {DATA_DISKS, STRIPING_UNIT, FAULTY_UNIT, NATURE, 0 * SECTOR, 8 * SECTOR},
    {DATA_DISKS, STRIPING_UNIT, FAULTY_UNIT, NATURE, 2 * SECTOR, 6 * SECTOR},
    {DATA_DISKS, STRIPING_UNIT, FAULTY_UNIT, NATURE, 0 * SECTOR, 6 * SECTOR},
    {DATA_DISKS, STRIPING_UNIT, FAULTY_UNIT, NATURE, 2 * SECTOR, 4 * SECTOR}
};

#undef DATA_DISKS
#undef STRIPING_UNIT
#undef FAULTY_UNIT
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

#define DATA_DISKS 7
#define STRIPING_UNIT (4 * SECTOR)
#define NATURE WRITE_REQUEST

struct stripe_request list1[] = {
    {DATA_DISKS, STRIPING_UNIT,     NO_UNIT, NATURE, 12 * SECTOR, 4 * SECTOR},
    {DATA_DISKS, STRIPING_UNIT,           3, NATURE, 12 * SECTOR, 4 * SECTOR},
    {DATA_DISKS, STRIPING_UNIT,           2, NATURE, 12 * SECTOR, 4 * SECTOR},
    {DATA_DISKS, STRIPING_UNIT, PARITY_UNIT, NATURE, 12 * SECTOR, 4 * SECTOR},
    {DATA_DISKS, STRIPING_UNIT,     NO_UNIT, NATURE, 12 * SECTOR, 3 * SECTOR},
    {DATA_DISKS, STRIPING_UNIT,           3, NATURE, 12 * SECTOR, 3 * SECTOR},
    {DATA_DISKS, STRIPING_UNIT,           2, NATURE, 12 * SECTOR, 3 * SECTOR},
    {DATA_DISKS, STRIPING_UNIT, PARITY_UNIT, NATURE, 12 * SECTOR, 3 * SECTOR},
    {DATA_DISKS, STRIPING_UNIT,     NO_UNIT, NATURE, 13 * SECTOR, 2 * SECTOR},
    {DATA_DISKS, STRIPING_UNIT,           3, NATURE, 13 * SECTOR, 2 * SECTOR},
    {DATA_DISKS, STRIPING_UNIT,           2, NATURE, 13 * SECTOR, 2 * SECTOR},
    {DATA_DISKS, STRIPING_UNIT, PARITY_UNIT, NATURE, 13 * SECTOR, 2 * SECTOR},
    {DATA_DISKS, STRIPING_UNIT,     NO_UNIT, NATURE, 12 * SECTOR, 12 * SECTOR},
    {DATA_DISKS, STRIPING_UNIT,           3, NATURE, 12 * SECTOR, 12 * SECTOR},
    {DATA_DISKS, STRIPING_UNIT,           2, NATURE, 12 * SECTOR, 12 * SECTOR},
    {DATA_DISKS, STRIPING_UNIT, PARITY_UNIT, NATURE, 12 * SECTOR, 12 * SECTOR},
    {DATA_DISKS, STRIPING_UNIT,     NO_UNIT, NATURE, 12 * SECTOR, 11 * SECTOR},
    {DATA_DISKS, STRIPING_UNIT,           3, NATURE, 12 * SECTOR, 11 * SECTOR},
    {DATA_DISKS, STRIPING_UNIT,           5, NATURE, 12 * SECTOR, 11 * SECTOR},
    {DATA_DISKS, STRIPING_UNIT,           2, NATURE, 12 * SECTOR, 11 * SECTOR},
    {DATA_DISKS, STRIPING_UNIT, PARITY_UNIT, NATURE, 12 * SECTOR, 11 * SECTOR},
    {DATA_DISKS, STRIPING_UNIT,     NO_UNIT, NATURE, 15 * SECTOR, 8 * SECTOR},
    {DATA_DISKS, STRIPING_UNIT,           4, NATURE, 15 * SECTOR, 8 * SECTOR},
    {DATA_DISKS, STRIPING_UNIT,           5, NATURE, 15 * SECTOR, 8 * SECTOR},
    {DATA_DISKS, STRIPING_UNIT,           2, NATURE, 15 * SECTOR, 8 * SECTOR},
    {DATA_DISKS, STRIPING_UNIT, PARITY_UNIT, NATURE, 15 * SECTOR, 8 * SECTOR}
};

#undef DATA_DISKS
#undef STRIPING_UNIT
#undef NATURE

/* Full-stripe write. */
struct stripe_request list2[] = {
    {7, 4 * SECTOR, NO_UNIT, WRITE_REQUEST, 0, 7 * (4 * SECTOR)}
};

#define DATA_DISKS 7
#define STRIPING_UNIT (4 * SECTOR)
#define FAULTY_UNIT NO_UNIT
#define NATURE WRITE_REQUEST

struct stripe_request list3[] = {
    {DATA_DISKS, STRIPING_UNIT, FAULTY_UNIT, NATURE, 27 * SECTOR, SECTOR},
    {DATA_DISKS, STRIPING_UNIT, FAULTY_UNIT, NATURE, 0, SECTOR},

    {DATA_DISKS, STRIPING_UNIT, FAULTY_UNIT, NATURE, 27 * SECTOR, SECTOR},
    {DATA_DISKS, STRIPING_UNIT, FAULTY_UNIT, NATURE, 0, 13 * SECTOR},

    {DATA_DISKS, STRIPING_UNIT, FAULTY_UNIT, NATURE, 15 * SECTOR, 13 * SECTOR},
    {DATA_DISKS, STRIPING_UNIT, FAULTY_UNIT, NATURE, 0, SECTOR},

    {DATA_DISKS, STRIPING_UNIT, FAULTY_UNIT, NATURE, 15 * SECTOR, 13 * SECTOR},
    {DATA_DISKS, STRIPING_UNIT, FAULTY_UNIT, NATURE, 0, 13 * SECTOR}
};

#undef DATA_DISKS
#undef STRIPING_UNIT
#undef FAULTY_UNIT
#undef NATURE

/* req_generator and req_list present the selection of request generators
 * and stripe request lists, respectively, currently available. */

unsigned int (*req_generator[])(struct stripe_request **) = {
    request_generator_0, request_generator_1, request_generator_2
};

struct {
    unsigned int requests;
    struct stripe_request *list;
} req_list[] = {
    {sizeof list0 / sizeof (struct stripe_request), list0},
    {sizeof list1 / sizeof (struct stripe_request), list1},
    {sizeof list2 / sizeof (struct stripe_request), list2},
    {sizeof list3 / sizeof (struct stripe_request), list3}
};
