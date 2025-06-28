#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

// Buddy allocator

static int nsizes;     // the number of entries in bd_sizes array
static void *metadata_end;
static void *mem_end;

#define LEAF_SIZE     16                         // The smallest block size
#define MAXSIZE       (nsizes-1)                 // Largest index in bd_sizes array
#define BLK_SIZE(k)   ((1L << (k)) * LEAF_SIZE)  // Size of block at size k
#define HEAP_SIZE     BLK_SIZE(MAXSIZE)
#define NBLK(k)       (1 << (MAXSIZE-k))         // Number of block at size k
#define ROUNDUP(n,sz) (((((n)-1)/(sz))+1)*(sz))  // Round up to the next multiple of sz

typedef struct list Bd_list;

// The allocator has sz_info for each size k. Each sz_info has a free
// list, an array alloc to keep track which blocks have been
// allocated, and an split array to to keep track which blocks have
// been split.  The arrays are of type char (which is 1 byte), but the
// allocator uses 1 bit per block (thus, one char records the info of
// 8 blocks).
//
// OPTIMIZATION: The alloc array is optimized. Instead of one bit per
// block, we use one bit per PAIR of buddy blocks. This bit stores
// the XOR of the allocation status of the two buddies.
struct sz_info {
  Bd_list free;
  char *alloc;
  char *split;
};
typedef struct sz_info Sz_info;

static Sz_info *bd_sizes;
static void *bd_base;   // start address of memory managed by the buddy allocator
static struct spinlock lock;

// Return 1 if bit at position index in array is set to 1
int bit_isset(char *array, int index) {
  char b = array[index/8];
  char m = (1 << (index % 8));
  return (b & m) == m;
}

// Set bit at position index in array to 1
void bit_set(char *array, int index) {
  char b = array[index/8];
  char m = (1 << (index % 8));
  array[index/8] = (b | m);
}

// Clear bit at position index in array
void bit_clear(char *array, int index) {
  char b = array[index/8];
  char m = (1 << (index % 8));
  array[index/8] = (b & ~m);
}

// *** NEW FUNCTION: Flip bit at position index in array ***
void bit_flip(char *array, int index) {
  char m = (1 << (index % 8));
  array[index/8] ^= m;
}


// Print a bit vector as a list of ranges of 1 bits
void
bd_print_vector(char *vector, int len) {
  int last, lb;

  last = 1;
  lb = 0;
  for (int b = 0; b < len; b++) {
    if (last == bit_isset(vector, b))
      continue;
    if(last == 1)
      printf(" [%d, %d)", lb, b);
    lb = b;
    last = bit_isset(vector, b);
  }
  if(lb == 0 || last == 1) {
    printf(" [%d, %d)", lb, len);
  }
  printf("\n");
}

// Print buddy's data structures
void
bd_print() {
  for (int k = 0; k < nsizes; k++) {
    // *** MODIFIED: Note the length for alloc is NBLK(k)/2 ***
    printf("size %d (blksz %ld nblk %d): free list: ", k, BLK_SIZE(k), NBLK(k));
    lst_print(&bd_sizes[k].free);
    printf("  alloc (XOR):");
    bd_print_vector(bd_sizes[k].alloc, NBLK(k)/2);
    if(k > 0) {
      printf("  split:");
      bd_print_vector(bd_sizes[k].split, NBLK(k));
    }
  }
}

// What is the first k such that 2^k >= n?
int
firstk(uint64 n) {
  int k = 0;
  uint64 size = LEAF_SIZE;

  while (size < n) {
    k++;
    size *= 2;
  }
  return k;
}

// Compute the block index for address p at size k
int
blk_index(int k, char *p) {
  int n = p - (char *) bd_base;
  return n / BLK_SIZE(k);
}

// Convert a block index at size k back into an address
void *addr(int k, int bi) {
  int n = bi * BLK_SIZE(k);
  return (char *) bd_base + n;
}

