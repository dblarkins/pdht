#define _XOPEN_SOURCE 600
#include <unistd.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <pdht.h>

/*
 * TO RUN:
 *   - need to make sure that PDHT_DEFAULT_TABLE_SIZE is at least 100K * NUM_PTES
 *   - set PDHT_DEFAULT_NUM_PTES to 2
 *   - set PDHT_DEFAULT_TABLE_SIZE 250000
 *   - set PDHT_PENDINGQ_SIZE 100000
 */

#define NITER 10000

extern pdht_context_t *c;
int eprintf(const char *format, ...);

int main(int argc, char **argv);

#define START_TIMER(TMR) TMR.last = pdht_get_wtime();
#define STOP_TIMER(TMR) TMR.total += pdht_get_wtime() - TMR.last;
#define READ_TIMER(TMR) TMR.total

void localhash(pdht_t *dht, void *key, ptl_match_bits_t *mbits, uint32_t *ptindex, ptl_process_t *rank) {
  (*rank).rank = 0;
  *mbits = *(unsigned long *)key;
  //*ptindex = *(unsigned long *)key % dht->nptes;
  *ptindex = 0;
}

void remotehash(pdht_t *dht, void *key, ptl_match_bits_t *mbits, uint32_t *ptindex, ptl_process_t *rank) {
  (*rank).rank = 1;
  *mbits = *(unsigned long *)key;
  //*ptindex = *(unsigned long *)key % dht->nptes;
  *ptindex = 0;
}


int main(int argc, char **argv) {
  pdht_t *ht;
  pdht_status_t ret;
  size_t elemsize = sizeof(unsigned long);
  pdht_timer_t ltimer;
  unsigned long key = 0; // whatever, just increasing monotonically
  void *val = NULL;
  int opt, maxiters = NITER;
  int mlengths[10] =  { 1, 10, 100, 1000, 2000, 5000, 10000, 20000, 50000, 100000};
  int lastlength = 9;
  pdht_timer_t gtimer,total;

  setenv("PTL_IGNORE_UMMUNOTIFY", "1",1);
  setenv("PTL_PROGRESS_NOSLEEP","1",1);

  while ((opt = getopt(argc, argv, "i:s:")) != -1) {
    switch (opt) {
      case 'i':
        maxiters = atoi(optarg);
        break;
      case 's':
        elemsize = atoi(optarg);
        break;
    } 
  }

  val = malloc(elemsize);
  memset(val,0,elemsize);


  // create hash table
  ht = pdht_create(sizeof(unsigned long), elemsize, PdhtModeStrict);

  if (c->size != 2) {
    if (c->rank == 0) {
      printf("requires two (and only two) processes to run\n");
    }
    goto done;
  }

  START_TIMER(total);

  pdht_sethash(ht, remotehash);
  //pdht_sethash(ht, localhash);

  printf("putting from %d to %d\n", 0, mlengths[lastlength]);fflush(stdout);
  for (int iter=0; iter < mlengths[lastlength]; iter++) {
    if (c->rank == 0) {
      pdht_put(ht, &key, val);
      key++;
    } 
  }

  pdht_barrier();

  // for each matchlist length in mlengths...
  for (int len=0; len<=lastlength; len++) {
    if (c->rank == 0) {
      key = mlengths[len] - 1;
      memset(&gtimer,0,sizeof(pdht_timer_t));
      START_TIMER(gtimer);
      for (int iter=0; iter < maxiters; iter++) {
        pdht_get(ht, &key, val);
      }
      STOP_TIMER(gtimer);
      eprintf(" %7d %12.7f\n", mlengths[len], (READ_TIMER(gtimer)/(double)maxiters)*1000000);
    }
    pdht_barrier();
  }

  pdht_print_stats(ht);

  STOP_TIMER(total);
  eprintf("total elapsed time: %12.7f\n", READ_TIMER(total));
done:
  pdht_free(ht);
  free(val);
}
