/********************************************************/
/*                                                      */
/*  poll.c - PDHT polling thread operations             */
/*                                                      */
/*  author: d. brian larkins                            */
/*  created: 3/20/16                                    */
/*                                                      */
/********************************************************/

#include <time.h>

#include <pdht_impl.h>

/**
 * @file
 * 
 * portals distributed hash table polling task code
 */

static pthread_t _pdht_poll_tid;

/**
 * pdht_polling_init -- initializes polling thread
 * @param dht - hash table data structure
 */
void pdht_polling_init(pdht_t *dht) {
  int ret;
  _pdht_ht_entry_t *hte;
  char *iter; // used for pointer math
  ptl_me_t me;
  int pentries = 0;

  // default match-list entry values
  me.length      = PDHT_MAXKEYSIZE + dht->elemsize; // storing key _and_ value for each entry
  me.ct_handle   = PTL_CT_NONE;
  me.uid         = PTL_UID_ANY;
  // disable AUTO unlink events, we just check for PUT completion
  me.options     = PTL_ME_OP_PUT 
                 | PTL_ME_USE_ONCE 
                 | PTL_ME_IS_ACCESSIBLE 
                 | PTL_ME_EVENT_UNLINK_DISABLE 
                 | PTL_ME_EVENT_LINK_DISABLE;
  me.match_id.rank = PTL_RANK_ANY;
  me.match_bits  = __PDHT_PENDING_MATCH; // this is ignored, each one of these is a wildcard
  me.ignore_bits = 0xffffffffffffffff; // ignore it all

  // deal with multiple PTEs per hash table to handle match list length
  for (int ptindex = 0; ptindex < dht->ptl.nptes; ptindex++) {

    // allocate event queue for pending puts
    ret = PtlEQAlloc(dht->ptl.lni, dht->pendq_size, &dht->ptl.eq[ptindex]);
    if (ret != PTL_OK) {
      pdht_dprintf("pdht_polling_init: PtlEQAlloc failure\n");
      exit(1);
    }

    // allocate PTE for pending put
    ret = PtlPTAlloc(dht->ptl.lni, PTL_PT_ONLY_USE_ONCE | PTL_PT_FLOWCTRL,
        dht->ptl.eq[ptindex], dht->ptl.putindex_base+ptindex, &dht->ptl.putindex[ptindex]);
    if (ret != PTL_OK) {
      pdht_dprintf("pdht_polling_init: PtlPTAlloc failure [%d] : %s\n", ptindex, pdht_ptl_error(ret));
      exit(1);
    }
    //pdht_dprintf("%d: %d %d\n", ptindex, dht->ptl.putindex_base+ptindex, dht->ptl.putindex[ptindex]);

    // iterator = ht[PTE * QSIZE] (i.e. PENDINGQ_SIZE per PTE)
    iter = (char *)dht->ht + ((dht->pendq_size * ptindex) * dht->entrysize);  // pointer math
    //pdht_dprintf("init append: ptindex: %d %d ht[%d] userp: %p\n", ptindex, dht->ptl.putindex[ptindex], pdht_find_bucket(dht, iter), iter);

    // append one-time match entres to the put PTE to catch incoming puts
    for (int i=0; i < dht->pendq_size; i++) {
      hte = (_pdht_ht_entry_t *)iter;
      assert(hte->pme == PTL_INVALID_HANDLE);
      assert(hte->ame == PTL_INVALID_HANDLE);
      me.start  = &hte->key; // each entry has a unique memory buffer (starts with key)

      //pdht_dprintf("init append: %d %d userp: %p\n", i, pdht_find_bucket(dht, hte), hte);
      ret = PtlMEAppend(dht->ptl.lni, dht->ptl.putindex[ptindex], &me, PTL_PRIORITY_LIST, hte, &hte->pme);
      if (ret != PTL_OK) {
        pdht_dprintf("pdht_polling_init: [%d/%d]:ht[%d] PTE: %d PtlMEAppend error: %s\n", ptindex, i, pdht_find_bucket(dht,iter),dht->ptl.putindex[ptindex], pdht_ptl_error(ret));
        pdht_dprintf("start %p len: %lu %d %d %8x %8x %8x\n", me.start, me.length, (me.ct_handle==PTL_CT_NONE), 
            (me.uid==PTL_UID_ANY), me.options, me.match_bits, me.ignore_bits);
        exit(1);
      } 
      pentries++;
      // clean out the LINK events from the event queue
      // PtlEQWait(dht->ptl.eq, &ev);

      iter += dht->entrysize; // pointer math, danger.
    }
  }

  dht->nextfree = dht->ptl.nptes * dht->pendq_size; // free = DEFAULT_TABLE_SIZE - PENDINGQ_SIZE

  pthread_create(&_pdht_poll_tid, NULL, pdht_poll, dht);
  eprintf("XXX progress threads need fixed in polling model -- won't work with multiple dhts\n");  // admit defeat
}



