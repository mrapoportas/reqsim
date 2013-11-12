#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "srs.h"

/* When FIXED_REQUESTS is defined, the program uses a pre-defined list of
 * stripe requests defined in stripe_requests.c and indicated by REQ_LIST.
 * Otherwise, requests are produced at execution using one (REQ_GENERATOR)
 * of the generators also from stripe_requests.c. */
/*#define FIXED_REQUESTS*/
#define REQ_GENERATOR 2
#define REQ_LIST 3
/* When NATURE_OVERRIDE is defined, its value overrides the stripe request
 * nature of all requests. */
/*#define NATURE_OVERRIDE READ_REQUEST*/

extern struct {
    unsigned int requests;
    struct stripe_request *list;
} req_list[];
extern unsigned int (*req_generator[])(struct stripe_request **);

/* These stripe request properties are needed in many places. */
static unsigned int data_disks;
static unsigned int striping_unit;
static unsigned int faulty_unit;
static unsigned int first_unit;
static unsigned int request_units;

/* A function pointer is used to make formatting transparent. */
static void (*print_scope_line)(struct scope_groups *, unsigned int);

/* The string is echoed for a return value to permit visualise_scope to be
 * called in a printf call, etc.. */ 
char *visualise_scope(struct unit_scope *scope, char *scope_string)
{
    unsigned int sector, sectors;

    /* Clear the string first. */
    for (sector = 0; sector < striping_unit / SECTOR; ++sector)
        scope_string[sector] = ' ';

    /* Identify sectors in the scope. */
    sector = scope->offset / SECTOR;
    for (sectors = scope->length / SECTOR; sectors; --sectors)
        scope_string[sector++] = 'x';

    return scope_string;
}

/* print_scope_line can be used for request scopes as well, while write scopes
 * may require additional code. */

/* The majority of request service methods exhibit a property where the scope
 * group representing a faulty unit is not set (.length == 0),
 * and print_scope_line assumes this property. The exception is rr, which may
 * set other_request_units even as one of these units is faulty, and it
 * requires alternative behaviour from print_scope_line. To control which
 * behaviour is needed, a switch (mode) is added to the parameter list. A value
 * of 0 results in default behaviour, while 1 turns on the alternative branch.*/

/* Precondition: the caller is expected to set final_request_unit only when
 * there are two or more request units. */
/* Precondition: the caller is expected to set other_request_units only when
 * there are three or more request units. */
void print_scope_line_f0(struct scope_groups *sg, unsigned int mode)
{
    char *scope_string, *empty_string;

    scope_string = (char *) malloc(striping_unit / SECTOR + 1);
    empty_string = (char *) malloc(striping_unit / SECTOR + 1);
    scope_string[striping_unit / SECTOR] = '\0';
    empty_string[striping_unit / SECTOR] = '\0';
    memset(empty_string, ' ', striping_unit / SECTOR);

    if (sg->first_request_unit.length)
        printf("[%s] ", visualise_scope(&sg->first_request_unit, scope_string));
    else
        printf(" %s  ", empty_string);

    if (sg->other_request_units.length)
        /* Hard coding the factor field width to 2. Numbers above two digits
         * are not expected. */
        printf("%2d * [%s] ", request_units - 2 - mode, visualise_scope(
                    &sg->other_request_units, scope_string));
    else
        printf("      %s  ", empty_string);

    if (sg->final_request_unit.length)
        printf("[%s] ", visualise_scope(&sg->final_request_unit, scope_string));
    else
        printf(" %s  ", empty_string);

    if (sg->off_request_units.length)
        printf("%2d * [%s] ", data_disks - request_units, visualise_scope(
                    &sg->off_request_units, scope_string));
    else
        printf("      %s  ", empty_string);

    if (sg->parity_unit.length)
        printf("[%s] ", visualise_scope(&sg->parity_unit, scope_string));
    else
        printf(" %s  ", empty_string);

    free((void *) scope_string);
    free((void *) empty_string);

    /* For other request units and off-request units we rely on a zero scope
     * length to eliminate the component. */
    printf("%d bytes\n",
            sg->first_request_unit.length +
            sg->other_request_units.length * (request_units - 2 - mode) +
            sg->final_request_unit.length +
            sg->off_request_units.length * (data_disks - request_units) +
            sg->parity_unit.length);
}

/* If visualise_scope becomes a permanent solution, we should drop scope line
 * initialisation. Note, this would oblige us to arrange the right characters
 * for a faulty unit. */
/* The second parameter has no purpose at all, and is added only to allow
 * the two scope line functions to have the same parameter list. */
