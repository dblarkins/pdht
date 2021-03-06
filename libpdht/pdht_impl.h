/***********************************************************/
/*                                                         */
/*  pdht_impl.h - PDHT private implementation protos/ADTs */
/*                                                         */
/*  author: d. brian larkins                               */
/*  created: 3/20/16                                       */
/*                                                         */
/***********************************************************/

#pragma once
#define _XOPEN_SOURCE 700

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <time.h>
#include <pdht.h>

#define PDHT_DEBUG
//#define PDHT_DEBUG_TRACE
#define PDHT_DEBUG_NONE     0
#define PDHT_DEBUG_WARN     1
#define PDHT_DEBUG_NAG      2
#define PDHT_DEBUG_VERBOSE  3

#ifdef PDHT_DEBUG
  #define pdht_dprintf(...) pdht_dbg_printf(__VA_ARGS__)
  #define pdht_lprintf(lvl, ...) pdht_lvl_dbg_printf(lvl, __VA_ARGS__)
  #define pdht_eprintf(lvl, ...) pdht_lvl_dbg_eprintf(lvl, __VA_ARGS__)
#else
  #define pdht_dprintf(...) ;
  #define pdht_lprintf(...) ;
  #define pdht_eprintf(...) ;
#endif

// default table size should be bigger than 2x pending queue size
#define PDHT_DEFAULT_TABLE_SIZE 100000
#define PDHT_PENDINGQ_SIZE      20000

#define PDHT_DEFAULT_NUM_PTES  1
#define PDHT_COUNT_PTES 1
#define PDHT_COMPLETION_CTS 1
#define PDHT_COLLECTIVE_PTES 3 // barrier, mutex, termination
#define PDHT_COLLECTIVE_CTS 4  // 
#define PDHT_ATOMIC_CTS PDHT_MAX_TABLES

#define PDHT_MAXKEYSIZE 32 // size in bytes (32 for MADNESS)

//#define PDHT_PTALLOC_OPTIONS 0
#define PDHT_PTALLOC_OPTIONS PTL_PT_MATCH_UNORDERED

#define PDHT_DEFAULT_QUIET 0

#define __PDHT_ACTIVE_INDEX 2
#define __PDHT_PENDING_INDEX __PDHT_ACTIVE_INDEX + PDHT_MAX_PTES
#define __PDHT_PENDING_MATCH 0xcafef00d


#define __PDHT_COLLECTIVE_INDEX 0
#define __PDHT_BARRIER_MATCH  0xdeadbeef
#define __PDHT_REDUCE_LMATCH  0xfa570911
#define __PDHT_REDUCE_RMATCH  0xfa570991
#define __PDHT_BCAST_MATCH    0xffffffff
#define __PDHT_COUNTER_INDEX 1
#define __PDHT_COUNTER_MATCH 0xdeadbeef

#define offsetof(type, member)  __builtin_offsetof (type, member)

extern pdht_config_t   *__pdht_config; 

/**
 * @file
 * 
 * portals distributed hash table implementations ADTs
 */

// overlay struct for casting
struct _pdht_ht_entry_s {
   ptl_handle_me_t   pme;  // pending ME handle
   ptl_handle_me_t   ame;  // active ME handle
   char              key[PDHT_MAXKEYSIZE]; // fixed data size for key right now.
   char              data[0]; // opaque payload
};
typedef struct _pdht_ht_entry_s _pdht_ht_entry_t;

// overlay struct for casting
struct _pdht_ht_trigentry_s {
   ptl_handle_me_t   pme;  // pending ME handle
   ptl_handle_me_t   ame;  // active ME handle
   ptl_handle_ct_t   tct;  // trigger counter for each entry
   ptl_me_t          me;   // ME buffer for copying match bits over
   char              key[PDHT_MAXKEYSIZE]; // fixed data size for key right now.
   char              data[0]; // opaque payload
};
typedef struct _pdht_ht_trigentry_s _pdht_ht_trigentry_t;


/********************************************************/
/* portals distributed hash table prototypes            */
/********************************************************/

// Initialization / Finalization -- init.c
void                 pdht_init(pdht_config_t *cfg);
void                 pdht_fini(void);
void                 pdht_clearall(void);

// commsynch.c
void                 pdht_collective_init(pdht_context_t *c);
void                 pdht_collective_fini();
pdht_status_t        pdht_finalize_puts(pdht_t *dht);

// pmi.c
void init_pmi(pdht_config_t *cfg);
void init_only_barrier(void);

// util.c
int  eprintf(const char *format, ...);
int  pdht_dbg_printf(const char *format, ...);
int  pdht_lvl_dbg_printf(int lvl, const char *format, ...);
int  pdht_lvl_dbg_eprintf(int lvl, const char *format, ...);
char *pdht_ptl_error(int error_code);
char *pdht_event_to_string(ptl_event_kind_t evtype);
void pdht_dump_event(ptl_event_t *ev);


// hash.c - PDHT hash function operations
void pdht_hash(pdht_t *dht, void *key, ptl_match_bits_t *bits, uint32_t *ptindex, ptl_process_t *rank);

// poll.c - PDHT polling tasks
void pdht_polling_init(pdht_t *dht);
void pdht_polling_fini(pdht_t *dht);
void *pdht_poll(void *arg);

// trig.c - PDHT triggered tasks
void pdht_trig_init(pdht_t *dht);
void pdht_trig_fini(pdht_t *dht);
