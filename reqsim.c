#include <assert.h>

#include "reqsim.h"

struct stripereq {
    unsigned offset; /* Absolute offset; a 64-bit value in practice. */
    unsigned len;    /* 32 bits should be adequate in practice. */
};

/* Offset and length matter all the way down to stripe units. The
 * properties need to be made explicit primarily because stripe requests
 * are free to begin and end anywhere in a stripe unit (but not anywhere
 * in a sector, which is assumed by the simulator). */
struct unitscope {
    unsigned offset;
    unsigned len;
};

/* Scope table: an experimental means of scalably recording stripe unit
 * scope. When scope is recorded for groups of stripe units, we need a
 * maximum of only 5 entries for any situation. */
struct scopetab {
    /* Scope of the first request unit. */
    struct unitscope req1;
    /* When there are two or more request units, scope of the final
     * request unit. Otherwise, req2.len must be 0. */
    struct unitscope req2;
    /* When there are three or more request units, scope of units between
     * the first request unit and the final one. Otherwise, req3.len must
     * be 0. */
    struct unitscope req3;
    /* Scope of off-request stripe units, that is, data units not part of
     * the request. */
    struct unitscope offreq;
    /* Scope of the parity unit. */
    struct unitscope parity;
};

/* A selection of jobs are built into the simulator. Some are defined
 * statically, while other are generated dynamically, that is, at
 * run-time. When DYNAMIC is defined, the simulator sources its jobs from
 * the generator indicated by JOBSRC. Otherwise, JOBSRC indicates a
 * particular job list.*/
#define DYNAMIC
#define JOBSRC 2

/* Built-in jobs are accessed through one of these. */
extern struct joblist jblist[];
extern unsigned (*jbgen[])(struct job **);

static struct dskarray *array; /* The current disk array. */
static struct raidreq *rreq;   /* The current RAID request. */
static struct stripereq *sreq; /* The current stripe request. */
/* A relative number indicating the first and final stripe units of the
 * request. */
static unsigned firstunit, finalunit;
/* The number of stripe units in the stripe request. Only data units are
 * counted. */
static unsigned requnits;
/* Physical stripe number. In single-level RAID configurations, the
 * physical number is the same as a stripe's logical number. */
static unsigned stripe;
/* The fault (flt) status (stat) of the current stripe (s). A value of
 * FLTFREE means the stripe is fault-free. Any other value indicates that
 * one the stripe's units falls on a faulty disk. If the value equals
 * PARITY_UNIT, then it is the parity unit. Otherwise, the the value is
 * the number of the unfortunate data unit.*/
static int fltstats;

#define PARITY_UNIT -2

/* This function maps disks to stripe units. For RAID4 arrays, it is an
 * identity function. For RAID5, left-symmetric placement is assumed. */
static unsigned disktounit(unsigned disk)
{
    return (array->lvl == RAID4) ? disk : (disk + stripe) %
      (array->datadsks + 1);
}

/* This function expands a RAID request to one or more stripe requests. */
static unsigned expandraidreq(struct stripereq **stripereqs)
{
    unsigned extlen, nextoffset, reqcount, stripelen;
    struct stripereq *req;

    /* We are interested in the stripe length without the parity unit, in
     * other words, the length of the logical stripe. */
    stripelen = array->datadsks * array->stripingunit;

    /* Extended length: RAID request length + the stripe-relative offset.
     * This limits alignment uncertainty to one end of the request without
     * affecting the number of stripe requests. */
    extlen = rreq->len + rreq->offset % stripelen;
    reqcount = extlen / stripelen;
    if (extlen - reqcount * stripelen) ++reqcount;

    req = *stripereqs = (struct stripereq *) malloc(reqcount * sizeof
      (struct stripereq));

    if (*stripereqs == NULL) {
        fprintf(stderr, "Could not get memory for expanding the RAID "
          "request.\n");
        exit(1);
    }

    /* The first stripe request. */
    req->offset = rreq->offset;
    req->len = (reqcount == 1) ? rreq->len : stripelen - rreq->offset %
      stripelen;

    if (reqcount > 1) {
        nextoffset = req->offset + req->len;

        /* Stripe requests between the first and the last, if there are
         * any. */
        for (++req; req < *stripereqs + reqcount - 1; ++req) {
            req->offset = nextoffset;
            req->len = stripelen;
            nextoffset += stripelen;
        }

        /* The last stripe request. */
        req->offset = nextoffset;
        req->len = rreq->len - (*stripereqs)->len - (reqcount - 2) *
          stripelen;
    }

    return reqcount;
}

