#include <stdlib.h>
#include <string.h>

enum raid_level {RAID4, RAID5};

#define NO_DISK -1

struct disk_array {
    enum raid_level level;
    unsigned data_disks;
    unsigned striping_unit;
    int faulty_disk; /* Can be positive or NO_DISK, with the latter
                        suggesting no disk in the array is faulty. */
};

enum request_nature {READ_REQUEST, WRITE_REQUEST};

struct raid_req {
    enum request_nature nature;
    unsigned offset; /* Absolute offset; a 64-bit value in practice. */
    unsigned len; /* Not sure if more than 32 bits are ever needed in
                     practice. */
};
 
/* Extended RAID request: a regular request together with an array
 * specification. */
struct ext_raid_req {
    struct disk_array array;
    struct raid_req req;
};

/* Extended RAID request list. */
struct erreqlist {
    unsigned reqs;
    struct ext_raid_req *list;
};

struct stripe_request {
    unsigned offset; /* Absolute offset; a 64-bit value in practice. */
    unsigned length; /* 32 bits should be adequate in practice. */
};

struct unit_scope {
    unsigned offset;
    unsigned length;
};

/* A set of all significant scope groups. */
struct scope_groups {
    struct unit_scope first_request_unit;
    struct unit_scope final_request_unit;
    struct unit_scope other_request_units;
    struct unit_scope off_request_units;
    struct unit_scope parity_unit;
};

#define SECTOR 512
