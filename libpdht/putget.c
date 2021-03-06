/*******************************************************/
/*                                                      */
/*  putget.c - PDHT put / get operations                */
/*                                                      */
/*  author: d. brian larkins                            */
/*  created: 3/20/16                                    */
/*                                                      */
/********************************************************/

#include <alloca.h>
#include <pdht_impl.h>

/**
 * @file
 * 
 * portals distributed hash table put/get ops
 */

static int _pdht_flow_control_warning = 0;
extern int gstateflag;

// local-only discriminator for add/update/put operations
typedef enum { PdhtPTQPending, PdhtPTQActive } pdht_ptq_t;
static inline pdht_status_t pdht_do_put(pdht_t *dht, void *key, void *value, pdht_ptq_t which);
static void pdht_keystr(void *key, char* str);
static void pdht_dump_entry(pdht_t *dht, void *exp, void *act);

/**
 * pdht_put - puts or overwrites an entry in the global hash table
 *   @param key - hash table key
 *   @param ksize - size of key
 *   @param value - value for table entry
 *   @returns status of operation
 */
static inline pdht_status_t pdht_do_put(pdht_t *dht, void *key, void *value, pdht_ptq_t which) {
  ptl_match_bits_t mbits; 
  ptl_process_t rank;
  uint32_t ptindex;
  ptl_size_t loffset;
  ptl_size_t lsize;
  ptl_ct_event_t ctevent, current, reset;
  ptl_event_t fault;
  ptl_pt_index_t ptl_pt_index;
  int toobusy = 1, ret = 0, again = 0;
  ptl_me_t *mep;
  char *valp, *kval;
  pdht_status_t rval = PdhtStatusOK;
  struct timespec ts;

  PDHT_START_TIMER(dht, ptimer);

  // 1. hash key -> rank + match bits + element
  dht->hashfn(dht, key, &mbits, &ptindex, &rank);

  dht->stats.rankputs[rank.rank]++;

  // 1.5 figure out what we need to send to far end
  //   - send just HT element usually, need to also send
  //     match bits for triggered-only updates
  switch (dht->pmode) {
    case PdhtPendingTrig:

      // need to setup different ME start/lengths for pending/active puts
      if (which == PdhtPTQPending) {

        // XXX - THIS CODE IS HIGHLY DEPENDENDENT ON PORTALS ME DATA DEFINITION - XXX
        // to workaround, could issue two puts, one for match_bits, one for data
        //  trigger on +2 event counts
        mep = alloca(sizeof(ptl_me_t) + PDHT_MAXKEYSIZE + dht->elemsize); // no need to free later.
        valp = (char *)mep + sizeof(ptl_me_t);
        memcpy(valp, key, PDHT_MAXKEYSIZE); // ugh. copying. copy key into element.
        memcpy(valp + PDHT_MAXKEYSIZE, value, dht->elemsize); // double ugh. -- pointer math
        mep->match_bits = mbits;
        mep->ignore_bits = 0;
        mep->min_free = 0;

        loffset = (ptl_size_t)&mep->match_bits; // only send from match_bits field and beyond
        lsize = (sizeof(ptl_me_t) - offsetof(ptl_me_t, match_bits)) + PDHT_MAXKEYSIZE + dht->elemsize; 

      } else {
        // if putting to active queue, don't worry about match bits madness
        kval = alloca(PDHT_MAXKEYSIZE + dht->elemsize); // no need to free later.
        memcpy(kval,key,PDHT_MAXKEYSIZE);
        memcpy(kval + PDHT_MAXKEYSIZE,value,dht->elemsize);
        loffset = (ptl_size_t)kval;
        lsize = PDHT_MAXKEYSIZE + dht->elemsize;
      }
      break;

    case PdhtPendingPoll:
    default:
      kval = alloca(PDHT_MAXKEYSIZE + dht->elemsize); // no need to free later.
      memcpy(kval,key,PDHT_MAXKEYSIZE);
      memcpy(kval + PDHT_MAXKEYSIZE,value,dht->elemsize);
      loffset = (ptl_size_t)kval;
      lsize = PDHT_MAXKEYSIZE + dht->elemsize;
      //pdht_dprintf("put: key: %lu\n", *(u_int64_t *)kval);
      //pdht_dprintf("put: value: %f\n", *(double *)((char *)kval + PDHT_MAXKEYSIZE));
      break;
  }

  //#define PDHT_DEBUG_TRACE
#ifdef PDHT_DEBUG_TRACE
  pdht_dprintf("put: key: %lu val: %lu onto pending queue of %d\n", *(unsigned long *)key, *(unsigned long *)value, rank);
#endif  

  // find out which ME queue we're off too...
  if (which == PdhtPTQPending) {
    ptl_pt_index = dht->ptl.putindex[ptindex];  // put/add to pending
    dht->stats.pendputs++;
  } else {
    ptl_pt_index = dht->ptl.getindex[ptindex];  // update to active
  }

  // handle local updates
  if ((rank.rank == c->rank) && (which == PdhtPTQActive)) {
    ptl_me_t me;
    void *pt;

    me.start         = NULL;
    me.length        = PDHT_MAXKEYSIZE + dht->elemsize; // storing HT key & entry in each elem.
    me.ct_handle     = PTL_CT_NONE;
    me.uid           = PTL_UID_ANY;
    me.options       = PTL_ME_OP_GET | PTL_ME_IS_ACCESSIBLE | PTL_ME_EVENT_UNLINK_DISABLE | PTL_ME_EVENT_LINK_DISABLE;
    me.match_id.rank = PTL_RANK_ANY;
    me.match_bits    = mbits;
    me.ignore_bits   = 0;

    pthread_mutex_lock(&dht->local_gets_flag_mutex);
    dht->local_get_flag = 0;
    pthread_mutex_unlock(&dht->local_gets_flag_mutex);

    PtlMESearch(dht->ptl.lni, dht->ptl.getindex[ptindex],&me,PTL_ACTIVE_SEARCH_ONLY,&pt);

    pthread_mutex_lock(&dht->completion_mutex);
    if (dht->local_get_flag == 0){
      pdht_finalize_puts(dht);
    }
    pthread_mutex_unlock(&dht->completion_mutex);

    if (dht->local_get_flag == -1){
      dht->stats.notfound++;
      rval = PdhtStatusNotFound;
      goto done;
    }

    if (memcmp(pt, key, dht->keysize) != 0) {
      // keys don't match, this must be a collision
      dht->stats.collisions++;
      pdht_dprintf("pdht_put: found local collision. %d\n");
      //pdht_dump_entry(dht, key, buf);
      rval = PdhtStatusCollision;
      goto done;
    }
    memcpy(pt + PDHT_MAXKEYSIZE, value, dht->elemsize); // pointer math

    goto done;
  }

  PtlCTGet(dht->ptl.lmdct, &dht->ptl.curcounts);
  current = dht->ptl.curcounts;
  //pdht_dprintf("pdht_put: pre: success: %lu fail: %lu\n", dht->ptl.curcounts.success, dht->ptl.curcounts.failure);

  // may have to re-attempt put if we run into flow control, repeat until successful
  do { 

    toobusy = 0; // default is to only repeat once

    // put hash entry on target
    ret = PtlPut(dht->ptl.lmd, loffset, lsize, PTL_ACK_REQ, rank, ptl_pt_index,
        mbits, 0, value, 0);
    if (ret != PTL_OK) {
      pdht_dprintf("pdht_put: PtlPut(key: %lu, rank: %d, ptindex: %d) failed: %s\n",
          *(long *)key, rank.rank, dht->ptl.putindex[ptindex], pdht_ptl_error(ret));
      goto error;
    }


    // wait for local completion
    ret = PtlCTWait(dht->ptl.lmdct, current.success+1, &ctevent);
    if (ret != PTL_OK) {
      pdht_dprintf("pdht_put: PtlCTWait() failed\n");
      goto error;
    }


    //pdht_dprintf("pdht_put: post: success: %lu fail: %lu match: %llx which: %s rank: %d\n", 
    //      ctevent.success, ctevent.failure, mbits, which == PdhtPTQPending ? "pending" : "active", rank.rank);

    // check for errors
    if (ctevent.failure > current.failure) {
      ret = PtlEQWait(dht->ptl.lmdeq, &fault);

      if (ret == PTL_OK) {

        if (fault.ni_fail_type == PTL_NI_PT_DISABLED) {
          // flow control event generated only on initial drop
          if (!_pdht_flow_control_warning) {
            pdht_dprintf("pdht_put: flow control on remote rank: %d : %d\n", rank, dht->stats.puts);
            _pdht_flow_control_warning = 1;
          }
          PtlCTGet(dht->ptl.lmdct, &current);
          ts.tv_sec = 0;
          ts.tv_nsec = 10000000; // 10ms
          nanosleep(&ts, NULL);
          again = 1;
          toobusy = 1; // reset loop sentinel
          reset.success = 0;
          reset.failure = -1;
          PtlCTInc(dht->ptl.lmdct, reset); // reset failure count
        } else { // if (fault.ni_fail_type != PTL_NI_OK) {
          pdht_dprintf("pdht_put: found fail event: %s\n", pdht_event_to_string(fault.type));
          pdht_dump_event(&fault);
        }

        // vim auto indenting is dumb as shit.
        } else {
          pdht_dprintf("pdht_put: PtlEQWait() error: %s\n", pdht_ptl_error(ret));
        }
      }

    if (again && (ctevent.success == dht->ptl.curcounts.success)) {
        //pdht_dprintf("pdht_put: (again) flow control on remote rank: %d : %d\n", rank, dht->stats.puts);
        PtlCTGet(dht->ptl.lmdct, &current);
        //pdht_dprintf("pdht_put: post (again): success: %lu fail: %lu\n", current.success, current.failure);
        nanosleep(&ts, NULL);
        toobusy = 1; // reset loop sentinel
      }

    } while (toobusy);

done:
    PDHT_STOP_TIMER(dht, ptimer);
    return rval;

error:
    PDHT_STOP_TIMER(dht, ptimer);
    return PdhtStatusError;
  }



  /**
   * pdht_add - adds an entry in the global hash table
   *   @param key - hash table key
   *   @param ksize - size of key
   *   @param value - value for table entry
   *   @returns status of operation
   */
  pdht_status_t pdht_add(pdht_t *dht, void *key, void *value) {
    dht->stats.puts++;
    return pdht_do_put(dht,key,value, PdhtPTQPending);
  }



  /**
   * pdht_put - adds an entry to the global hash table
   *   @param key - hash table key
   *   @param ksize - size of key
   *   @param value - value for table entry
   *   @returns status of operation
   */
  pdht_status_t pdht_put(pdht_t *dht, void *key, void *value) {
    dht->stats.puts++;
    return pdht_do_put(dht,key,value,PdhtPTQPending);
  }



  /**
   * pdht_update - overwrites an entry in the global hash table
   *   @param key - hash table key
   *   @param ksize - size of key
   *   @param value - value for table entry
   *   @returns status of operation
   */
  pdht_status_t pdht_update(pdht_t *dht, void *key, void *value) {
    dht->stats.updates++;
    return pdht_do_put(dht,key,value,PdhtPTQActive);
  }