/* This function fills a string according to the given unit scope. Sectors
 * of the stripe unit taking part in the request are represented by 'x' in
 * the scope string, and remaining sectors appear as ' '. */
static char *visualisescope(struct unitscope *scope, char *scopestr)
{
    unsigned sector, sectors;

    /* Clear the string first. */
    for (sector = 0; sector < array->stripingunit / SECTOR; ++sector)
        scopestr[sector] = ' ';

    /* Identify sectors in the scope. */
    sector = scope->offset / SECTOR;
    for (sectors = scope->len / SECTOR; sectors; --sectors)
        scopestr[sector++] = 'x';

    /* The string is echoed for a return value to permit visualisescope to
     * be called in a printf call, etc.. */ 
    return scopestr;
}

/* This function prints unit scopes for the same stripe request together
 * on one line separated by '|' characters and followed by the final
 * number of bytes required for each disk. The scopes follow disk, or
 * physical, order. */
static void printscopeline(struct scopetab *scopes)
{
    /* Scope string used as a source for the final scope line. Never
     * printed whole. In contrast to the final product, here stripe unit
     * scopes are kept in logical unit order. */
    char *scopestr;
    unsigned bytes;       /* All unit scope lengths together, in bytes. */
    unsigned disk;        /* Which disk. */
    unsigned strlen;      /* Length of the scope string. */
    unsigned unit;        /* Stripe unit. */
    unsigned unitsectors; /* Sectors in the striping unit. */

    unitsectors = array->stripingunit / SECTOR;

    /* No need for a null character at the end, as scopestr will never
     * be printed whole. */
    strlen = (array->datadsks + 1) * unitsectors;
    if ((scopestr = (char *) malloc(strlen)) == NULL) {
        fprintf(stderr, "Could not get memory for printing the scope "
          "line.\n");
        exit(2);
    }
    memset(scopestr, ' ', strlen);

    bytes = 0;

    if (scopes->req1.len) {
        visualisescope(&scopes->req1, scopestr + firstunit * unitsectors);

        bytes += scopes->req1.len;
    }

    if (scopes->req3.len)
        for (unit = firstunit + 1; unit < finalunit; ++unit) {
            /* Reconstruct-read is the only request service method which
             * may give a group scope (req3) even as one of the member
             * units is faulty. */
            if (unit == fltstats) continue;

            visualisescope(&scopes->req3, scopestr + unit * unitsectors);

            bytes += scopes->req3.len;
        }

    if (scopes->req2.len) {
        visualisescope(&scopes->req2, scopestr + finalunit * unitsectors);

        bytes += scopes->req2.len;
    }

    if (scopes->offreq.len) {
        for (unit = 0; unit < firstunit; ++unit)
            visualisescope(&scopes->offreq, scopestr + unit *
              unitsectors);

        for (unit = finalunit + 1; unit < array->datadsks; ++unit)
            visualisescope(&scopes->offreq, scopestr + unit *
              unitsectors);

        bytes += (array->datadsks - requnits) * scopes->offreq.len;
    }

    if (scopes->parity.len) {
        visualisescope(&scopes->parity, scopestr + array->datadsks *
          unitsectors);

        bytes += scopes->parity.len;
    }

    /* The final scope line is constructed by sourcing scope information
     * from scopestr and dynamically inserting '|' . */
    for (disk = 0; disk <= array->datadsks; ++disk) {
        unit = disktounit(disk);
        printf("|%.*s", unitsectors, scopestr + unit * unitsectors);
    }

    printf("| %d bytes\n", bytes);

    free((void *) scopestr);
}

/* Nonredundant-write stripe request service method. */
static void nwmethod(struct scopetab *inscopes)
{
    struct scopetab outscopes = {{0, 0}};

    printscopeline(&outscopes);
}

