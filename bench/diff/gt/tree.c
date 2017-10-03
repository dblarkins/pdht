/**********************************************************/
/*                                                        */
/*    tree.c                                              */
/*                                                        */
/*    author: d. brian larkins                            */
/*    id: $Id: tensor.c 618 2007-03-06 23:53:38Z dinan $  */
/*                                                        */
/**********************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <tensor.h>
#include <diff3d.h>

/*
 * create_tree - collective call to create function tree
 */
gt_tree_t create_tree(gt_context_t *context, int chunksize) {
  gt_tree_t gtree;

  gtree = gt_tree_create(context, chunksize, sizeof(tree_t), GT_NUM_CHILDREN);
  gt_placement_localopen(gtree);
  return gtree;
}


gt_cnp_t *get_root(gt_tree_t ftree) {
  return gt_get_root(ftree);
}



gt_cnp_t *get_parent(gt_tree_t ftree, gt_cnp_t *node) {
  return gt_get_parent(ftree, node);
}


gt_cnp_t *get_child(gt_tree_t ftree, gt_cnp_t *node, int childidx) {
  return gt_get_child_cnp(ftree, node, childidx);
  //return gt_get_child(ftree, node, childidx);
}



int child_index(gt_tree_t ftree, gt_cnp_t *pnode, gt_cnp_t *cnode) {
  return gt_child_index(ftree, pnode, cnode);
}



long get_level(gt_tree_t ftree, gt_cnp_t *node) {
  tree_t *t = gt_get_node(ftree,node);
  long lvl = t->data.level;
  gt_finish_node(ftree, t);
  return lvl;
}



void get_xyzindex(gt_tree_t ftree, gt_cnp_t *node, long *x, long *y, long *z) {
  tree_t *t = gt_get_node(ftree,node);
  *x = t->data.x;
  *y = t->data.y;
  *z = t->data.z;
  gt_finish_node(ftree, t);
}



long get_xindex(gt_tree_t ftree, gt_cnp_t *node) {
  tree_t *t = gt_get_node(ftree,node);
  long idx = t->data.x;
  gt_finish_node(ftree, t);
  return idx;
}



long get_yindex(gt_tree_t ftree, gt_cnp_t *node) {
  tree_t *t = gt_get_node(ftree,node);
  long idx = t->data.y;
  gt_finish_node(ftree, t);
  return idx;
}



long get_zindex(gt_tree_t ftree, gt_cnp_t *node) {
  tree_t *t = gt_get_node(ftree,node);
  long idx = t->data.z;
  gt_finish_node(ftree, t);
  return idx;
}



int has_child(gt_tree_t ftree, gt_cnp_t *node, int whichchild) {
  tree_t *t = gt_get_node(ftree,node);
  int ret = (gt_is_active(&(t->children[whichchild])));
  gt_finish_node(ftree, t);
  return ret;
}


int has_scaling(gt_tree_t ftree, gt_cnp_t *node) {
  tree_t *t = gt_get_node(ftree,node);
  int ret = ((t->data.valid == madCoeffScaling) || (t->data.valid == madCoeffBoth));
  gt_finish_node(ftree, t);
  return ret;
}



int has_wavelet(gt_tree_t ftree, gt_cnp_t *node) {
  tree_t *t = gt_get_node(ftree,node);
  int ret = ((t->data.valid == madCoeffWavelet) || (t->data.valid == madCoeffBoth));
  gt_finish_node(ftree, t);
  return ret;
}



tensor_t *get_scaling(func_t *f, gt_cnp_t *node) {
  assert(node);
  tree_t   *t = gt_get_node(f->ftree,node);
  assert(t);
  //printf(" %d: scaling\n", context->mythread);
  if ((t->data.valid == madCoeffScaling) || (t->data.valid == madCoeffBoth)) {
    return tensor_copy((tensor_t *)&t->data.s);
  } else
    return NULL;
}



tensor_t *get_wavelet(func_t *f, gt_cnp_t *node) {
  assert(node);
  tree_t   *t = gt_get_node(f->ftree,node);
  assert(t);
  //printf(" %d: wavelet\n", context->mythread);
  if ((t->data.valid == madCoeffWavelet) || (t->data.valid == madCoeffBoth)) {
    return tensor_copy((tensor_t *)&t->data.d);
  } else
    return NULL;
}



#if 0
void set_root(gt_tree_t ftree, gt_cnp_t *root) {
  gtrees[ftree].root = root;
}
#endif


gt_cnp_t *set_child(gt_tree_t ftree, gt_cnp_t *parent, long level, long x, long y, long z, int childidx) {
  gt_cnp_t *child;
  tree_t *pnode, *cnode;

  pnode = gt_get_node(ftree, parent);
  
  child = gt_node_alloc(ftree, &pnode->children[childidx], parent);
  cnode = gt_get_node(ftree, child);
  
  gt_cnp_copy(ftree, parent,&(cnode->parent)); // XXX - can be fixed up

  gt_clear_children(ftree, cnode);
  cnode->data.level = level;
  cnode->data.x = x;
  cnode->data.y = y;
  cnode->data.z = z;
  cnode->data.valid = madCoeffNone;

  gt_put_node(ftree, child, cnode);
  gt_put_node(ftree, parent, pnode);

  return child;
}