// allocate nbytes, but malloc won't return anything smaller than LEAF_SIZE
void *
bd_malloc(uint64 nbytes)
{
  int fk, k;

  acquire(&lock);

  // Find a free block >= nbytes, starting with smallest k possible
  fk = firstk(nbytes);
  for (k = fk; k < nsizes; k++) {
    if(!lst_empty(&bd_sizes[k].free))
      break;
  }
  if(k >= nsizes) {
    release(&lock);
    return 0;
  }

  // Found a block; pop it and potentially split it.
  char *p = lst_pop(&bd_sizes[k].free);
  // *** MODIFIED: Flip the XOR bit for the buddy pair. ***
  // The block is changing state from free to allocated.
  bit_flip(bd_sizes[k].alloc, blk_index(k, p) / 2);

  for(; k > fk; k--) {
    // split a block at size k and mark one half allocated at size k-1
    // and put the buddy on the free list at size k-1
    char *q = p + BLK_SIZE(k-1);   // p's buddy
    bit_set(bd_sizes[k].split, blk_index(k, p));
    
    // *** MODIFIED: Flip the XOR bit for the newly allocated half. ***
    // The state of BLK_SIZEp at level k-1 changes from free to allocated.
    
    
    // bit_flip(bd_sizes[k-1].alloc, blk_index(k-1, p) / 2);
    bit_set(bd_sizes[k-1].alloc, blk_index(k-1, p) / 2);
    lst_push(&bd_sizes[k-1].free, q);
  }
  release(&lock);

  return p;
}

// Find the size of the block that p points to.
int
size(char *p) {
  for (int k = 0; k < nsizes; k++) {
    if(bit_isset(bd_sizes[k+1].split, blk_index(k+1, p))) {
      return k;
    }
  }
  return 0;
}
// int
// size(void *p) {
//   // A block of size k is one that was created by splitting a block of size k+1.
//   // We determine its size by finding the smallest level k such that its parent
//   // at level k+1 is marked as split.
//   for (int k = 0; k < MAXSIZE; k++) { // FIX: Loop up to MAXSIZE-1, so k+1 is at most MAXSIZE.
//     if(bit_isset(bd_sizes[k+1].split, blk_index(k+1, p))) {
//       return k;
//     }
//   }
//   // FIX: If the loop completes, it means p was never created by a split,
//   // so it must be the largest possible block.
//   return MAXSIZE;
// }

// Free memory pointed to by p, which was earlier allocated using
// bd_malloc.
void
bd_free(void *p) {
  void *q;
  int k;

  
  acquire(&lock);
  for (k = size(p); k < MAXSIZE; k++) {
    int bi = blk_index(k, p);
    int pair_idx = bi / 2;
    int buddy = (bi % 2 == 0) ? bi+1 : bi-1;

    // Check if the buddy is free for merging.
    // A '1' in the XOR bit means buddy states are different. Since we are
    // freeing 'p' (which is currently allocated), a '1' implies the buddy is free.
    if (bit_isset(bd_sizes[k].alloc, pair_idx) == 0) {
      // Bit is 0, states are the same (buddy is also allocated). Cannot merge.
      // We just flip the bit to 1 to reflect that 'p' is now free and states differ.
      bit_flip(bd_sizes[k].alloc, pair_idx);
      break; // Break out of the merge loop.
    }

    // Buddy is free, we can merge.
    // First, flip the XOR bit from 1 to 0, reflecting that both blocks are now free (same state).
    bit_flip(bd_sizes[k].alloc, pair_idx);

    // Remove buddy from its free list.
    q = addr(k, buddy);
    lst_remove(q); // This is now safe because we hold the lock.

    // The merged block should have the lower address.
    if(buddy % 2 == 0) {
      p = q;
    }
    // At size k+1, mark that the merged buddy pair isn't split anymore.
    bit_clear(bd_sizes[k+1].split, blk_index(k+1, p));
  }
    
  lst_push(&bd_sizes[k].free, p);
  release(&lock);
}


// Compute the first block at size k that doesn't contain p
int
blk_index_next(int k, char *p) {
  int n = (p - (char *) bd_base) / BLK_SIZE(k);
  if((p - (char*) bd_base) % BLK_SIZE(k) != 0)
      n++;
  return n ;
}

int
bd_log2(uint64 n) {
  int k = 0;
  while (n > 1) {
    k++;
    n = n >> 1;
  }
  return k;
}

void
bd_mark(void *start, void *stop)
{
  int bi, bj;

  if (((uint64) start % LEAF_SIZE != 0) || ((uint64) stop % LEAF_SIZE != 0))
    panic("bd_mark");

  for (int k = 0; k < nsizes; k++) {
    bi = blk_index(k, start);
    bj = blk_index_next(k, stop);
    for(; bi < bj; bi++) {
      if(k > 0) {
        // if a block is allocated at size k, mark it as split too.
        bit_set(bd_sizes[k].split, bi);
      }
      // MODIFIED: Flip the bit instead of setting it.
      // Marking a block changes its state from free to allocated.
      // This corresponds to flipping the XOR bit of its pair.
      bit_flip(bd_sizes[k].alloc, bi / 2);
    }
  }
}

// *** MODIFIED: Complete rewrite of bd_initfree_pair and bd_initfree for robustness ***