/* Read-modify-write stripe request service method. The purpose of the
 * andprint parameter is to suppress printing when only the function's
 * return value is desired, namely when verifying read-modify-write is
 * indeed more efficient than reconstruct-write in cases where the fomrer
 * is considered the primary choice. This is needed in order to develop
 * trust in the rmw-rw cut-off confition. See processwrite. */
static unsigned rmwmethod(struct scopetab *inscopes, int andprint)
{
    struct scopetab outscopes = *inscopes;

    if (requnits == 1)
        outscopes.parity = outscopes.req1;
    else {
        outscopes.parity.offset = 0;
        outscopes.parity.len = array->stripingunit;
    }

    if (andprint) printscopeline(&outscopes);

    return outscopes.req1.len +
      outscopes.req2.len +
      outscopes.req3.len * (requnits - 2) +
      outscopes.parity.len;
}

/* Reconstruct-write stripe request service method. See the comment in
 * front of rmwmethod. */
static unsigned rwmethod(struct scopetab *inscopes, int andprint)
{
    struct scopetab outscopes = {{0, 0}};

    if (requnits == 1)
        /* For XOR to work, there need to be at least two data disks. With
         * one request unit, we can be sure there is at least one unit
         * left off request. */
        outscopes.offreq = inscopes->req1;
    else {
        if (inscopes->req1.len < array->stripingunit) 
            /* Read the first unit's scope complement. */
            /* The default offset (0) is adequate. */
            outscopes.req1.len = inscopes->req1.offset;
        if (inscopes->req2.len < array->stripingunit) {
            /* Read the final unit's scope complement. */
            outscopes.req2.offset = inscopes->req2.len;
            outscopes.req2.len = array->stripingunit - inscopes->req2.len;
        }
        /* With more than one request unit, we cannot be sure there are
         * any units left off request. */
        if (requnits < array->datadsks)
            /* The default offset (0) is adequate. */
            outscopes.offreq.len = array->stripingunit;
    }

    if (andprint) printscopeline(&outscopes);

    return outscopes.req1.len +
      outscopes.req2.len +
      outscopes.offreq.len * (array->datadsks - requnits);
}

/* Reconstruct-write-plus stripe request service method. Unlike the other
 * methods, this one is original. It is based on a mathematically-derived
 * parity function. Optimal over read-modify-write and reconstruct-write
 * in certain cases (see the condition for the rw+ branch in
 * processwrite). */
static void rwplusmethod(struct scopetab *inscopes)
{
    struct scopetab outscopes = {{0, 0}};
    struct unitscope complement; /* Faulty unit scope complement. */

    if (fltstats == firstunit) {
        complement.offset = 0;
        complement.len = inscopes->req1.offset;
        if (inscopes->req2.len == array->stripingunit)
            outscopes.req2 = complement;
        else
            /* The default offset (0) is adequate. */
            outscopes.req2.len = array->stripingunit;
        if (requnits < array->datadsks)
            outscopes.offreq = inscopes->req1;
    }
    else
    {
        complement.offset = inscopes->req2.len;
        complement.len = array->stripingunit - complement.offset;
        if (inscopes->req1.len == array->stripingunit)
            outscopes.req1 = complement;
        else
            /* The default offset (0) is adequate. */
            outscopes.req1.len = array->stripingunit;
        if (requnits < array->datadsks)
            outscopes.offreq = inscopes->req2;
    }
    if (requnits > 2)
        outscopes.req3 = complement;
    outscopes.parity = complement;

    printscopeline(&outscopes);
}

/* This function chooses the appropriate stripe request service method for
 * writes. */
