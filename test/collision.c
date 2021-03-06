#include <sys/time.h>
#include <sys/resource.h>

#include <pdht.h>

#define ASIZE 10

void pdht_poll(pdht_t *dht);

extern pdht_context_t *c;

void f_hash(pdht_t *dht, void *key, ptl_match_bits_t *mbits, ptl_process_t *rank);
int main(int argc, char **argv);



void f_hash(pdht_t *dht, void *key, ptl_match_bits_t *mbits, ptl_process_t *rank) {
  // everything hashes to 1,1
  *mbits = 1;
  (*rank).rank = 1;
}



int main(int argc, char **argv) {
  pdht_t *ht;
  unsigned long key = 10;
  double pbuf[ASIZE], gbuf[ASIZE];
  pdht_status_t ret;
  
  // create hash table
  ht = pdht_create(sizeof(unsigned long), ASIZE * sizeof(double), PdhtModeStrict);

  if (c->size != 2) {
    if (c->rank == 0) { 
      printf("requires two (and only two) processes to run\n");
    }
    goto done;
  }

  pdht_sethash(ht, f_hash);


  if (c->rank == 0) {
    for (int i=0; i<ASIZE; i++) {
       pbuf[i] = i * 1.1 + 13.0;
       gbuf[i] = i * 2.2 + 13.0;
    }

    //printf("%d: putting object\n", c->rank);
    ret = pdht_put(ht, &key, pbuf);
    if (ret != PdhtStatusOK) {
       printf("initial put failed\n");
    }
    pdht_barrier();
    pdht_barrier();

    //printf("%d: putting object\n", c->rank);
    key += 1;

    // try getting with different key, but same match bits (see shitty hash)
    ret = pdht_get(ht, &key, gbuf);
    if (ret != PdhtStatusCollision) {
       printf("failed : %d\n",ret);
    } else {
       printf("passed\n");
    } 
    pdht_barrier();



  } else {

    pdht_barrier();

    //printf("%d: calling poll()\n", c->rank); 
    pdht_poll(ht);

    pdht_barrier();

    //printf("%d: calling poll()\n", c->rank); 
    pdht_poll(ht);

    pdht_barrier();

  }

  pdht_barrier();

done:
  pdht_print_stats(ht);
  pdht_free(ht);
}