void print_scope_line_f1(struct scope_groups *scopes, unsigned int mode)
{
    char *scope_line; /* Unformatted scope line. */
    unsigned int line_length; /* Length of the unformatted scope line. */
    unsigned int unit; /* Stripe unit. */
    unsigned int unit_sectors; /* Sectors in the striping unit. */

    unit_sectors = striping_unit / SECTOR;

    /* Will I need a null character at the end? */
    line_length = (data_disks + 1) * unit_sectors;
    scope_line = (char *) malloc(line_length);
    memset(scope_line, ' ', line_length);

    /* Request units (3 kinds). */

    if (scopes->first_request_unit.length)
        visualise_scope(&scopes->first_request_unit, scope_line + first_unit *
                unit_sectors);

    if (scopes->other_request_units.length)
        for (unit = first_unit + 1; unit < first_unit + request_units; ++unit) {
            /* Something only rr can properly exercise at this time. */
            if (unit == faulty_unit) continue;

            visualise_scope(&scopes->other_request_units, scope_line + unit *
                    unit_sectors);
        }

    if (scopes->final_request_unit.length)
        visualise_scope(&scopes->final_request_unit, scope_line + (first_unit +
                    request_units - 1) * unit_sectors);

    /* Off-request units. */
    if (scopes->off_request_units.length) {
        for (unit = 0; unit < first_unit; ++unit)
            visualise_scope(&scopes->off_request_units, scope_line + unit *
                    unit_sectors);

        for (unit = first_unit + request_units; unit < data_disks; ++unit)
            visualise_scope(&scopes->off_request_units, scope_line + unit *
                    unit_sectors);
    }

    /* Parity unit. */
    if (scopes->parity_unit.length)
        visualise_scope(&scopes->parity_unit, scope_line + data_disks *
                unit_sectors);

    /* Formatting is added here, that is just before printing. */
    for (unit = 0; unit <= data_disks; ++unit) {
        printf("|%.*s", unit_sectors, scope_line + unit * unit_sectors);
    }
    /* For other request units and off-request units we rely on a zero scope
     * length to eliminate the component. */
    printf("| %d bytes\n",
            scopes->first_request_unit.length +
            scopes->other_request_units.length *
                (request_units - 2 - (faulty_unit > first_unit &&
                faulty_unit < first_unit + request_units - 1) ? 1 : 0) +
            scopes->final_request_unit.length +
            scopes->off_request_units.length * (data_disks - request_units) +
            scopes->parity_unit.length);

    free((void *) scope_line);
}

/* Nonredundant-write. */
void nw_method(struct scope_groups *write_scopes)
{
    /* Write scopes are currently not printed. */
}

/* Read-modify-write. */
unsigned int rmw_method(struct scope_groups *req_scopes, int and_print)
{
    struct scope_groups read_scopes = *req_scopes;

    if (request_units == 1)
        read_scopes.parity_unit = read_scopes.first_request_unit;
    else {
        read_scopes.parity_unit.offset = 0;
        read_scopes.parity_unit.length = striping_unit;
    }

    if (and_print) (*print_scope_line)(&read_scopes, 0);

    return read_scopes.first_request_unit.length +
        read_scopes.final_request_unit.length +
        read_scopes.other_request_units.length * (request_units - 2) +
        read_scopes.parity_unit.length;
}

/* Reconstruct-write. */
unsigned int rw_method(struct scope_groups *req_scopes, int and_print)
{
    struct scope_groups read_scopes = {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}};

    if (request_units == 1)
        /* For XOR to work, there need to be at least two data disks. With one
         * request unit, we can be sure there is at least one unit left off
         * request. */
        read_scopes.off_request_units = req_scopes->first_request_unit;
    else {
        if (req_scopes->first_request_unit.length < striping_unit) 
            /* Read the first unit's scope complement. */
            /* The default offset (0) is adequate. */
            read_scopes.first_request_unit.length =
                req_scopes->first_request_unit.offset;
        if (req_scopes->final_request_unit.length < striping_unit) {
            /* Read the final unit's scope complement. */
            read_scopes.final_request_unit.offset =
                req_scopes->final_request_unit.length;
            read_scopes.final_request_unit.length = striping_unit -
                req_scopes->final_request_unit.length;
        }
        /* With more than one request unit, we cannot be sure there are any
         * units left off request. */
        if (request_units < data_disks)
            /* The default offset (0) is adequate. */
            read_scopes.off_request_units.length = striping_unit;
    }

    if (and_print) (*print_scope_line)(&read_scopes, 0);

    return read_scopes.first_request_unit.length +
        read_scopes.final_request_unit.length +
        read_scopes.off_request_units.length * (data_disks - request_units);
}

