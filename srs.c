#include <assert.h>
#include <stdio.h>

#include "srs.h"

/* When FIXED_REQUESTS is defined, the program uses a pre-defined list of
 * RAID requests defined in raid_requests.c and exposed via req_list when
 * qualified with REQ_LIST. Otherwise, requests are produced at execution
 * using one (REQ_GENERATOR/req_gen) of the generators also from
 * raid_requests.c. */
#define FIXED_REQUESTS
#define REQ_GENERATOR 3
#define REQ_LIST 4

extern struct erreqlist req_list[];
extern unsigned (*req_gen[])(struct ext_raid_req **);

static struct disk_array *array; /* The current disk array. */
static struct raid_req *rd_req; /* The current RAID request. */
static struct stripe_request *sr; /* The current stripe request. */
static unsigned first_unit;
static unsigned request_units;
static unsigned stripe; /* Physical stripe number. In single-level RAID
                           configurations, the physical number is the same
                           as a stripe's logical number. */
static int fustat; /* Keeps the faulty unit status of the current stripe.
                      A positive value indicates the faulty unit, NO-UNIT
                      means the stripe is fault-free, and PARITY_UNIT
                      means the stripe's parity unit falls on the faulty
                      disk. */
#define NO_UNIT -2
#define PARITY_UNIT -1

/* A function pointer is used to make formatting transparent. */
static void (*print_scope_line)(struct scope_groups *, unsigned);

/* This function maps disks to stripe units. In other words, physical order
 * is transformed into logical order. For RAID4 arrays, it is an identity
 * function. For RAID5, left-symmetric placement is assumed, as that is
 * what currently interests us. */
static unsigned disktounit(unsigned disk)
{
    return (array->level == RAID4) ? disk : (disk + stripe) %
      (array->data_disks + 1);
}

/* This function translates a RAID request to one or more stripe requests. */
static unsigned expandraidreq(struct stripe_request **srs)
{
    unsigned ext_len, next_offset, requests, stripe_len;
    struct stripe_request *sr;

    /* We are interested in the stripe length without the parity unit, in
     * other words, the length of the logical stripe. */
    stripe_len = array->data_disks * array->striping_unit;

    /* Extended length: RAID request length + the stripe-relative offset.
     * This limits alignment uncertainty to one end of the request without
     * affecting the number of stripe requests. */
    ext_len = rd_req->len + rd_req->offset % stripe_len;
    requests = ext_len / stripe_len;
    if (ext_len - requests * stripe_len) ++requests;

    sr = *srs = (struct stripe_request *) malloc(requests * sizeof (struct
      stripe_request));

    /* The first stripe request. */
    sr->offset = rd_req->offset;
    sr->length = (requests == 1) ? rd_req->len : stripe_len - rd_req->offset %
      stripe_len;

    if (requests > 1) {
        next_offset = sr->offset + sr->length;

        /* Stripe requests between the first and the last, if there are any. */
        for (++sr; sr < *srs + requests - 1; ++sr) {
            sr->offset = next_offset;
            sr->length = stripe_len;
            next_offset += stripe_len;
        }

        /* The last stripe request. */
        sr->offset = next_offset;
        sr->length = rd_req->len - (*srs)->length - (requests - 2) *
          stripe_len;
    }

    return requests;
}

/* The string is echoed for a return value to permit visualise_scope to be
 * called in a printf call, etc.. */ 