/**
 * pdht_get - gets an entry from the global hash table
 *   @param key - hash table key
 *   @param ksize - size of key
 *   @param value - value for table entry
 *   @returns status of operation
 */
pdht_status_t pdht_get(pdht_t *dht, void *key, void *value) {

  ptl_match_bits_t mbits; 
  unsigned long roffset = 0;
  uint32_t ptindex;
  ptl_ct_event_t ctevent;
  ptl_process_t rank;
  char buf[PDHT_MAXKEYSIZE + dht->elemsize];
  ptl_event_t ev;
  int ret;
  pdht_status_t rval = PdhtStatusOK;

  int check, c2 = -1;
  char *ptr;

  PDHT_START_TIMER(dht, gtimer);

  dht->stats.gets++;

  dht->hashfn(dht, key, &mbits, &ptindex, &rank);

  dht->stats.ptcounts[ptindex]++;

#ifdef PDHT_DEBUG_TRACE
  pdht_dprintf("pdht_get: key: %lu from active queue of %d with match: %lu\n", *(unsigned long *)key, rank, mbits);
#endif

  if ((rank.rank == c->rank) && (dht->local_get == PdhtSearchLocal) && 
      (dht->ptl.ptalloc_opts == PTL_PT_MATCH_UNORDERED)) {
    ptl_me_t me;
    char *index;
    ptl_event_t ev;
    int which;
    int ret; 
    //char pt[PDHT_MAXKEYSIZE + dht->elemsize];
    void *pt;

    _pdht_ht_entry_t *hte;
    index = (char *)dht->ht;

    me.match_bits = mbits;
    // XXX this is probably not needed and totally wrong with triggered updates
    hte = (_pdht_ht_entry_t *)index;

    // setup ME to append to active list
    me.start         = &hte->key; // see XXX above
    me.length        = PDHT_MAXKEYSIZE + dht->elemsize; // storing HT key in each elem.
    me.ct_handle     = PTL_CT_NONE;
    me.uid           = PTL_UID_ANY;
    // disable auto-unlink events, we just check for PUT completion
    me.options       = PTL_ME_OP_GET | PTL_ME_IS_ACCESSIBLE 
      | PTL_ME_EVENT_UNLINK_DISABLE | PTL_ME_EVENT_LINK_DISABLE;
    me.match_id.rank = PTL_RANK_ANY;
    me.match_bits    = mbits;
    me.ignore_bits   = 0;

    pthread_mutex_lock(&dht->local_gets_flag_mutex);
    dht->local_get_flag = 0;
    pthread_mutex_unlock(&dht->local_gets_flag_mutex);
    
    PtlMESearch(dht->ptl.lni, dht->ptl.getindex[ptindex],&me,PTL_ACTIVE_SEARCH_ONLY,&pt);
    
    pthread_mutex_lock(&dht->completion_mutex);

    if (dht->local_get_flag == 0){
      pdht_finalize_puts(dht);
    }
    pthread_mutex_unlock(&dht->completion_mutex);

    if (dht->local_get_flag == -1){
      dht->stats.notfound++;
      rval = PdhtStatusNotFound;
      goto done;
    }
    if (memcmp(pt, key, dht->keysize) != 0) {
      // keys don't match, this must be a collision
      dht->stats.collisions++;
      pdht_dprintf("pdht_get: found local collision. %d\n");
      pdht_dump_entry(dht, key, buf);
      rval = PdhtStatusCollision;
      PDHT_STOP_TIMER(dht,t4);
      goto done;
    }
    memcpy(value, pt + PDHT_MAXKEYSIZE, dht->elemsize); // pointer math

    goto done;

  } else {

    PtlCTGet(dht->ptl.lmdct, &dht->ptl.curcounts);
    if (dht->ptl.curcounts.failure > 0) {
      ctevent.success = 0;
      ctevent.failure = -dht->ptl.curcounts.failure;
      PtlCTInc(dht->ptl.lmdct, ctevent);
    }
    PtlCTGet(dht->ptl.lmdct, &dht->ptl.curcounts);
    //pdht_dprintf("pdht_get: pre: success: %lu fail: %lu\n", dht->ptl.curcounts.success, dht->ptl.curcounts.failure);

    ret = PtlGet(dht->ptl.lmd, (ptl_size_t)buf, PDHT_MAXKEYSIZE + dht->elemsize, rank, dht->ptl.getindex[ptindex], mbits, roffset, NULL);


    if (ret != PTL_OK) {
      //pdht_dprintf("pdht_get: PtlGet(key: %lu, rank: %d, ptindex: %d/%d) failed: (%s) : %d\n", *(long *)key, rank.rank, ptindex, dht->ptl.putindex[ptindex], pdht_ptl_error(ret), dht->stats.gets);
      goto error;
    }

#define RELIABLE_TARGETS
#ifdef RELIABLE_TARGETS
    // check for completion or failure
    ret = PtlCTWait(dht->ptl.lmdct, dht->ptl.curcounts.success+1, &ctevent);
    if (ret != PTL_OK) {
      pdht_dprintf("pdht_get: PtlCTWait() failed\n");
      goto error;
    }
#else
    ptl_size_t splusone = dht->ptl.curcounts.success+1;
    int which;
    ret = PtlCTPoll(&dht->ptl.lmdct, &splusone, 1, 3, &ctevent, &which);
    if (ret == PTL_CT_NONE_REACHED) {
      pdht_dprintf("pdht_get: timed out waiting for reply\n");
      dht->stats.notfound++;
      rval = PdhtStatusNotFound;
      goto done;
    } else if (ret != PTL_OK) {
      pdht_dprintf("pdht_get: PtlCTPoll() failed\n");
      goto error;
    }
#endif

    //pdht_dprintf("pdht_get: event counter: success: %lu failure: %lu\n", ctevent.success, ctevent.failure);
    if (ctevent.failure > dht->ptl.curcounts.failure) {
      ret = PtlEQWait(dht->ptl.lmdeq, &ev);
      if (ret == PTL_OK) {
        if (ev.type == PTL_EVENT_REPLY) {
#ifdef PDHT_DEBUG_TRACE
          pdht_dprintf("pdht_get: key: %lu not found\n", *(unsigned long *)key);
#endif  
          //pdht_dprintf("pdht_get: key: %lu not found\n", *(unsigned long *)key);

          ctevent.success = 0;
          ctevent.failure = -1;
          PtlCTInc(dht->ptl.lmdct, ctevent);
          dht->stats.notfound++;
          rval = PdhtStatusNotFound;
          goto done;
        } else if (ev.ni_fail_type != PTL_NI_OK) {
          pdht_dprintf("pdht_get: found fail event: %s\n", pdht_event_to_string(ev.type));   
          pdht_dump_event(&ev);
        } 
      } else {
        pdht_dprintf("pdht_get: PtlEQWait() failed\n");
        goto error;
      }
    }
  }
  // fetched entry has key + value concatenated, validate key

  if (memcmp(buf, key, dht->keysize) != 0) {
    // keys don't match, this must be a collision
    dht->stats.collisions++;
    pdht_dprintf("pdht_get: found collision.\n");
    pdht_dump_entry(dht, key, buf);
    rval = PdhtStatusCollision;
    PDHT_STOP_TIMER(dht,t4);
    goto done;
  }

  // looks good, copy value to application buffer
  // skipping over the embedded key data (for collision detection)
  memcpy(value, buf + PDHT_MAXKEYSIZE, dht->elemsize); // pointer math

done:
  // get of non-existent entry should hit fail counter + PTL_EVENT_REPLY event
  // in PTL_EVENT_REPLY event, we should get ni_fail_type
  // ni_fail_type should be: PTL_NI_DROPPED
  PDHT_STOP_TIMER(dht, gtimer);
  return rval;

error:
  PDHT_STOP_TIMER(dht, gtimer);
  return PdhtStatusError;
}