static void processwrite(struct scopetab *inscopes)
{
    unsigned primary, secondary;

    if (fltstats == FLTFREE) {
        /* The rmw-rw cut-off condition referred to in various places. It
         * is a request lengh mark where read-modify-write becomes more
         * efficient than reconstruct-write or vice versa. requnits != 1
         * does not represent any extra knowledge. It is added only so we
         * can have two branches instead of four. */
        if (requnits == 1 && array->datadsks > 3 || requnits != 1 &&
          array->stripingunit * (array->datadsks - 1) > 2 * sreq->len) {
            primary = rmwmethod(inscopes, 1);
            secondary = rwmethod(inscopes, 0);
        }
        else {
            secondary = rmwmethod(inscopes, 0);
            primary = rwmethod(inscopes, 1);
        }
        assert(primary <= secondary);
    }
    else if (fltstats == PARITY_UNIT)
        nwmethod(inscopes);
    /* If an off-request unit is faulty. */
    else if (fltstats < firstunit || fltstats > finalunit)
        rmwmethod(inscopes, 1);
    /* If there is one request unit, or the faulty unit changes
     * completely. */
    else if (requnits == 1 || fltstats > firstunit && fltstats < finalunit
      || fltstats == firstunit && inscopes->req1.len ==
      array->stripingunit || fltstats == finalunit && inscopes->req2.len
      == array->stripingunit)
        rwmethod(inscopes, 1);
    else
        /* There are two or more request units, and the faulty unit
         * changes partially. */
        rwplusmethod(inscopes);
}

/* Direct-read stripe request service method. Old method, new name. */
static void drmethod(struct scopetab *inscopes)
{
    printscopeline(inscopes);
}

/* Reconstruct-read stripe request service method. */
static void rrmethod(struct scopetab *inscopes)
{
    struct scopetab outscopes = {{0, 0}};
    struct unitscope fltscope; /* Faulty unit scope. */

    if (requnits > 1) {
        if (fltstats != firstunit) {
            outscopes.req1.offset = 0;
            outscopes.req1.len = array->stripingunit;
        }
        if (requnits > 2 && (fltstats == firstunit || fltstats ==
          finalunit) || requnits > 3) {
            outscopes.req3.offset = 0;
            outscopes.req3.len = array->stripingunit;
        }
        if (fltstats != finalunit) {
            outscopes.req2.offset = 0;
            outscopes.req2.len = array->stripingunit;
        }
    }

    if (fltstats == firstunit)
        fltscope = inscopes->req1;
    else if (fltstats < finalunit)
        fltscope = inscopes->req3;
    else
        fltscope = inscopes->req2;

    if (requnits < array->datadsks)
        outscopes.offreq = fltscope;

    outscopes.parity = fltscope;

    printscopeline(&outscopes);
}

/* This function chooses the appropriate stripe request service method for
 * reads. */
static void processread(struct scopetab *inscopes)
{
    if (fltstats >= firstunit && fltstats <= finalunit)
        /* A request unit is faulty. */
        rrmethod(inscopes);
    else
        /* The stripe is fault-free, or either the parity unit or an
         * off-request unit is faulty. */
        drmethod(inscopes);
}

/* This function does preliminary processing of the current stripe
 * request, then calls one of two request nature-specific functions to
 * continue the work. In particular, processreq figures out the number of
 * request units as well as the first and final request units, translates
 * a single offset-length pair into a bunch of scopes, and finally sets up
 * the stripe fault status. */
static void processreq()
{
    /* Extended length concept as in expandraidreq only here for a stripe
     * request rather than a RAID one. */
    unsigned extlen;
    /* Request offset relative to the stripe (s) and the first request
     * unit (u), respectively. */
    unsigned sreloffset;
    unsigned ureloffset;
    struct scopetab inscopes = {{0, 0}};

    sreloffset = sreq->offset - stripe * (array->datadsks *
      array->stripingunit);
    firstunit = sreloffset / array->stripingunit;
    ureloffset = sreloffset - firstunit * array->stripingunit;

    extlen = sreq->len + ureloffset;
    requnits = extlen / array->stripingunit;
    if (extlen - requnits * array->stripingunit) ++requnits;

    finalunit = firstunit + requnits - 1;

    inscopes.req1.offset = ureloffset;
    inscopes.req1.len = (requnits == 1) ? sreq->len : array->stripingunit
      - inscopes.req1.offset;

    if (requnits > 1) {
        /* The default offset (0) is adequate. */
        inscopes.req2.len = sreq->len - inscopes.req1.len - (requnits - 2)
          * array->stripingunit;

        if (requnits > 2)
            /* The default offset (0) is adequate. */
            inscopes.req3.len = array->stripingunit;
    }

    if (array->fltstata == FLTFREE)
        fltstats = FLTFREE;
    else {
        fltstats = disktounit(array->fltstata);
        if (fltstats == array->datadsks) fltstats = PARITY_UNIT;
    }

    if (rreq->nature == WRITEREQ)
        processwrite(&inscopes);
    else
        processread(&inscopes);
}