/* Reconstruct-write plus. */
void rwplus_method(struct scope_groups *req_scopes, unsigned int
        first_unit_is_faulty)
{
    struct scope_groups read_scopes = {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}};
    struct unit_scope scope_complement; /* Faulty unit scope complement. */

    if (first_unit_is_faulty) {
        scope_complement.offset = 0;
        scope_complement.length = req_scopes->first_request_unit.offset;
        if (req_scopes->final_request_unit.length == striping_unit)
            read_scopes.final_request_unit = scope_complement;
        else
            /* The default offset (0) is adequate. */
            read_scopes.final_request_unit.length = striping_unit;
        if (request_units < data_disks)
            read_scopes.off_request_units = req_scopes->first_request_unit;
    }
    else
    {
        scope_complement.offset = req_scopes->final_request_unit.length;
        scope_complement.length = striping_unit - scope_complement.offset;
        if (req_scopes->first_request_unit.length == striping_unit)
            read_scopes.first_request_unit = scope_complement;
        else
            /* The default offset (0) is adequate. */
            read_scopes.first_request_unit.length = striping_unit;
        if (request_units < data_disks)
            read_scopes.off_request_units = req_scopes->final_request_unit;
    }
    if (request_units > 2)
        read_scopes.other_request_units = scope_complement;
    read_scopes.parity_unit = scope_complement;

    (*print_scope_line)(&read_scopes, 0);
}

void process_write(struct stripe_request *sr, struct scope_groups *req_scopes)
{
    unsigned int final_unit, primary, secondary;

    final_unit = first_unit + request_units - 1;

/*#define DEBUG*/

    if (sr->faulty_unit == NO_UNIT) {
#ifdef DEBUG
        rmw_method(req_scopes, 1);
        rw_method(req_scopes, 1);
#else
        /* request_units != 1 is added so that we can have two branches
         * instead of four. Maybe implication can be applied here. */
        if (request_units == 1 && data_disks > 3 || request_units != 1 &&
                striping_unit * (data_disks - 1) > 2 * sr->length) {
            primary = rmw_method(req_scopes, 1);
            secondary = rw_method(req_scopes, 0);
        }
        else {
            secondary = rmw_method(req_scopes, 0);
            primary = rw_method(req_scopes, 1);
        }
        assert(primary <= secondary);
#endif
    }
    else if (sr->faulty_unit == PARITY_UNIT)
        nw_method(req_scopes);
    /* If an off-request unit is faulty. */
    else if (sr->faulty_unit < first_unit || sr->faulty_unit > final_unit)
        rmw_method(req_scopes, 1);
    /* If there is one request unit, or the faulty unit changes completely. */
    else if (request_units == 1 || sr->faulty_unit > first_unit &&
            sr->faulty_unit < final_unit || sr->faulty_unit == first_unit &&
            req_scopes->first_request_unit.length == striping_unit ||
            sr->faulty_unit == final_unit &&
            req_scopes->final_request_unit.length == striping_unit)
        rw_method(req_scopes, 1);
    else
        /* There are two or more request units, and the faulty unit changes
         * partially. */
        rwplus_method(req_scopes, sr->faulty_unit == first_unit);
}

/* Direct-read. */
void dr_method(struct scope_groups *req_scopes)
{
    struct scope_groups read_scopes = *req_scopes;

    (*print_scope_line)(&read_scopes, 0);
}

/* Reconstruct-read. */
void rr_method(struct scope_groups *req_scopes, unsigned int faulty_unit)
{
    struct scope_groups read_scopes = {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}};
    unsigned int final_unit = first_unit + request_units - 1;
    struct unit_scope faulty_unit_scope;

    if (request_units > 1) {
        if (faulty_unit != first_unit) {
            read_scopes.first_request_unit.offset = 0;
            read_scopes.first_request_unit.length = striping_unit;
        }
        if (request_units > 2 && (faulty_unit == first_unit || faulty_unit ==
                    final_unit) || request_units > 3) {
            read_scopes.other_request_units.offset = 0;
            read_scopes.other_request_units.length = striping_unit;
        }
        if (faulty_unit != final_unit) {
            read_scopes.final_request_unit.offset = 0;
            read_scopes.final_request_unit.length = striping_unit;
        }
    }

    if (faulty_unit == first_unit)
        faulty_unit_scope = req_scopes->first_request_unit;
    else if (faulty_unit < final_unit)
        faulty_unit_scope = req_scopes->other_request_units;
    else
        faulty_unit_scope = req_scopes->final_request_unit;
    if (request_units < data_disks)
        read_scopes.off_request_units = faulty_unit_scope;
    read_scopes.parity_unit = faulty_unit_scope;

    (*print_scope_line)(&read_scopes, (faulty_unit > first_unit && faulty_unit <
                final_unit) ? 1 : 0);
}