static char *visualise_scope(struct unit_scope *scope, char *scope_string)
{
    unsigned sector, sectors;

    /* Clear the string first. */
    for (sector = 0; sector < array->striping_unit / SECTOR; ++sector)
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
static void print_scope_line_f0(struct scope_groups *sg, unsigned mode)
{
    char *scope_string, *empty_string;

    scope_string = (char *) malloc(array->striping_unit / SECTOR + 1);
    empty_string = (char *) malloc(array->striping_unit / SECTOR + 1);
    scope_string[array->striping_unit / SECTOR] = '\0';
    empty_string[array->striping_unit / SECTOR] = '\0';
    memset(empty_string, ' ', array->striping_unit / SECTOR);

    if (sg->first_request_unit.length)
        printf("[%s] ", visualise_scope(&sg->first_request_unit, scope_string));
    else
        printf(" %s  ", empty_string);

    if (sg->other_request_units.length)
        /* Hard coding the factor field width to 2. Numbers above two digits
         * are not expected. */
        printf("%2d * [%s] ", request_units - 2 - mode,
         visualise_scope(&sg->other_request_units, scope_string));
    else
        printf("      %s  ", empty_string);

    if (sg->final_request_unit.length)
        printf("[%s] ", visualise_scope(&sg->final_request_unit, scope_string));
    else
        printf(" %s  ", empty_string);

    if (sg->off_request_units.length)
        printf("%2d * [%s] ", array->data_disks - request_units,
         visualise_scope(&sg->off_request_units, scope_string));
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
     sg->off_request_units.length * (array->data_disks - request_units) +
     sg->parity_unit.length);
}

/* If visualise_scope becomes a permanent solution, we should drop scope line
 * initialisation. Note, this would oblige us to arrange the right characters
 * for a faulty unit. */

/* The second parameter has no purpose at all, and is added only to allow
 * the two scope line functions to have the same parameter list. */
static void print_scope_line_f1(struct scope_groups *scopes, unsigned mode)
{
    char *scope_line; /* Unformatted scope line. */
    unsigned disk; /* Which disk. */
    unsigned line_length; /* Length of the unformatted scope line. */
    unsigned unit; /* Stripe unit. */
    unsigned unit_sectors; /* Sectors in the striping unit. */

    unit_sectors = array->striping_unit / SECTOR;

    /* Will I need a null character at the end? */
    line_length = (array->data_disks + 1) * unit_sectors;
    scope_line = (char *) malloc(line_length);
    memset(scope_line, ' ', line_length);

    /* Request units (3 kinds). */

    if (scopes->first_request_unit.length)
        visualise_scope(&scopes->first_request_unit, scope_line + first_unit *
         unit_sectors);

    if (scopes->other_request_units.length)
        for (unit = first_unit + 1; unit < first_unit + request_units; ++unit) {
            /* Something only rr can properly exercise at this time. */
            if (unit == fustat) continue;

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

        unit = first_unit + request_units;
        for (; unit < array->data_disks; ++unit)
            visualise_scope(&scopes->off_request_units, scope_line + unit *
             unit_sectors);
    }

    /* Parity unit. */
    if (scopes->parity_unit.length)
        visualise_scope(&scopes->parity_unit, scope_line + array->data_disks *
         unit_sectors);

    /* Formatting is added here, that is, just before printing. */
    for (disk = 0; disk <= array->data_disks; ++disk) {
        unit = disktounit(disk);
        putchar('|');
        printf("%.*s", unit_sectors, scope_line + unit * unit_sectors);
    }

    /* For other request units and off-request units we rely on a zero scope
     * length to eliminate the component. */
    printf("| %d bytes\n",
      scopes->first_request_unit.length +
      scopes->other_request_units.length * (request_units - 2 - ((fustat >
      first_unit && fustat < first_unit + request_units - 1) ? 1 : 0)) +
      scopes->final_request_unit.length +
      scopes->off_request_units.length * (array->data_disks - request_units) +
      scopes->parity_unit.length);

    free((void *) scope_line);
}

/* Nonredundant-write. */
static void nw_method(struct scope_groups *write_scopes)
{
    struct scope_groups read_scopes = {{0, 0}};

    (*print_scope_line)(&read_scopes, 0);
}

/* Read-modify-write. */
static unsigned rmw_method(struct scope_groups *req_scopes, int and_print)
{
    struct scope_groups read_scopes = *req_scopes;

    if (request_units == 1)
        read_scopes.parity_unit = read_scopes.first_request_unit;
    else {
        read_scopes.parity_unit.offset = 0;
        read_scopes.parity_unit.length = array->striping_unit;
    }

    if (and_print) (*print_scope_line)(&read_scopes, 0);

    return read_scopes.first_request_unit.length +
     read_scopes.final_request_unit.length +
     read_scopes.other_request_units.length * (request_units - 2) +
     read_scopes.parity_unit.length;
}