/* This function rotates a string of length len left by units positions.
 * len may be less than the actual length of str, in which case a
 * substring is rotated. The function treats a terminating \0 like any
 * other character, so expect its position to change. You can emulate
 * preserving \0 by making sure len is precisely one less than the actual
 * length of the string. */
static void rotatestringleft(char *str, unsigned len, unsigned units)
{
    char *temp;

    if (units) {
        if ((temp = (char *) malloc(units)) == NULL) {
            fprintf(stderr, "Could not get memory for string rotation.\n"
              );
            exit(3);
        }

        memcpy(temp, str, units);

        memmove(str, str + units, len - units);

        memcpy(str + len - units, temp, units);

        free((void *) temp);
    }
}

/* This function takes care of formatting common to every stripe request
 * line in the job header. When the line is properly rotated, the function
 * prints the result, colouring the parity disk and possibly the faulty
 * disk. */
static void colourandprint(char *str, unsigned strlen, unsigned
  offset)
{
    /* Formatting element text to be inserted at pos of some string. */
    struct felemt {
        unsigned pos;
        const char *text;
        struct felemt *next;
    };

    unsigned lastpos, paritydisk, rotation, unitsectors;
    struct felemt *curr, eol;
    /* Formatting elements for emphasising the stripe's parity disk
     * (pari), and possibly a faulty disk (fault). Only parity formatting
     * has separate elements for left (l) and right (r), but both tasks
     * involve opening (o) and closing (c) tags. */
    struct felemt faultc, faulto, parilc, parilo, parirc, pariro;
    /* ECMA-48 SGR terminal sequences which will be used to achieve the
     * desired colouring. */
    char const *CTAG = "\x1B[0m", *FTAG = "\x1B[41m", *PTAG = "\x1B[33m";

    /* The number of sectors in the striping unit, a common derivative. */
    unitsectors = array->stripingunit / SECTOR;

    /* Rotation of the stripe. For RAID5, assuming (1) the left-symmetric
     * mapping, and (2) an ordinary single-level array configuration. */
    rotation = (array->lvl == RAID4) ? 0 : offset / (array->datadsks *
      array->stripingunit) % (array->datadsks + 1);

    /* Initialise characters for the parity disk. */
    memset(str + array->datadsks * (unitsectors + 1) + 1, ' ',
      unitsectors);

    /* We hide the last character from the function since rotating the
     * whole string would require subsequent manipulation to restore the
     * rightmost border. */
    rotatestringleft(str, strlen - 1, rotation * (unitsectors + 1));

    /* The disk carrying the parity for the current stripe. */
    paritydisk = (array->datadsks - rotation) % (array->datadsks + 1);

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
    eol.pos = strlen;

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

    if (array->fltstata != FLTFREE) {
        /* The array is not free from faults, and we need a little more
         * formatting to represent this. */

        /* An opening and closing tag pair for colouring the faulty disk. */
        faulto.text = FTAG;
        faulto.pos = array->fltstata * (unitsectors + 1) + 1;

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
        printf("%.*s", curr->pos - lastpos, str + lastpos);
        printf(curr->text);
        lastpos = curr->pos;
        curr = curr->next;
    } while (curr);
}

/* This function prints the simulation job header, which displays the RAID
 * request laid out over a series of stripes in the disk array. You can
 * see the array's configuration, and there is highlighting for each
 * stripe's parity as well as the optional faulty disk in the array.
 * Stripe units (the columns) follow disk order. */
