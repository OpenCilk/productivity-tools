/// -*- C++ -*-
#ifndef INCLUDED_SHADOW_STACK_H
#define INCLUDED_SHADOW_STACK_H

#include <cassert>
#include <cilk/cilk.h>
#include "cilkscale_timer.h"

#ifndef SERIAL_TOOL
#define SERIAL_TOOL 1
#endif

#ifndef TRACE_CALLS
#define TRACE_CALLS 0
#endif

#ifndef DEFAULT_STACK_SIZE
#define DEFAULT_STACK_SIZE 64
#endif

// Enum for types of frames
enum class frame_type
  {
   NONE,
   MAIN,
   SPAWNER,
   HELPER,
  };

// Type for a shadow stack frame
struct shadow_stack_frame_t {
  // Sum of work of all outstanding spawned children of this function observed
  // so far
  cilk_time_t achild_work = cilk_time_t::zero();
  // Work of the continuation of the function since the spawn of its longest
  // child
  cilk_time_t contin_work = cilk_time_t::zero();

  // Longest span of an outstanding spawned child of this function observed so
  // far
  cilk_time_t lchild_span = cilk_time_t::zero();
  // Span of the continuation of the function since the spawn of its longest
  // child
  cilk_time_t contin_span = cilk_time_t::zero();

  // Longest span of an outstanding spawned child of this function observed so
  // far
  cilk_time_t lchild_bspan = cilk_time_t::zero();
  // Span of the continuation of the function since the spawn of its longest
  // child
  cilk_time_t contin_bspan = cilk_time_t::zero();

  // Function type
  frame_type type = frame_type::NONE;

  // Initialize the stack frame. 
  void init(frame_type _type) {
    type = _type;
    achild_work = cilk_time_t::zero();
    contin_work = cilk_time_t::zero();
    lchild_span = cilk_time_t::zero();
    contin_span = cilk_time_t::zero();
    lchild_bspan = cilk_time_t::zero();
    contin_bspan = cilk_time_t::zero();
  }
};


using stack_index_t = int32_t;

// Type for a shadow stack
struct shadow_stack_t {
  // Start and stop timers for measuring the execution time of a strand.
  cilkscale_timer_t start;
  cilkscale_timer_t stop;

private:
  // Dynamic array of shadow-stack frames.
  shadow_stack_frame_t *frames;
  stack_index_t capacity;

  // Index of the shadow-stack frame for the function/task frame at the bottom
  // of the stack.
  stack_index_t bot = 0;

public:
  shadow_stack_t(frame_type type = frame_type::MAIN) {
    frames = new shadow_stack_frame_t[DEFAULT_STACK_SIZE];
    capacity = DEFAULT_STACK_SIZE;
    frames[0].init(type);
  }

  shadow_stack_t(const shadow_stack_t &copy) : capacity(copy.capacity),
                                               bot(copy.bot) {
    frames = new shadow_stack_frame_t[capacity];
    for (stack_index_t i = 0; i <= bot; ++i)
      frames[i] = copy.frames[i];
  }

  ~shadow_stack_t() {
    if (frames)
      delete[] frames;
  }

  // For debugging
  stack_index_t bot_index() const { return bot; }

  shadow_stack_frame_t &peek_bot() const {
    assert(frames && "frames not allocated");
    return frames[bot];
  }

  shadow_stack_frame_t &push(frame_type type) {
    ++bot;

    // If necessary, double the capacity of the shadow stack.
    if (bot >= capacity) {
      // Save the old shadow stack.
      stack_index_t old_capacity = capacity;
      shadow_stack_frame_t *old_frames = frames;

      // Allocate a new shadow stack of twice the capacity.
      capacity *= 2;
      frames = new shadow_stack_frame_t[capacity];

      // Copy the old stack into the new.
      for (stack_index_t i = 0; i < old_capacity; ++i)
        frames[i] = old_frames[i];

      // Delete the old stack.
      delete[] old_frames;
    }

    frames[bot].init(type);
    return frames[bot];
  }

  shadow_stack_frame_t &pop() {
    assert(bot > 0 && "Pop from empty shadow stack.");
    shadow_stack_frame_t &old_bottom = frames[bot];
    --bot;
    return old_bottom;
  }

  /// Reducer support

  static void identity(void *view) {
    new (view) shadow_stack_t(frame_type::SPAWNER);
  }

  static void reduce(void *left_view, void *right_view) {
    shadow_stack_t *left = static_cast<shadow_stack_t *>(left_view);
    shadow_stack_t *right = static_cast<shadow_stack_t *>(right_view);

    shadow_stack_frame_t &l_bot = left->peek_bot();
    shadow_stack_frame_t &r_bot = right->peek_bot();

    assert(frame_type::SPAWNER == r_bot.type);
    assert(0 == right->bot_index());

#if TRACE_CALLS
    fprintf(stderr, "left contin_work = %ld\nleft achild_work = %ld\n"
            "right contin_work = %ld\nright achild_work = %ld\n",
            l_bot.contin_work, l_bot.achild_work,
            r_bot.contin_work, r_bot.achild_work);
    fprintf(stderr, "left contin = %ld\nleft child = %ld\n"
            "right contin = %ld\nright child = %ld\n",
            l_bot.contin_span, l_bot.lchild_span,
            r_bot.contin_span, r_bot.lchild_span);
    fprintf(stderr, "left contin bspan = %ld\nleft child bspan = %ld\n"
            "right contin bspan = %ld\nright child bspan = %ld\n",
            l_bot.contin_bspan, l_bot.lchild_bspan,
            r_bot.contin_bspan, r_bot.lchild_bspan);
#endif

    // Add the work variables from the right stack into the left.
    l_bot.contin_work += r_bot.contin_work;
    l_bot.achild_work += r_bot.achild_work;

    // If the left stack has a longer path from the root to the end of its
    // longest child, set this new span in keft.
    if (l_bot.contin_span + r_bot.lchild_span > l_bot.lchild_span) {
      l_bot.lchild_span = l_bot.contin_span + r_bot.lchild_span;
    }
    // Add the continuation span from the right stack into the left.
    l_bot.contin_span += r_bot.contin_span;

    // If the left stack has a longer path from the root to the end of its
    // longest child, set this new span in keft.
    if (l_bot.contin_bspan + r_bot.lchild_bspan > l_bot.lchild_bspan) {
      l_bot.lchild_bspan = l_bot.contin_bspan + r_bot.lchild_bspan;
    }
    // Add the continuation span from the right stack into the left.
    l_bot.contin_bspan += r_bot.contin_bspan;

    right->~shadow_stack_t();
  }

  duration_t elapsed_time() {
    return ::elapsed_time(&stop, &start);
  }
};

typedef shadow_stack_t cilk_reducer(shadow_stack_t::identity,
                                    shadow_stack_t::reduce)
  shadow_stack_reducer;

#endif