/* Reconstruct-write. */
static unsigned rw_method(struct scope_groups *req_scopes, int and_print)
{
    struct scope_groups read_scopes = {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}};

    if (request_units == 1)
        /* For XOR to work, there need to be at least two data disks. With one
         * request unit, we can be sure there is at least one unit left off
         * request. */
        read_scopes.off_request_units = req_scopes->first_request_unit;
    else {
        if (req_scopes->first_request_unit.length < array->striping_unit) 
            /* Read the first unit's scope complement. */
            /* The default offset (0) is adequate. */
            read_scopes.first_request_unit.length =
             req_scopes->first_request_unit.offset;
        if (req_scopes->final_request_unit.length < array->striping_unit) {
            /* Read the final unit's scope complement. */
            read_scopes.final_request_unit.offset =
             req_scopes->final_request_unit.length;
            read_scopes.final_request_unit.length = array->striping_unit -
             req_scopes->final_request_unit.length;
        }
        /* With more than one request unit, we cannot be sure there are any
         * units left off request. */
        if (request_units < array->data_disks)
            /* The default offset (0) is adequate. */
            read_scopes.off_request_units.length = array->striping_unit;
    }

    if (and_print) (*print_scope_line)(&read_scopes, 0);

    return read_scopes.first_request_unit.length +
     read_scopes.final_request_unit.length +
     read_scopes.off_request_units.length * (array->data_disks - request_units);
}

/* Reconstruct-write plus. */
static void rwplus_method(struct scope_groups *req_scopes)
{
    struct scope_groups read_scopes = {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}};
    struct unit_scope scope_complement; /* Faulty unit scope complement. */

    if (fustat == first_unit) {
        scope_complement.offset = 0;
        scope_complement.length = req_scopes->first_request_unit.offset;
        if (req_scopes->final_request_unit.length == array->striping_unit)
            read_scopes.final_request_unit = scope_complement;
        else
            /* The default offset (0) is adequate. */
            read_scopes.final_request_unit.length = array->striping_unit;
        if (request_units < array->data_disks)
            read_scopes.off_request_units = req_scopes->first_request_unit;
    }
    else
    {
        scope_complement.offset = req_scopes->final_request_unit.length;
        scope_complement.length = array->striping_unit -
          scope_complement.offset;
        if (req_scopes->first_request_unit.length == array->striping_unit)
            read_scopes.first_request_unit = scope_complement;
        else
            /* The default offset (0) is adequate. */
            read_scopes.first_request_unit.length = array->striping_unit;
        if (request_units < array->data_disks)
            read_scopes.off_request_units = req_scopes->final_request_unit;
    }
    if (request_units > 2)
        read_scopes.other_request_units = scope_complement;
    read_scopes.parity_unit = scope_complement;

    (*print_scope_line)(&read_scopes, 0);
}

