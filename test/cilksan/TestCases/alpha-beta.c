// RUN: %clang_cilksan -fopencilk -Og %s -o %t
// RUN: %run %t 2>&1 | FileCheck %s

#include <stdio.h>
#include <stdint.h>

#include <cilk/cilk.h>
//#include "../cilktool/cilktool.h"

#define DEPTH_THRESHOLD 7

typedef struct node_s {
  const struct node_s* parent;  
  uint32_t value;
  uint32_t depth;
} node_t;


// Function declarations
void evaluateMove(const node_t *move);
void scout_search(node_t *move, uint32_t depth);


void initialize_move(node_t *move, uint32_t depth) {
  move->depth = depth;
}

void evaluateMove(const node_t *move) {
  node_t next_move;
  next_move.parent = move;
  next_move.value = 2 * move->value;

/* #if EXPOSE_BUG == 1 */
  // NO RACE
  uint32_t new_depth = move->depth + 1;
/* #else */
/*   volatile uint32_t new_depth = move->depth + 1; */
/* #endif */

  // Search further using `scout_search`
  scout_search(&next_move, new_depth);
}

void scout_search(node_t *move, uint32_t depth) {
  // Stop the recursion once our depth surpassed a threshold
  if (depth > DEPTH_THRESHOLD) {
    return;
  }

  initialize_move(move, depth);

  cilk_for(uint32_t i = 0; i < move->value; i++) {
    evaluateMove(move);
  }
}

int main() {
  node_t root_move;
  root_move.parent = NULL;
  root_move.value = 1;
  root_move.depth = 1;
  
  evaluateMove(&root_move);

  return 0;
}

// CHECK: Cilksan detected 0 distinct races.
// CHECK-NEXT: Cilksan suppressed 0 duplicate race reports.