/**
 * pdht_polling_fini - cleans up polling structures
 * @param dht - hash table data structure
 */
void pdht_polling_fini(pdht_t *dht) {
  struct timespec ts;
  _pdht_ht_entry_t *hte;
  char *iter;
  int ret;

  // disable any new messages arriving on the portals table put entry
  for (int ptindex=0; ptindex < dht->ptl.nptes; ptindex++)
    PtlPTDisable(dht->ptl.lni, dht->ptl.putindex[ptindex]);

  // wait for polling progress thread to exit
  pthread_join(_pdht_poll_tid, NULL);

  // kill off event queues
  for (int ptindex=0; ptindex < dht->ptl.nptes; ptindex++) {
    if (!PtlHandleIsEqual(dht->ptl.eq[ptindex], PTL_EQ_NONE)) {
      PtlEQFree(dht->ptl.eq[ptindex]);
    }
  }

  // remove all match entries from the table
  iter = (char *)dht->ht;

  for (int i=0; i<dht->maxentries; i++) {
    hte = (_pdht_ht_entry_t *)iter;

    // pending/put ME entries
    if (!PtlHandleIsEqual(hte->pme, PTL_INVALID_HANDLE)) {
      ret = PtlMEUnlink(hte->pme);
      while (ret == PTL_IN_USE) {
        ts.tv_sec = 0;
        ts.tv_nsec = 20000;  // 20ms
        nanosleep(&ts, NULL);
        ret = PtlMEUnlink(hte->pme);
      }
    }

    // active/get ME entries
    if (!PtlHandleIsEqual(hte->ame, PTL_INVALID_HANDLE)) {
      ret = PtlMEUnlink(hte->ame);
      while (ret == PTL_IN_USE) {
        ts.tv_sec = 0;
        ts.tv_nsec = 20000;  // 20ms
        nanosleep(&ts, NULL);
        ret = PtlMEUnlink(hte->ame);
      }
    }
    iter += dht->entrysize; // pointer math, danger.
  }

  // free our table entries
  for (int ptindex=0; ptindex < dht->ptl.nptes; ptindex++)
    PtlPTFree(dht->ptl.lni, dht->ptl.putindex[ptindex]);

  // release all storage for ht objects
  free(dht->ht);
}



/**
 * pdht_poll - runs periodic polling events
 * @param dht distributed hash table data structure
 */
