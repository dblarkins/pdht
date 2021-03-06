#define _XOPEN_SOURCE 600
#include <unistd.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <pdht.h>
#include <city.h>

#define ASIZE 1000

extern pdht_context_t *c;
struct point{
  int x;
  int y;

};
typedef struct point pt;

static void kprint(void *key);
static void vprint(void *val);

void ahash(pdht_t *dht,void *key,ptl_match_bits_t *mbits, uint32_t *ptindex,ptl_process_t *rank){
   *mbits = CityHash64((char *)key, dht->keysize);
   *ptindex = *mbits % dht->ptl.nptes;
   (*rank).rank  = 0; // for testing only
   //(*rank).rank  = *mbits % c->size;
}



int main(int argc, char **agrv[]){
  pdht_t *ht; //making pointer to a pdht
  unsigned long key = 0; 
  pt value;//creating buffers to place thing I am putting and getting
  pdht_timer_t total,ptimer,gtimer,utimer;//creating a time for total time to complete, puts and gets/updates
  size_t elemsize = sizeof(pt);
 


  pdht_config_t cfg; //creating configure datastructure
  cfg.pendmode = PdhtPendingTrig; //setting to triggered mode
  cfg.nptes = 1;
  cfg.maxentries = 250000;
  cfg.pendq_size = 500; //if you change this to 500 it will not be happy but will work,it you change it to <500 it will not finish
  cfg.ptalloc_opts = 0;
  setenv("PTL_IGNORE_UMMUNOTIFY","1",1);
  setenv("PTL_PROGRESS_NOSLEEP","1",1);
  
  pdht_tune(PDHT_TUNE_ALL,&cfg); //setting rest of cfg with pdht_tune?
  //printf("creating ht\n");
  ht = pdht_create(sizeof(unsigned long),elemsize,PdhtModeStrict);//creating pdht with key size of int and a value size of a point

  pdht_sethash(ht, ahash);

  pdht_barrier();

  PDHT_START_ATIMER(total);//starting total timer
  key =  1;

  long unsigned fAdd = key-1; //something to add to ASIZE for for loops


  //int simRank = 1; //used to tell if the math would allow this to run on 2 processes
  
  if (c->rank == 0){
    printf("putting \n");
    PDHT_START_ATIMER(ptimer); //starting put timer
    for (int i = fAdd; i < ASIZE ;i++){
      //setting key x and y vals
      value.x = i;
      value.y = i;
      
      pdht_put(ht,&key,&value);//putting into the pdht
      
      printf("%ld ", key);
      key += 1;
    } //for
    
    PDHT_STOP_ATIMER(ptimer); //stopping put timer
    printf("Put Timer: %d, %12.7f ms\n",c->rank,PDHT_READ_ATIMER_MSEC(ptimer));
  }
  

  pdht_fence(ht);
  
  
  //goto done;
  


  key = 1;
  
  pdht_print_active(ht, kprint, vprint);

  if (c->rank == 0){
    printf("updating\n");
    PDHT_START_ATIMER(gtimer); //starting get/update timer
    
    
    for(int i = fAdd; i<ASIZE;i++){
  
      pdht_get(ht,&key,&value); //getting pt from hash table
      

      value.y++;
      
      //printf("x: %d, y:%d\n",value.x,value.y);
      pdht_update(ht,&key,&value);
    
      key += 1;
    }//for
    PDHT_STOP_ATIMER(gtimer);//stopping get timer
    printf("Get Timer: %d, %12.7f ms\n",c->rank,PDHT_READ_ATIMER_MSEC(gtimer));
  
  }


  pdht_fence(ht);
  pdht_print_active(ht, kprint, vprint);
  key = 1;
  if (c->rank == 0){
    printf("validating\n");

    PDHT_START_ATIMER(utimer);
    for(int i = fAdd;i<ASIZE;i++){
      pdht_get(ht,&key,&value);
      if ((i != value.x) || ((i+1) != value.y)){
        printf("Did not match\n");
        printf("i : %d, x : %d, y : %d\n",i,value.x,value.y);

      }
      key += 1;
    }
    
  
    PDHT_STOP_ATIMER(utimer);
    printf("Update Timer: %d, %12.7f ms\n",c->rank,PDHT_READ_ATIMER_MSEC(utimer));
  }
  pdht_fence(ht);

  PDHT_STOP_ATIMER(total);//stopping timer
 
  printf("Total: %d, %12.7f ms\n",c->rank,PDHT_READ_ATIMER_MSEC(total));
done:
  pdht_print_stats(ht);//print ht stats
  printf("freeing hash table\n");
  pdht_free(ht);//freeing hash table
  return 0;
}


static void kprint(void *key) {
  unsigned long *kp = key;
  printf(" key: %ld ", *kp);
}

static void vprint(void *val) {
  pt *p = val;
  printf(" x: %d y: %d", p->x, p->y);
}