void free_node(gt_tree_t ftree, gt_cnp_t *node) {
  gt_cnp_t *parent = get_parent(ftree, node);
  long i;
  tree_t *p = gt_get_node(ftree, parent);

  i = gt_child_index(ftree, parent, node);

  gt_set_inactive(&(p->children[i]));
  gt_put_node(ftree,node,p);
  gt_node_free(ftree,node);
}



void set_level(gt_tree_t ftree, gt_cnp_t *node, long level) {
  tree_t *t = gt_get_node(ftree,node);
  t->data.level = level;
  gt_put_node(ftree, node,t);
}



void set_xyzindex(gt_tree_t ftree, gt_cnp_t *node, long x, long y, long z) {
  tree_t *t = gt_get_node(ftree,node);
  t->data.x = x;
  t->data.y = y;
  t->data.z = z;
  gt_put_node(ftree, node,t);
}



void set_xindex(gt_tree_t ftree, gt_cnp_t *node, long x) {
  tree_t *t = gt_get_node(ftree,node);
  t->data.x = x;
  gt_put_node(ftree, node,t);
}



void set_yindex(gt_tree_t ftree, gt_cnp_t *node, long y) {
  tree_t *t = gt_get_node(ftree,node);
  t->data.y = y;
  gt_put_node(ftree, node,t);
}



void set_zindex(gt_tree_t ftree, gt_cnp_t *node, long z) {
  tree_t *t = gt_get_node(ftree,node);
  t->data.z = z;
  gt_put_node(ftree, node,t);
}



void set_scaling(func_t *f, gt_cnp_t *node, tensor_t *scoeffs) {
  assert(node);
  tree_t *t = gt_get_node(f->ftree, node);
  assert(t);

  if (!scoeffs) {
    t->data.valid = (t->data.valid == madCoeffBoth) ? madCoeffWavelet : madCoeffNone;

  } else {
    memcpy(&t->data.s, scoeffs, sizeof(tensor3dk_t));

    // technically need to check for both && wavelet
    t->data.valid = (t->data.valid == madCoeffWavelet) ? madCoeffBoth : madCoeffScaling;
  }
  gt_put_node(f->ftree,node,t);
}



void set_wavelet(func_t *f, gt_cnp_t *node, tensor_t *dcoeffs) {
  assert(node);
  tree_t *t = gt_get_node(f->ftree, node);
  assert(t);
  if (!dcoeffs) {
    t->data.valid = (t->data.valid == madCoeffBoth) ? madCoeffScaling : madCoeffNone;

  } else {
    // fucking huge!
    memcpy(&t->data.d, dcoeffs, sizeof(tensor3d2k_t));

    // technically need to check for both && wavelet
    t->data.valid = (t->data.valid == madCoeffScaling) ? madCoeffBoth : madCoeffWavelet;
  }
  gt_put_node(f->ftree,node,t);
}


void print_node(gt_tree_t ftree, gt_cnp_t *t) {
  gt_cnp_t *p;
  tree_t   *n;
  int i;

  n = gt_get_node(ftree,t);
  printf("node: %ld %ld,%ld,%ld ", n->data.level, n->data.x, n->data.y, n->data.z);
  if ((n->data.valid == madCoeffScaling) || (n->data.valid == madCoeffBoth))
    printf("s ");
  if ((n->data.valid == madCoeffWavelet) || (n->data.valid == madCoeffBoth))
    printf("d ");
  printf("\n");

  printf("  node pointer: ci: %ld ni: %ld : %p\n", t->ci, t->ni, t);
  gt_finish_node(ftree,n);

  p = get_parent(ftree, t);
  printf("    parent: ci: %ld ni: %ld\n", p->ci, p->ni);
  tfree(p);
  for (i=0;i<8;i++) {
    p = get_child(ftree, t, i);
    if (p)
      printf("    child : ci: %ld ni: %ld\n", p->ci, p->ni);
    else
      printf("    child : (nil)\n");
    tfree(p);
  }

}




void set_children(gt_tree_t ftree, gt_cnp_t *node) {
  long level = get_level(ftree, node);
  long i,j,k;
  long x,y,z;
  long child = 0;

  x = 2*get_xindex(ftree, node);
  y = 2*get_yindex(ftree, node);
  z = 2*get_zindex(ftree, node);

  for (i=0;i<2;i++) {
    for (j=0;j<2;j++) {
      for (k=0;k<2;k++) {
	set_child(ftree, node, level+1, x+i, y+j, z+k, child++);
      }
    }
  }
}