void *pdht_poll(void *arg) {
  pdht_t *dht = (pdht_t *)arg;
  _pdht_ht_entry_t *hte;
  ptl_event_t ev;
  ptl_me_t me;
  char *index;
  int ret;
  unsigned int ptindex;
  int pollcount = 0;


  // NOTE: we have one thread per DHT, we could re-write to handle _all_ 
  //   polling activity in one thread, need to keep a list of "active" hts
  //   and switch over to PtlEQPoll()
  // XXX - this will cause pain in the future. TODO

  // default match-list entry values
  me.length        = PDHT_MAXKEYSIZE + dht->elemsize; // storing HT key _and_ HT entry in each elem.
  me.ct_handle     = PTL_CT_NONE;
  me.uid           = PTL_UID_ANY;
  // disable auto-unlink events, we just check for PUT completion
  me.options       = PTL_ME_OP_GET 
                   | PTL_ME_OP_PUT
                   | PTL_ME_IS_ACCESSIBLE 
                   | PTL_ME_EVENT_UNLINK_DISABLE;
  me.match_id.rank = PTL_RANK_ANY;

  pdht_eprintf(PDHT_DEBUG_WARN, "Polling thread is active\n");


  // need to run through the event queue for the putindex
  while (!dht->gameover) {
 
    if ((ret = PtlEQPoll(dht->ptl.eq,dht->ptl.nptes, PTL_TIME_FOREVER, &ev, &ptindex)) == PTL_OK)  {
      PDHT_START_TIMER(dht,t5);
      // found something to do, is it something we care about?
      if (ev.type == PTL_EVENT_PUT) {
        pollcount++;
        hte = (_pdht_ht_entry_t *)ev.user_ptr;
        //eprintf("+");
#ifdef PDHT_DEBUG_TRACE
        pdht_dprintf("poll: key: %lu is pending queue bound for %d, new pending is: %d\n", *(unsigned long *)hte->key, pdht_find_bucket(dht, hte), dht->nextfree);
#endif  

        // if get ME is inactive, then this is a new put()
        if (PtlHandleIsEqual(hte->ame, PTL_INVALID_HANDLE)) {

          //eprintf("processing key: %lu\n", (u_int64_t)be64toh(hte->key));
          me.start         = &hte->key; // hte points to entire entry, start at key+val
          me.options       = PTL_ME_OP_GET 
                           | PTL_ME_IS_ACCESSIBLE 
                           | PTL_ME_EVENT_UNLINK_DISABLE;
          me.match_bits = ev.match_bits; // copy over match_bits from put
          me.ignore_bits   = 0;

          PDHT_START_TIMER(dht,t6);
          // append this pending put to the active PTE match list
          ret = PtlMEAppend(dht->ptl.lni, dht->ptl.getindex[ptindex], &me, PTL_PRIORITY_LIST, hte, &hte->ame);
          if (ret != PTL_OK) {
            pdht_dprintf("pdht_poll: ME append failed (active) [%d]: %s\n", pollcount, pdht_ptl_error(ret));
            exit(1);
          }
          PDHT_STOP_TIMER(dht,t6);
        } 

        // setup ME entry to replace the one that was just consumed
        index =  (char *)dht->ht;
        index += (dht->nextfree * dht->entrysize); // pointer math, caution

        hte = (_pdht_ht_entry_t *)index; // index into HT entry table
        me.start       = &hte->key; // put stores key+val into ht
        me.options     = PTL_ME_OP_PUT 
                       | PTL_ME_USE_ONCE 
                       | PTL_ME_IS_ACCESSIBLE 
                       | PTL_ME_EVENT_UNLINK_DISABLE 
                       | PTL_ME_EVENT_LINK_DISABLE;
        me.match_bits  = __PDHT_PENDING_MATCH; // this is ignored, each one of these is a wildcard
        me.ignore_bits = 0xffffffffffffffff; // ignore it all


        assert(dht->nextfree <= dht->maxentries);

        //pdht_dprintf("append: %d %d userp: %p\n", dht->nextfree, pdht_find_bucket(dht, hte), hte);

        PDHT_START_TIMER(dht,t6);
        // add replacement entry to put/pending ME
        ret = PtlMEAppend(dht->ptl.lni, dht->ptl.putindex[ptindex], &me, PTL_PRIORITY_LIST, hte, &hte->pme);
        if (ret != PTL_OK) {
          pdht_dprintf("append: ptindex: %d pollcount: %d %d %d userp: %p\n", ptindex, pollcount, dht->nextfree, pdht_find_bucket(dht, hte), hte);
          pdht_dprintf("pdht_poll: PtlMEAppend error (pending): %s\n", pdht_ptl_error(ret));
        }

        PDHT_STOP_TIMER(dht,t6);

        dht->nextfree++; // update next free space in local HT table
      } else {
        pdht_dprintf("pdht_poll: got event for %s\n", pdht_event_to_string(ev.type));
        pdht_dprintf("pdht_poll: pollcount: %d\n", pollcount);
        pdht_dump_event(&ev);
      }
    } else {
      // check for other problems
      pdht_dprintf("pdht_poll: event queue issue: %s\n", pdht_ptl_error(ret));
    }
    PDHT_STOP_TIMER(dht,t5);

    // count up link events from appending things to the active queue
    pthread_mutex_lock(&dht->completion_mutex);
    pdht_finalize_puts(dht);
    pthread_mutex_unlock(&dht->completion_mutex);
  }
  return NULL;
}
