#include <stdlib.h>
#include <string.h>

enum request_nature {READ_REQUEST, WRITE_REQUEST};

struct stripe_request {
    unsigned int data_disks;
    unsigned int striping_unit;
    int faulty_unit;
    enum request_nature nature;
    unsigned int offset; /* Absolute offset; a 64-bit value in practice. */
    unsigned int length;
};

/* Special values for faulty unit. */
#define NO_UNIT -2
#define PARITY_UNIT -1

struct unit_scope {
    unsigned int offset;
    unsigned int length;
};

/* A set of all significant scope groups. */
struct scope_groups {
    struct unit_scope first_request_unit;
    struct unit_scope final_request_unit;
    struct unit_scope other_request_units;
    struct unit_scope off_request_units;
    struct unit_scope parity_unit;
};

struct request_list {
    unsigned int requests;
    struct stripe_request *list;
};

enum raid_level {RAID4, RAID5};

#define NO_DISK -1

struct disk_array {
    enum raid_level level;
    unsigned data_disks;
    unsigned striping_unit;
    int faulty_disk; /* Can be positive or NO_DISK, with the latter
                        suggesting no disk in the array is faulty. */
};

struct raid_req {
    enum request_nature nature;
    unsigned offset;
    unsigned len;
};

#define SECTOR 512

unsigned disktounit(struct disk_array *array, unsigned disk, unsigned stripe);
