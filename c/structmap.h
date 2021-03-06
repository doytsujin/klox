#ifndef klox_structmap_h
#define klox_structmap_h

#include <assert.h>
#include <inttypes.h>
#include <stdint.h>

#include <cb.h>
#include <cb_region.h>

#define CB_NULL ((cb_offset_t)0)

extern __thread void *thread_ring_start;
extern __thread cb_mask_t thread_ring_mask;

static const int STRUCTMAP_LEVEL_BITS = 5;

// The maximum amount structmap_nodes we may need for a modification (insertion)
// so that no CB resizes will happen.  This is `ceil(64 / STRUCTMAP_LEVEL_BITS)`
// structmap_node levels times 2 (heightening to max height, before descending
// to max depth) minus 1 (because the highest node is shared across the
// ascending and descending paths).
static const int STRUCTMAP_MODIFICATION_MAX_NODES = ((64 / STRUCTMAP_LEVEL_BITS + (int)!!(64 % STRUCTMAP_LEVEL_BITS)) * 2 - 1);

//NOTES:
// 1) Neither keys nor values are allowed to be 0, as this value is reserved
//    for NULL-like sentinels.

typedef size_t (*structmap_value_size_t)(const struct cb *cb, uint64_t v);

struct structmap
{
    uint64_t     enclosed_mask;
    int          shl;
    uint64_t     lowest_inserted_key;
    uint64_t     highest_inserted_key;
    cb_offset_t  root_node_offset;
    unsigned int node_count;
    size_t       total_external_size;
    unsigned int height;
    structmap_value_size_t sizeof_value;
};

struct structmap_node
{
    uint64_t children[1 << STRUCTMAP_LEVEL_BITS];
};


void structmap_init(struct structmap *sm, structmap_value_size_t sizeof_value);

int
structmap_insert(struct cb        **cb,
                 struct cb_region  *region,
                 struct structmap  *sm,
                 uint64_t           key,
                 uint64_t           value);

extern inline bool
structmap_lookup(const struct cb        *cb,
                 const struct structmap *sm,
                 uint64_t                key,
                 uint64_t               *value)
{
  if ((key & sm->enclosed_mask) != key)
    return false;

  struct structmap_node *n;
  uint64_t child = sm->root_node_offset;
  int path;

  for (int shl = sm->shl; shl; shl -= STRUCTMAP_LEVEL_BITS) {
    n = (struct structmap_node *)cb_at_immed(thread_ring_start, thread_ring_mask, child);
    path = (key >> shl) & (((uint64_t)1 << STRUCTMAP_LEVEL_BITS) - 1);
    child = n->children[path];
    if (child == 1) { return false; }
  }
  n = (struct structmap_node *)cb_at_immed(thread_ring_start, thread_ring_mask, child);
  path = key & (((uint64_t)1 << STRUCTMAP_LEVEL_BITS) - 1);
  uint64_t tmpval = n->children[path];
  if (tmpval == 1) { return false; }

  *value = tmpval;

  return true;
}

extern inline bool
structmap_contains_key(const struct cb        *cb,
                       const struct structmap *sm,
                       uint64_t                key)
{
  uint64_t v;
  return (structmap_lookup(cb, sm, key, &v) == true);
}

typedef int (*structmap_traverse_func_t)(uint64_t key, uint64_t value, void *closure);

int
structmap_traverse(const struct cb           **cb,
                   const struct structmap     *sm,
                   structmap_traverse_func_t   func,
                   void                       *closure);

extern inline size_t
structmap_modification_size(void) //FIXME switch to constant?
{
  // The maximum amount of space for the maximum amount of nodes we may need for
  // a modification, plus alignment.

  return STRUCTMAP_MODIFICATION_MAX_NODES * sizeof(struct structmap_node) + alignof(struct structmap_node) - 1;
}

extern inline size_t
structmap_internal_size(const struct structmap *sm)
{
  return sm->node_count * sizeof(struct structmap_node) + alignof(struct structmap_node) - 1;
}

extern inline size_t
structmap_external_size(const struct structmap *sm)
{
  return sm->total_external_size;
}

extern inline void
structmap_external_size_adjust(struct structmap *sm,
                               ssize_t           adjustment)
{
  sm->total_external_size = (size_t)((ssize_t)sm->total_external_size + adjustment);
}

extern inline size_t
structmap_size(const struct structmap *sm)
{
  return structmap_internal_size(sm) + structmap_external_size(sm) + structmap_modification_size();
}

#endif  //klox_structmap_h