pdht_status_t pdht_persistent_get(pdht_t *dht, void *key, void *value){
    pdht_status_t ret;
    while(1){
      ret = pdht_get(dht, key, value);
      //printf("%d: persistent get ret : %d ok : %d \n", c->rank, ret, PdhtStatusOK);
      if (ret == PdhtStatusOK) return ret;
    }
}


/**
 * pdht_insert - manually inserts a hash table entry into the global hash table
 *  @param bits - Portals match bits for the table entry
 *  @param value - value for table entry
 *  @returns status of operation
 */
pdht_status_t pdht_insert(pdht_t *dht, ptl_match_bits_t bits, uint32_t ptindex, void *key, void *value) {
  char *index;
  _pdht_ht_entry_t *phte;
  _pdht_ht_trigentry_t *thte;
  ptl_me_t me;
  int ret;
  static int foo = 1;

  dht->stats.inserts++;

  // find our next spot 

  // iterator = ht[PTE * QSIZE] (i.e. PENDINGQ_SIZE per PTE)
  //index = (char *)dht->ht + ((dht->pendq_size * ptindex) * dht->entrysize);
  index = (char *)dht->ht;
  index += (dht->nextfree * dht->entrysize); // pointer math

  switch (dht->pmode) {
  case PdhtPendingPoll:
    phte = (_pdht_ht_entry_t *)index;
    assert(dht->nextfree == pdht_find_bucket(dht, phte));
    memcpy(&phte->key, key, PDHT_MAXKEYSIZE); 
    memcpy(&phte->data, value, dht->elemsize);
    me.start         = &phte->key;
    break;

  case PdhtPendingTrig:
    thte = (_pdht_ht_trigentry_t *)index;
    assert(dht->nextfree == pdht_find_bucket(dht, thte));
    memcpy(&thte->key, key, PDHT_MAXKEYSIZE); 
    memcpy(&thte->data, value, dht->elemsize);
    me.start         = &thte->key;
    break;
  }

  // setup ME to append to active list
  me.length        = PDHT_MAXKEYSIZE + dht->elemsize; // storing HT key _and_ HT entry in each elem.
  me.ct_handle     = PTL_CT_NONE;
  me.uid           = PTL_UID_ANY;
  // disable auto-unlink events, we just check for PUT completion
  me.options       = PTL_ME_OP_GET 
                   | PTL_ME_OP_PUT
                   | PTL_ME_IS_ACCESSIBLE 
                   | PTL_ME_EVENT_COMM_DISABLE
                   | PTL_ME_EVENT_LINK_DISABLE
                   | PTL_ME_EVENT_UNLINK_DISABLE;
  me.match_id.rank = PTL_RANK_ANY;
  me.match_bits    = bits;
  me.ignore_bits   = 0;


  //pdht_dprintf("inserting val: %lu on rank %d ptindex %d [%d] matchbits %lu\n", 
  //     *(unsigned long *)value, c->rank, ptindex, dht->ptl.getindex[ptindex], bits);

  if (dht->pmode == PdhtPendingPoll)
    ret = PtlMEAppend(dht->ptl.lni, dht->ptl.getindex[ptindex], &me, PTL_PRIORITY_LIST, phte, &phte->ame);
  else if (dht->pmode == PdhtPendingTrig)
    ret = PtlMEAppend(dht->ptl.lni, dht->ptl.getindex[ptindex], &me, PTL_PRIORITY_LIST, thte, &thte->ame);
  else 
    ret = PTL_FAIL;
  if (ret != PTL_OK) {
    pdht_dprintf("pdht_insert: ME append failed (active) : %s\n", pdht_ptl_error(ret));
    exit(1);
  }
  dht->nextfree++;

  return PdhtStatusOK;

error:
  return PdhtStatusError;
}


static void pdht_dump_entry(pdht_t *dht, void *exp, void *act) {
  char *cp;
  int ptindex;
  ptl_process_t rank;
  uint64_t ehash, ahash;

  printf("expected: ");
  cp = exp;
  for (int i=0; i<dht->keysize; i++)  printf("%2hhx ", cp[i]);
  printf("\nactual:   ");
  cp = act;
  for (int i=0; i<dht->keysize; i++)  printf("%2hhx ", cp[i]);
  printf("\n");
  dht->hashfn(dht, exp, &ehash, &ptindex, &rank);
  dht->hashfn(dht, act, &ahash, &ptindex, &rank);
  //printf("    hashes: exp: %llx act: %llx\n", ehash, ahash);
}

  static void pdht_keystr(void *key, char* str) {
    long *k = (long *)key;
    // sprintf(str, "<%ld,%ld,%ld> @ %ld", k[0], k[1], k[2], k[3]); // MADNESS
    sprintf(str, "%ld", k[0]); // tests
  }