void process_read(struct stripe_request *sr, struct scope_groups *req_scopes)
{
    unsigned int final_unit;

    final_unit = first_unit + request_units - 1;

    /* If no stripe unit is faulty, or if either the parity unit or an
     * off-request unit is faulty. */
    if (sr->faulty_unit == NO_UNIT || sr->faulty_unit == PARITY_UNIT ||
            sr->faulty_unit < first_unit || sr->faulty_unit > final_unit)
        dr_method(req_scopes);
    else
        /* A request unit is faulty. */
        rr_method(req_scopes, sr->faulty_unit);
}

/* The parity column is printed only so that we may use the same method of
 * marking a faulty unit as for data units. */

/* With the current formatting, we can fit 13 4-sector units on a line before
 * running out of columns. */
void visualise_request(struct stripe_request *sr)
{
    unsigned int column, relative_offset, sector_pos, starting_col, stripe_unit;

    if (sr->faulty_unit != NO_UNIT) {
        starting_col = 1 + (1 + striping_unit / SECTOR) * ((sr->faulty_unit !=
                       PARITY_UNIT) ? sr->faulty_unit : data_disks);
        for (column = 0; column < starting_col; ++column)
            putchar(' ');
        printf("%.*s\n", striping_unit / SECTOR, "faulty");
    }

    sector_pos = 0;
    relative_offset = sr->offset % (data_disks * striping_unit);
    for (stripe_unit = 0; stripe_unit < data_disks; ++stripe_unit) {
       putchar('|'); 
       while (sector_pos < (stripe_unit + 1) * striping_unit) {
           if (sector_pos >= relative_offset && sector_pos < relative_offset +
               sr->length)
               putchar('x');
           else
               putchar(' ');
           sector_pos += SECTOR;
       }
       if (stripe_unit == data_disks - 1) putchar('|');
    }

    /* A final block representing the parity unit is expected, but we do
     * not know what to print until the write IO method decides if it
     * needs parity. */
    while (sector_pos < (data_disks + 1) * striping_unit) {
        putchar(' ');
        sector_pos += SECTOR;
    }
    putchar('|');

    printf(" %s", (sr->nature == WRITE_REQUEST) ? "write" : "read");

    putchar('\n');
}

void process_request(struct stripe_request *sr)
{
    unsigned int stripe_rel_offset, unit_rel_offset;
    struct scope_groups req_scopes = {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}};

    data_disks = sr->data_disks;
    striping_unit = sr->striping_unit;
    faulty_unit = sr->faulty_unit;

    stripe_rel_offset = sr->offset % (data_disks * striping_unit);
    unit_rel_offset = stripe_rel_offset % striping_unit;
    first_unit = stripe_rel_offset / striping_unit;
    request_units = (unit_rel_offset + sr->length) / striping_unit +
                  (((unit_rel_offset + sr->length) % striping_unit) ? 1 : 0);

    req_scopes.first_request_unit.offset = unit_rel_offset;
    req_scopes.first_request_unit.length = (request_units == 1) ? sr->length :
        striping_unit - req_scopes.first_request_unit.offset;

    if (request_units > 1) {
        /* The default offset (0) is adequate. */
        req_scopes.final_request_unit.length = sr->length -
            req_scopes.first_request_unit.length - (request_units - 2) *
            striping_unit;

        if (request_units > 2)
            /* The default offset (0) is adequate. */
            req_scopes.other_request_units.length = striping_unit;
    }

    visualise_request(sr);

    switch (sr->nature) {
        case WRITE_REQUEST: process_write(sr, &req_scopes); break;
        case READ_REQUEST: process_read(sr, &req_scopes); break;
    }
}

int main(int argc, char *argv[])
{
    unsigned int requests;
    struct stripe_request *sr, *srs;

    print_scope_line = print_scope_line_f1;

#ifdef FIXED_REQUESTS
    requests = req_list[REQ_LIST].requests;
    srs = req_list[REQ_LIST].list;
#else
    requests = (*req_generator[REQ_GENERATOR])(&srs);
#endif

    sr = srs;
    while (requests--) {
#ifdef NATURE_OVERRIDE
        sr->nature = NATURE_OVERRIDE;
#endif
        process_request(sr);
        putchar('\n');
        sr++;
    }

#ifndef FIXED_REQUESTS
    free((void *) srs);
#endif

    return 0;
}

/* vim: set cindent shiftwidth=4 expandtab: */