// If a buddy pair has differing states (XOR bit is 1), figure out which one
// is free by checking if it falls into the initially allocated regions.
int
bd_initfree_pair(int k, int bi) {
  int buddy = (bi % 2 == 0) ? bi+1 : bi-1;

  void* addr_bi = addr(k, bi);
  void* addr_buddy = addr(k, buddy);

  // Check if addr_bi is in the initial meta/unavailable regions
  int bi_is_allocated = (addr_bi >= bd_base && addr_bi < metadata_end) ||
                        (addr_bi >= mem_end);
  
  // Since we know their states differ (XOR bit is 1),
  // the buddy must have the opposite status.
  if (bi_is_allocated) {
    lst_push(&bd_sizes[k].free, addr_buddy); // buddy is free
  } else {
    lst_push(&bd_sizes[k].free, addr_bi); // bi is free
  }
  
  return BLK_SIZE(k);
}
  
// Initialize the free lists for each size k.
int
bd_initfree() {
  int free = 0;

  for (int k = 0; k < MAXSIZE; k++) {
    // Iterate over all pairs at size k
    for (int pair_idx = 0; pair_idx < NBLK(k) / 2; pair_idx++) {
      // If the XOR bit is 1, one buddy is free and one is allocated.
      if (bit_isset(bd_sizes[k].alloc, pair_idx)) {
        // The first block in the pair has index 2*pair_idx
        free += bd_initfree_pair(k, 2 * pair_idx);
      }
    }
  }
  return free;
}

// Mark the range [bd_base,p) as allocated
int
bd_mark_data_structures(char *p) {
  int meta = p - (char*)bd_base;
  printf("bd: %d meta bytes for managing %ld bytes of memory\n", meta, BLK_SIZE(MAXSIZE));
  bd_mark(bd_base, p);
  return meta;
}

// Mark the range [end, HEAPSIZE) as allocated
int
bd_mark_unavailable(void *end, void *left) {
  int unavailable = BLK_SIZE(MAXSIZE)-(end-bd_base);
  if(unavailable > 0)
    unavailable = ROUNDUP(unavailable, LEAF_SIZE);
  printf("bd: 0x%x bytes unavailable\n", unavailable);

  void *bd_end = bd_base+BLK_SIZE(MAXSIZE)-unavailable;
  bd_mark(bd_end, bd_base+BLK_SIZE(MAXSIZE));
  return unavailable;
}

// Initialize the buddy allocator: it manages memory from [base, end).
void
bd_init(void *base, void *end) {
  char *p = (char *) ROUNDUP((uint64)base, LEAF_SIZE);
  int sz;

  initlock(&lock, "buddy");
  bd_base = (void *) p;

  // compute the number of sizes we need to manage [base, end)
  nsizes = bd_log2(((char *)end-p)/LEAF_SIZE) + 1;
  if((char*)end-p > BLK_SIZE(MAXSIZE)) {
    nsizes++;  // round up to the next power of 2
  }

  printf("bd: memory sz is %ld bytes; allocate an size array of length %d\n",
         (char*) end - p, nsizes);

  // allocate bd_sizes array
  bd_sizes = (Sz_info *) p;
  p += sizeof(Sz_info) * nsizes;
  memset(bd_sizes, 0, sizeof(Sz_info) * nsizes);

  // initialize free list and allocate the alloc array for each size k
  for (int k = 0; k < nsizes; k++) {
    lst_init(&bd_sizes[k].free);
    // MODIFIED: Allocate half the space for the alloc array.
    sz = (NBLK(k) / 2 + 7) / 8;
    bd_sizes[k].alloc = p;
    memset(bd_sizes[k].alloc, 0, sz);
    p += sz;
  }

  // allocate the split array for each size k
  for (int k = 1; k < nsizes; k++) {
    sz = sizeof(char)* (ROUNDUP(NBLK(k), 8))/8;
    bd_sizes[k].split = p;
    memset(bd_sizes[k].split, 0, sz);
    p += sz;
  }
  p = (char *) ROUNDUP((uint64) p, LEAF_SIZE);
  
  // *** MODIFIED: Store boundaries before using them ***
  metadata_end = p; // End of metadata is here
  
  int meta = bd_mark_data_structures(p);
  
  int unavailable = bd_mark_unavailable(end, p);
  mem_end = bd_base + BLK_SIZE(MAXSIZE) - unavailable; // Start of unavailable region

  // initialize free lists for each size k
  int free = bd_initfree();

  // check if the amount that is free is what we expect
  if(free != BLK_SIZE(MAXSIZE)-meta-unavailable) {
    printf("free %d %ld\n", free, BLK_SIZE(MAXSIZE)-meta-unavailable);
    panic("bd_init: free mem");
  }
}