static void process_write(struct scope_groups *req_scopes)
{
    unsigned final_unit, primary, secondary;

    final_unit = first_unit + request_units - 1;

/*#define DEBUG*/

    if (fustat == NO_UNIT) {
#ifdef DEBUG
        rmw_method(req_scopes, 1);
        rw_method(req_scopes, 1);
#else
        /* request_units != 1 is added so that we can have two branches
         * instead of four. */
        if (request_units == 1 && array->data_disks > 3 || request_units
          != 1 && array->striping_unit * (array->data_disks - 1) > 2 *
          sr->length) {
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
    else if (fustat == PARITY_UNIT)
        nw_method(req_scopes);
    /* If an off-request unit is faulty. */
    else if (fustat < first_unit || fustat > final_unit)
        rmw_method(req_scopes, 1);
    /* If there is one request unit, or the faulty unit changes completely. */
    else if (request_units == 1 || fustat > first_unit && fustat <
      final_unit || fustat == first_unit &&
      req_scopes->first_request_unit.length == array->striping_unit ||
      fustat == final_unit && req_scopes->final_request_unit.length ==
      array->striping_unit)
        rw_method(req_scopes, 1);
    else
        /* There are two or more request units, and the faulty unit changes
         * partially. */
        rwplus_method(req_scopes);
}

/* Direct-read. */
static void dr_method(struct scope_groups *req_scopes)
{
    struct scope_groups read_scopes = *req_scopes;

    (*print_scope_line)(&read_scopes, 0);
}

/* Reconstruct-read. */
static void rr_method(struct scope_groups *req_scopes)
{
    struct scope_groups read_scopes = {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}};
    unsigned final_unit = first_unit + request_units - 1;
    struct unit_scope faulty_unit_scope;

    if (request_units > 1) {
        if (fustat != first_unit) {
            read_scopes.first_request_unit.offset = 0;
            read_scopes.first_request_unit.length = array->striping_unit;
        }
        if (request_units > 2 && (fustat == first_unit || fustat ==
          final_unit) || request_units > 3) {
            read_scopes.other_request_units.offset = 0;
            read_scopes.other_request_units.length = array->striping_unit;
        }
        if (fustat != final_unit) {
            read_scopes.final_request_unit.offset = 0;
            read_scopes.final_request_unit.length = array->striping_unit;
        }
    }

    if (fustat == first_unit)
        faulty_unit_scope = req_scopes->first_request_unit;
    else if (fustat < final_unit)
        faulty_unit_scope = req_scopes->other_request_units;
    else
        faulty_unit_scope = req_scopes->final_request_unit;

    if (request_units < array->data_disks)
        read_scopes.off_request_units = faulty_unit_scope;

    read_scopes.parity_unit = faulty_unit_scope;

    (*print_scope_line)(&read_scopes, (fustat > first_unit && fustat <
      final_unit) ? 1 : 0);
}

static void process_read(struct scope_groups *req_scopes)
{
    unsigned final_unit;

    final_unit = first_unit + request_units - 1;

    /* If no stripe unit is faulty, or if either the parity unit or an
     * off-request unit is faulty. */
    if (fustat == NO_UNIT || fustat == PARITY_UNIT || fustat < first_unit
      || fustat > final_unit)
        dr_method(req_scopes);
    else
        /* A request unit is faulty. */
        rr_method(req_scopes);
}

/* The parity column is printed only so that we may use the same method of
 * marking a faulty unit as for data units. */

/* With the current formatting, we can fit 13 4-sector units on a line before
 * running out of columns. */

/* The future of visualise_request is uncertain. */
static void visualise_request()
{
    unsigned column, relative_offset, sector_pos, starting_col, stripe_unit;

    if (fustat != NO_UNIT) {
        starting_col = 1 + (1 + array->striping_unit / SECTOR) * ((fustat
          != PARITY_UNIT) ? fustat : array->data_disks);
        for (column = 0; column < starting_col; ++column) putchar(' ');
        printf("%.*s\n", array->striping_unit / SECTOR, "faulty");
    }

    sector_pos = 0;
    relative_offset = sr->offset % (array->data_disks * array->striping_unit);
    for (stripe_unit = 0; stripe_unit < array->data_disks; ++stripe_unit) {
       putchar('|'); 
       while (sector_pos < (stripe_unit + 1) * array->striping_unit) {
           if (sector_pos >= relative_offset && sector_pos < relative_offset +
            sr->length)
               putchar('x');
           else
               putchar(' ');
           sector_pos += SECTOR;
       }
       if (stripe_unit == array->data_disks - 1) putchar('|');
    }

    /* A final block representing the parity unit is expected, but we do
     * not know what to print until the write IO method decides if it
     * needs parity. */
    while (sector_pos < (array->data_disks + 1) * array->striping_unit) {
        putchar(' ');
        sector_pos += SECTOR;
    }
    putchar('|');

    printf(" %s", (rd_req->nature == WRITE_REQUEST) ? "write" : "read");

    putchar('\n');
}

static void process_request()
{
    unsigned ext_len; /* Once more we see the extended length concept. */
    unsigned s_rel_offset; /* Request offset relative to the stripe. */
    unsigned u_rel_offset; /* Request offset relative to the first stripe
                              unit. */
    struct scope_groups req_scopes = {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}};

    s_rel_offset = sr->offset - stripe * (array->data_disks *
      array->striping_unit);
    first_unit = s_rel_offset / array->striping_unit;
    u_rel_offset = s_rel_offset - first_unit * array->striping_unit;

    ext_len = sr->length + u_rel_offset;
    request_units = ext_len / array->striping_unit;
    if (ext_len - request_units * array->striping_unit) ++request_units;

    req_scopes.first_request_unit.offset = u_rel_offset;
    req_scopes.first_request_unit.length = (request_units == 1) ? sr->length :
      array->striping_unit - req_scopes.first_request_unit.offset;

    if (request_units > 1) {
        /* The default offset (0) is adequate. */
        req_scopes.final_request_unit.length = sr->length -
          req_scopes.first_request_unit.length - (request_units - 2) *
          array->striping_unit;

        if (request_units > 2)
            /* The default offset (0) is adequate. */
            req_scopes.other_request_units.length = array->striping_unit;
    }

    if (array->faulty_disk == NO_DISK)
        fustat = NO_UNIT;
    else {
        fustat = disktounit(array->faulty_disk);
        if (fustat == array->data_disks) fustat = PARITY_UNIT;
    }

    /*visualise_request();*/

    if (rd_req->nature == WRITE_REQUEST)
        process_write(&req_scopes);
    else
        process_read(&req_scopes);
}