static void printjobheader(unsigned reqcount, struct stripereq *reqs)
{
    unsigned index, offset, pos, stops[3], stripelen, strlen;
    char actionsymbol, symboltouse;
    char *next, *str;
    struct stripereq *curr;

    /* One character for each sector in the stripe, including the parity
     * disk, one character before each disk to signal the start of a new
     * disk, and one character after the last disk for aesthetics. No
     * terminating \0 at the end. */
    strlen = (array->datadsks + 1) * (array->stripingunit / SECTOR + 1)
      + 1;

    if ((str = (char *) malloc(strlen)) == NULL) {
        fprintf(stderr, "Could not get memory for the job header source "
          "string.\n");
        exit(4);
    }

    /* The left and right borders for the last disk, respectively. */
    str[array->datadsks * (array->stripingunit / SECTOR + 1)] = '|';
    str[strlen - 1] = '|';

    /* A symbol representing the action performed on the array. */
    actionsymbol = (rreq->nature == WRITEREQ) ? 'w' : 'r';

    /* The stripe length without the parity disk, a commonly-used
     * derivative. */
    stripelen = array->datadsks * array->stripingunit;

    curr = reqs; /* We are at the first stripe request. */

    /* A stripe request conceptually divides a stripe into three zones,
     * the request area and two blocks of optional space around it.
     * Knowing where each zone stops helps us pick the right character to
     * print. We make zones explicit only for the first stripe request and
     * the final one, that is, where the sizes of the zones are not known
     * in advance. */
    stops[0] = curr->offset % stripelen;
    stops[1] = stops[0] + curr->len;
    stops[2] = stripelen;

    next = str;
    pos = 0;

    for (index = 0; index < 3; ++index) {
        symboltouse = (index == 1) ? actionsymbol : ' ';

        for (; pos < stops[index]; pos += SECTOR) {
            if (pos % array->stripingunit == 0) *next++ = '|';

            *next++ = symboltouse;
        }
    }

    colourandprint(str, strlen, curr->offset);

    if (reqcount > 1) {
        /* Stripe requests second to penultimate, if any. */
        while (++curr < reqs + reqcount - 1) {
            next = str;

            for (pos = 0; pos < stripelen; pos += SECTOR) {
                if (pos % array->stripingunit == 0) *next++ = '|';

                *next++ = actionsymbol;
            }

            colourandprint(str, strlen, curr->offset);
        }

        /* We are now at the last stripe request.*/
        stops[1] = curr->len;

        next = str;
        pos = 0;

        /* Notice how index begins at 1 this time. For the final stripe
         * request, we know the middle zone begins at the start of the
         * stripe, that is, the first zone has zero length. */
        for (index = 1; index < 3; ++index) {
            symboltouse = (index == 1) ? actionsymbol : ' ';

            for (; pos < stops[index]; pos += SECTOR) {
                if (pos % array->stripingunit == 0) *next++ = '|';

                *next++ = symboltouse;
            }
        }

        colourandprint(str, strlen, curr->offset);
    }

    free((void *) str);
}

/* This function obtains one or more jobs from our built-in selection,
 * then for each one sets the current array and RAID request, and expands
 * the latter into one or more stripe requests, which are then run through
 * the simulator. */
static void loadstripereq()
{
    unsigned jbcount, sreqcount;
    struct job *jb, *jobs;
    struct stripereq *sreqs;

#ifdef DYNAMIC
    jbcount = (*jbgen[JOBSRC])(&jobs);
#else
    jbcount = jblist[JOBSRC].jbcount;
    jobs = jblist[JOBSRC].list;
#endif

    jb = jobs;

    while (jbcount--) {
        array = &jb->array;
        rreq = &jb->req;
        ++jb;

        sreqcount = expandraidreq(&sreqs);

        printjobheader(sreqcount, sreqs);
        putchar('\n');

        stripe = rreq->offset / (array->datadsks * array->stripingunit);
        sreq = sreqs;
        while (sreqcount--) {
            processreq();

            ++sreq;
            ++stripe;
        }

        putchar('\n'); /* A new line before the next header. */

        free((void *) sreqs);
    }

#ifdef DYNAMIC 
    free((void *) jobs);
#endif
}

int main(int argc, char *argv[])
{
    loadstripereq();

    return 0;
}

/* vim: set cindent shiftwidth=4 expandtab: */