/* Treats a terminating \0 like any other character, so expect its
 * position to change. */
static void rotatestringleft(char *string, unsigned len, unsigned units)
{
    char *temp;

    if (units) {
        temp = (char *) malloc(units);

        memcpy(temp, string, units);

        memmove(string, string + units, len - units);

        memcpy(string + len - units, temp, units);

        free((void *) temp);
    }
}

/* Formatting common to every stripe request line in the RAID request
 * header. */
static void colourandprintref2(char *string, unsigned stringlen, unsigned
  offset)
{
    struct felemt { /* Formatting element. */
        unsigned pos; /* The position of the element against some string. */
        const char *text; /* The formatting text to be inserted. */
        struct felemt *next;
    };

    unsigned lastpos, paritydisk, rotation, unitsectors;
    struct felemt *curr, eol, faultc, faulto, parilc, parilo, parirc, pariro;
    /* ECMA-48 SGR terminal sequences. */
    char const *CTAG = "\x1B[0m", *FTAG = "\x1B[41m", *PTAG = "\x1B[33m";

    /* The number of sectors in the striping unit, a common derivative. */
    unitsectors = array->striping_unit / SECTOR;

    /* Rotation of the stripe. For RAID5, assuming (1) the left-symmetric
     * mapping, and (2) an ordinary single-level array configuration. */
    rotation = (array->level == RAID4) ? 0 : offset / (array->data_disks *
      array->striping_unit) % (array->data_disks + 1);

    /* Initialise characters for the parity disk. */
    memset(string + array->data_disks * (unitsectors + 1) + 1, ' ',
      unitsectors);

    /* We hide the last character from the function since rotating the
     * whole string would require subsequent manipulation to restore the
     * rightmost border. */
    rotatestringleft(string, stringlen - 1, rotation * (unitsectors + 1));

    /* The disk carrying the parity for the current stripe. */
    paritydisk = (array->data_disks - rotation) % (array->data_disks + 1);

    /* Opening and closing tag pairs for colouring the left and right
     * borders of the parity unit, respectively. */
    parilo.text = PTAG;
    parilo.pos = paritydisk * (unitsectors + 1);

    parilc.text = CTAG;
    parilc.pos = parilo.pos + 1;

    pariro.text = PTAG;
    pariro.pos = parilo.pos + unitsectors + 1;

    parirc.text = CTAG;
    parirc.pos = pariro.pos + 1;

    /* A long way of printing a newline character at the end. Expressed as
     * a formatting element in order to allow the formatting mechanism at
     * the bottom of the function to work. */
    eol.text = "\n";
    eol.pos = stringlen;

    /* Solid links. Inserting the optional faulty disk formatting will not
     * change these. */
    parilo.next = &parilc;
    pariro.next = &parirc;
    eol.next = NULL;

    /* Tentative links. If there is a faulty disk, one of these links will
     * be broken, depending on where the faulty disk is in relation to the
     * stripe's parity disk. */
    curr = &parilo; /* The tentative head of the list. */
    parilc.next = &pariro;
    parirc.next = &eol;

    if (array->faulty_disk != NO_DISK) {
        /* The array is not free from faults, and we need a little more
         * formatting to represent this. */

        /* An opening and closing tag pair for colouring the faulty disk. */
        faulto.text = FTAG;
        faulto.pos = array->faulty_disk * (unitsectors + 1) + 1;

        faultc.text = CTAG;
        faultc.pos = faulto.pos + unitsectors;

        faulto.next = &faultc; /* A solid link. */

        /* Insert the tags into the list of formatting elements we have
         * so far. */
        if (faulto.pos < parilo.pos) {
            /* The faulty disk precedes the stripe's parity disk. */
            faultc.next = curr;
            curr = &faulto; /* The list has a new head. */
        }
        else if (faulto.pos < pariro.pos) {
            /* The stripe's parity disk is faulty. */
            faultc.next = parilc.next;
            parilc.next = &faulto;
        }
        else {
            /* The faulty disk succeeds the stripe's parity disk. */
            faultc.next = parirc.next;
            parirc.next = &faulto;
        }
    }

    lastpos = 0;

    /* Gradually print the given string, stopping at various points to
     * insert formatting. */
    do {
        printf("%.*s", curr->pos - lastpos, string + lastpos);
        printf(curr->text);
        lastpos = curr->pos;
        curr = curr->next;
    } while (curr);
}

/* Physical mode prototype. */
static void printraidreqheaderphyref4(unsigned sreqs, struct
  stripe_request *srs)
{
    unsigned index, offset, pos, stops[3], stringlen, stripelen;
    char actionsymbol, symboltouse;
    char *string, *next;
    struct stripe_request *curr;

    /* One character for each sector in the stripe, including the parity
     * disk, one character before each disk to signal the start of a new
     * disk, and one character after the last disk for aesthetics. No
     * terminating \0 at the end. */
    stringlen = (array->data_disks + 1) * (array->striping_unit / SECTOR
      + 1) + 1;

    string = (char *) malloc(stringlen);

    /* The left and right borders for the last disk, respectively. */
    string[array->data_disks * (array->striping_unit / SECTOR + 1)] = '|';
    string[stringlen - 1] = '|';

    /* A symbol representing the action performed on the array. */
    actionsymbol = (rd_req->nature == WRITE_REQUEST) ? 'w' : 'r';

    /* The stripe length without the parity disk, a commonly-used
     * derivative. */
    stripelen = array->data_disks * array->striping_unit;

    curr = srs; /* We are at the first stripe request. */

    /* A stripe request conceptually divides a stripe into three zones,
     * the request area and two blocks of optional space around it.
     * Knowing where each zone stops helps us pick the right character to
     * print. We make zones explicit only for the first stripe request and
     * the final one, that is, where the sizes of the zones are not known
     * in advance. */
    stops[0] = curr->offset % stripelen;
    stops[1] = stops[0] + curr->length;
    stops[2] = stripelen;

    next = string;
    pos = 0;

    for (index = 0; index < 3; ++index) {
        symboltouse = (index == 1) ? actionsymbol : ' ';

        for (; pos < stops[index]; pos += SECTOR) {
            if (pos % array->striping_unit == 0) *next++ = '|';

            *next++ = symboltouse;
        }
    }

    colourandprintref2(string, stringlen, curr->offset);

    if (sreqs > 1) {
        /* Stripe requests second to penultimate, if any. */
        while (++curr < srs + sreqs - 1) {
            next = string;

            for (pos = 0; pos < stripelen; pos += SECTOR) {
                if (pos % array->striping_unit == 0) *next++ = '|';

                *next++ = actionsymbol;
            }

            colourandprintref2(string, stringlen, curr->offset);
        }

        /* We are now at the last stripe request.*/
        stops[1] = curr->length;

        next = string;
        pos = 0;

        /* Notice how index begins at 1 this time. For the final stripe
         * request, we know the middle zone begins at the start of the
         * stripe, that is, the first zone has zero length. */
        for (index = 1; index < 3; ++index) {
            symboltouse = (index == 1) ? actionsymbol : ' ';

            for (; pos < stops[index]; pos += SECTOR) {
                if (pos % array->striping_unit == 0) *next++ = '|';

                *next++ = symboltouse;
            }
        }

        colourandprintref2(string, stringlen, curr->offset);
    }

    free((void *) string);
}

/* Logical mode. */
static void printraidreqheaderlog(unsigned sreqs, struct stripe_request *srs)
{
    unsigned index, pos, stops[3], stripelen;
    char actionsymbol, symboltouse;

    /* A symbol representing the action performed on the array. */
    actionsymbol = (rd_req->nature == WRITE_REQUEST) ? 'w' : 'r';

    /* The stripe length without the parity disk, a commonly-used
     * derivative. */
    stripelen = array->data_disks * array->striping_unit;

    /* A stripe request conceptually divides a stripe into three zones,
     * the request area and two blocks of optional space around it.
     * Knowing where each zone stops helps us pick the right character to
     * print. We make zones explicit only for the first stripe request and
     * the final one, that is, where the sizes of the zones are not known
     * in advance. */
    stops[0] = srs[0].offset % stripelen;
    stops[1] = stops[0] + srs[0].length;
    stops[2] = stripelen;

    pos = 0;

    for (index = 0; index < 3; ++index) {
        symboltouse = (index == 1) ? actionsymbol : ' ';

        for (; pos < stops[index]; pos += SECTOR)
            printf((pos % array->striping_unit == 0) ? "|%c" : "%c",
              symboltouse);
    }

    puts("|");

    if (sreqs > 1) {
        /* Although we do not need a new set of stops just yet, we must
         * define them now, as sreqs is consumed shortly. */
        stops[1] = srs[sreqs - 1].length;

        sreqs -= 2;

        while (sreqs--) {
            for (pos = 0; pos < stripelen; pos += SECTOR)
                printf((pos % array->striping_unit == 0) ? "|%c" : "%c",
                  actionsymbol);

            puts("|");
        }

        pos = 0;

        /* For the final stripe request, we know the middle zone begins at
         * the start of the stripe, that is, the first zone has zero
         * length. */
        for (index = 1; index < 3; ++index) {
            symboltouse = (index == 1) ? actionsymbol : ' ';

            for (; pos < stops[index]; pos += SECTOR)
                printf((pos % array->striping_unit == 0) ? "|%c" : "%c",
                  symboltouse);
        }

        puts("|");
    }
}

static void printraidreqheader(unsigned sreqs, struct stripe_request *srs)
{
    printraidreqheaderphyref4(sreqs, srs);
    putchar('\n');
}

static void loadstripereq()
{
    unsigned raid_reqs, stripe_reqs;
    struct ext_raid_req *err, *errs;
    struct stripe_request *srs;

#ifdef FIXED_REQUESTS
    raid_reqs = req_list[REQ_LIST].reqs;
    errs = req_list[REQ_LIST].list;
#else
    raid_reqs = (*req_gen[REQ_GENERATOR])(&errs);
#endif

    err = errs;

    while (raid_reqs--) {
        array = &err->array;
        rd_req = &err->req;
        ++err;

        stripe_reqs = expandraidreq(&srs);

        printraidreqheader(stripe_reqs, srs);

        stripe = rd_req->offset / (array->data_disks * array->striping_unit);
        sr = srs;
        while (stripe_reqs--) {
            process_request();

            ++sr;
            ++stripe;
        }

        putchar('\n'); /* A new line before the next RAID req header. */

        free((void *) srs);
    }

#ifndef FIXED_REQUESTS
    free((void *) errs);
#endif
}

int main(int argc, char *argv[])
{
    /* The scope line formatting function to be used. */
    print_scope_line = print_scope_line_f1;

    loadstripereq();

    return 0;
}

/* vim: set cindent shiftwidth=4 expandtab: */
