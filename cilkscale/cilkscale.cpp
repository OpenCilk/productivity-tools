#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>

// Ensure that __cilkscale__ is defined, so we can provide a nontrivial
// definition of getworkspan().
#ifndef __cilkscale__
#define __cilkscale__
#endif

#include "shadow_stack.h"
#include <csi/csi.h>
#include <iostream>
#include <fstream>

#define CILKTOOL_API extern "C" __attribute__((visibility("default")))

#ifndef SERIAL_TOOL
#define SERIAL_TOOL 1
#endif

#ifndef TRACE_CALLS
#define TRACE_CALLS 0
#endif

#if SERIAL_TOOL
FILE *err_io = stderr;
#else
#include <cilk/reducer.h>
#include <cilk/reducer_ostream.h>
#include "shadow_stack_reducer.h"
#endif
#include <cilk/cilk_api.h>

// defined in libopencilk
extern "C" int __cilkrts_is_initialized(void);
extern "C" void __cilkrts_internal_set_nworkers(unsigned int nworkers);

///////////////////////////////////////////////////////////////////////////
// Data structures for tracking work and span.

// Top-level class to manage the state of the global Cilkscale tool.  This class
// interface allows the tool to initialize data structures, such as a
// std::ostream and a std::ofstream, only after the standard libraries they rely
// on have been initialized, and to destroy those structures before those
// libraries are deinitialized.
class CilkscaleImpl_t {
public:
  // Shadow-stack data structure, for managing work-span variables.
#if SERIAL_TOOL
  shadow_stack_t *shadow_stack = nullptr;
#else
  shadow_stack_reducer *shadow_stack = nullptr;
#endif

  // Output stream for printing results.
  std::ostream &outs = std::cout;
  std::ofstream outf;
#if !SERIAL_TOOL
  cilk::reducer<cilk::op_ostream> *outf_red = nullptr;
#endif

  CilkscaleImpl_t();
  ~CilkscaleImpl_t();
};

// Top-level Cilkscale tool.
static CilkscaleImpl_t *create_tool(void) {
  if (!__cilkrts_is_initialized())
    // If the OpenCilk runtime is not yet initialized, then csi_init will
    // register a call to init_tool to initialize the tool after the runtime is
    // initialized.
    return nullptr;

  // Otherwise, ordered dynamic initalization should ensure that it's safe to
  // create the tool.
  return new CilkscaleImpl_t();
}
static CilkscaleImpl_t *tool = create_tool();

// Macro to access the correct shadow-stack data structure, based on the
// initialized state of the tool.
#if SERIAL_TOOL
#define STACK (*tool->shadow_stack)
#else
#define STACK (tool->shadow_stack->get_view())
#endif

// Macro to use the correct output stream, based on the initialized state of the
// tool.
#if SERIAL_TOOL
#define OUTPUT ((tool->outf.is_open()) ? (tool->outf) : (tool->outs))
#else
#define OUTPUT                                                                 \
  ((tool->outf_red) ? (**(tool->outf_red))                                     \
                    : ((tool->outf.is_open()) ? (tool->outf) : (tool->outs)))
#endif

bool CILKSCALE_INITIALIZED = false;

///////////////////////////////////////////////////////////////////////////
// Utilities for printing analysis results

// Ensure that a proper header has been emitted to OS.
template<class Out>
static void ensure_header(Out &OS) {
  static bool PRINT_STARTED = false;
  if (PRINT_STARTED)
    return;

  OS << "tag,work (" << cilk_time_t::units << ")"
     << ",span (" << cilk_time_t::units << ")"
     << ",parallelism"
     << ",burdened_span (" << cilk_time_t::units << ")"
     << ",burdened_parallelism\n";

  PRINT_STARTED = true;
}

// Emit the given results to OS.
template<class Out>
static void print_results(Out &OS, const char *tag, cilk_time_t work,
                          cilk_time_t span, cilk_time_t bspan) {
  OS << tag
     << "," << work << "," << span << "," << work.get_val_d() / span.get_val_d()
     << "," << bspan << "," << work.get_val_d() / bspan.get_val_d() << "\n";
}

// Emit the results from the overall program execution to the proper output
// stream.
static void print_analysis(void) {
  assert(CILKSCALE_INITIALIZED);
  shadow_stack_frame_t &bottom = STACK.peek_bot();

  assert(frame_type::NONE != bottom.type);

  cilk_time_t work = bottom.contin_work;
  cilk_time_t span = bottom.contin_span;
  cilk_time_t bspan = bottom.contin_bspan;

  ensure_header(OUTPUT);
  print_results(OUTPUT, "", work, span, bspan);
}

///////////////////////////////////////////////////////////////////////////
// Tool startup and shutdown

#if SERIAL_TOOL
// Ensure that this tool is run serially
static inline void ensure_serial_tool(void) {
  fprintf(stderr, "Forcing CILK_NWORKERS=1.\n");
  if (__cilkrts_is_initialized()) {
    __cilkrts_internal_set_nworkers(1);
  } else {
    // Force the number of Cilk workers to be 1.
    char *e = getenv("CILK_NWORKERS");
    if (!e || 0 != strcmp(e, "1")) {
      if (setenv("CILK_NWORKERS", "1", 1)) {
        fprintf(err_io, "Error setting CILK_NWORKERS to be 1\n");
        exit(1);
      }
    }
  }
}
#endif

CilkscaleImpl_t::CilkscaleImpl_t() {
#if SERIAL_TOOL
  shadow_stack = new shadow_stack_t(frame_type::MAIN);
#else
  shadow_stack = new shadow_stack_reducer();
#endif

  const char *envstr = getenv("CILKSCALE_OUT");
  if (envstr)
    outf.open(envstr);

#if !SERIAL_TOOL
  outf_red =
      new cilk::reducer<cilk::op_ostream>((outf.is_open() ? outf : outs));
#endif

#if SERIAL_TOOL
  shadow_stack_t &stack = *shadow_stack;
#else
  shadow_stack_t &stack = shadow_stack->get_view();
#endif
  stack.push(frame_type::SPAWNER);
  stack.start.gettime();
}

CilkscaleImpl_t::~CilkscaleImpl_t() {
  STACK.stop.gettime();
  shadow_stack_frame_t &bottom = STACK.peek_bot();

  duration_t strand_time = elapsed_time(&(STACK.stop), &(STACK.start));
  bottom.contin_work += strand_time;
  bottom.contin_span += strand_time;
  bottom.contin_bspan += strand_time;

  print_analysis();

  if (outf.is_open())
    outf.close();
  delete shadow_stack;

#if !SERIAL_TOOL
  delete outf_red;
  outf_red = nullptr;
#endif
}

///////////////////////////////////////////////////////////////////////////
// Hooks for operating the tool.

// Custom function to intialize tool after the OpenCilk runtime is initialized.
static void init_tool(void) {
  assert(nullptr == tool && "Tool already initialized");
  tool = new CilkscaleImpl_t();
}

static void destroy_tool(void) {
  if (tool) {
    delete tool;
    tool = nullptr;
  }

  CILKSCALE_INITIALIZED = false;
}

CILKTOOL_API void __csi_init() {
#if TRACE_CALLS
  fprintf(stderr, "__csi_init()\n");
#endif

  if (!__cilkrts_is_initialized())
    __cilkrts_atinit(init_tool);

  __cilkrts_atexit(destroy_tool);

#if SERIAL_TOOL
  ensure_serial_tool();
#endif

  CILKSCALE_INITIALIZED = true;
}

CILKTOOL_API void __csi_unit_init(const char *const file_name,
                                  const instrumentation_counts_t counts) {
  return;
}

CILKTOOL_API
void __csi_bb_entry(const csi_id_t bb_id, const bb_prop_t prop) {
  if (!CILKSCALE_INITIALIZED)
    return;

  shadow_stack_t &stack = STACK;

  shadow_stack_frame_t &bottom = stack.peek_bot();
  get_bb_time(&bottom.contin_work, &bottom.contin_span, &bottom.contin_bspan,
              bb_id);
  return;
}

CILKTOOL_API
void __csi_bb_exit(const csi_id_t bb_id, const bb_prop_t prop) { return; }

CILKTOOL_API
void __csi_func_entry(const csi_id_t func_id, const func_prop_t prop) {
  if (!CILKSCALE_INITIALIZED)
    return;
  if (!prop.may_spawn)
    return;

  shadow_stack_t &stack = STACK;

  stack.stop.gettime();

#if TRACE_CALLS
  fprintf(stderr, "[W%d] func_entry(%ld)\n", __cilkrts_get_worker_number(),
          func_id);
#endif

  shadow_stack_frame_t &bottom = stack.peek_bot();

  duration_t strand_time = elapsed_time(&(stack.stop), &(stack.start));
  bottom.contin_work += strand_time;
  bottom.contin_span += strand_time;
  bottom.contin_bspan += strand_time;

  shadow_stack_frame_t &p_bottom = stack.peek_bot();
  cilk_time_t p_contin_work = p_bottom.contin_work;
  cilk_time_t p_contin_span = p_bottom.contin_span;
  cilk_time_t p_contin_bspan = p_bottom.contin_bspan;

  // Push new frame onto the stack
  shadow_stack_frame_t &c_bottom = stack.push(frame_type::SPAWNER);
  c_bottom.contin_work = p_contin_work;
  c_bottom.contin_span = p_contin_span;
  c_bottom.contin_bspan = p_contin_bspan;

  // stack.start.gettime();
  // Because of the high overhead of calling gettime(), especially compared to
  // the running time of the operations in this hook, the work and span
  // measurements appear more stable if we simply use the recorded time as the
  // new start time.
  stack.start = stack.stop;
}

CILKTOOL_API
void __csi_func_exit(const csi_id_t func_exit_id, const csi_id_t func_id,
                     const func_exit_prop_t prop) {
  if (!CILKSCALE_INITIALIZED)
    return;
  if (!prop.may_spawn)
    return;

  shadow_stack_t &stack = STACK;

  stack.stop.gettime();

#if TRACE_CALLS
  fprintf(stderr, "[W%d] func_exit(%ld)\n", __cilkrts_get_worker_number(),
          func_id);
#endif

  duration_t strand_time = elapsed_time(&(stack.stop), &(stack.start));

  assert(cilk_time_t::zero() == stack.peek_bot().lchild_span);

  // Pop the stack
  shadow_stack_frame_t &c_bottom = stack.pop();
  shadow_stack_frame_t &p_bottom = stack.peek_bot();

  p_bottom.contin_work = c_bottom.contin_work + strand_time;
  p_bottom.contin_span = c_bottom.contin_span + strand_time;
  p_bottom.contin_bspan = c_bottom.contin_bspan + strand_time;

  // stack.start.gettime();
  // Because of the high overhead of calling gettime(), especially compared to
  // the running time of the operations in this hook, the work and span
  // measurements appear more stable if we simply use the recorded time as the
  // new start time.
  stack.start = stack.stop;
}

CILKTOOL_API
void __csi_detach(const csi_id_t detach_id, const int32_t *has_spawned) {
  shadow_stack_t &stack = STACK;

  stack.stop.gettime();

#if TRACE_CALLS
  fprintf(stderr, "[W%d] detach(%ld)\n", __cilkrts_get_worker_number(),
          detach_id);
#endif

  shadow_stack_frame_t &bottom = stack.peek_bot();

  duration_t strand_time = elapsed_time(&(stack.stop), &(stack.start));
  bottom.contin_work += strand_time;
  bottom.contin_span += strand_time;
  bottom.contin_bspan += strand_time;
}

CILKTOOL_API
void __csi_task(const csi_id_t task_id, const csi_id_t detach_id,
                const task_prop_t prop) {
  shadow_stack_t &stack = STACK;

#if TRACE_CALLS
  fprintf(stderr, "[W%d] task(%ld, %ld)\n", __cilkrts_get_worker_number(),
          task_id, detach_id);
#endif

  shadow_stack_frame_t &p_bottom = stack.peek_bot();
  cilk_time_t p_contin_work = p_bottom.contin_work;
  cilk_time_t p_contin_span = p_bottom.contin_span;
  cilk_time_t p_contin_bspan = p_bottom.contin_bspan;

  // Push new frame onto the stack.
  shadow_stack_frame_t &c_bottom = stack.push(frame_type::HELPER);
  c_bottom.contin_work = p_contin_work;
  c_bottom.contin_span = p_contin_span;
  c_bottom.contin_bspan = p_contin_bspan;

  stack.start.gettime();
}

CILKTOOL_API
void __csi_task_exit(const csi_id_t task_exit_id, const csi_id_t task_id,
                     const csi_id_t detach_id, const task_exit_prop_t prop) {
  shadow_stack_t &stack = STACK;

  stack.stop.gettime();

#if TRACE_CALLS
  fprintf(stderr, "[W%d] task_exit(%ld, %ld, %ld)\n",
          __cilkrts_get_worker_number(), task_exit_id, task_id, detach_id);
#endif

  shadow_stack_frame_t &bottom = stack.peek_bot();

  duration_t strand_time = elapsed_time(&(stack.stop), &(stack.start));
  bottom.contin_work += strand_time;
  bottom.contin_span += strand_time;
  bottom.contin_bspan += strand_time;

  assert(cilk_time_t::zero() == bottom.lchild_span);

  // Pop the stack
  shadow_stack_frame_t &c_bottom = stack.pop();
  shadow_stack_frame_t &p_bottom = stack.peek_bot();
  p_bottom.achild_work += c_bottom.contin_work - p_bottom.contin_work;
  // Check if the span of c_bottom exceeds that of the previous longest child.
  if (c_bottom.contin_span > p_bottom.lchild_span)
    p_bottom.lchild_span = c_bottom.contin_span;
  if (c_bottom.contin_bspan + cilkscale_timer_t::burden
      > p_bottom.lchild_bspan)
    p_bottom.lchild_bspan = c_bottom.contin_bspan + cilkscale_timer_t::burden;
}

CILKTOOL_API
void __csi_detach_continue(const csi_id_t detach_continue_id,
                           const csi_id_t detach_id,
                           const detach_continue_prop_t prop) {
  // In the continuation
  shadow_stack_t &stack = STACK;

#if TRACE_CALLS
  fprintf(stderr, "[W%d] detach_continue(%ld, %ld, %ld)\n",
          __cilkrts_get_worker_number(), detach_continue_id, detach_id, prop);
#endif

  shadow_stack_frame_t &bottom = stack.peek_bot();

  if (prop.is_unwind) {
    // In opencilk, upon reaching the unwind destination of a detach, all
    // spawned child computations have been synced.  Hence we replicate the
    // logic from after_sync here to compute work and span.

    // Add achild_work to contin_work, and reset contin_work.
    bottom.contin_work += bottom.achild_work;
    bottom.achild_work = cilk_time_t::zero();

    // Select the largest of lchild_span and contin_span, and then reset
    // lchild_span.
    if (bottom.lchild_span > bottom.contin_span)
      bottom.contin_span = bottom.lchild_span;
    bottom.lchild_span = cilk_time_t::zero();

    if (bottom.lchild_bspan > bottom.contin_bspan)
      bottom.contin_bspan = bottom.lchild_bspan;
    bottom.lchild_bspan = cilk_time_t::zero();
  } else {
    bottom.contin_bspan += cilkscale_timer_t::burden;
  }

  stack.start.gettime();
}

CILKTOOL_API
void __csi_before_sync(const csi_id_t sync_id, const int32_t *has_spawned) {
  shadow_stack_t &stack = STACK;

  stack.stop.gettime();

#if TRACE_CALLS
  fprintf(stderr, "[W%d] before_sync(%ld)\n", __cilkrts_get_worker_number(),
          sync_id);
#endif

  shadow_stack_frame_t &bottom = stack.peek_bot();

  duration_t strand_time = elapsed_time(&(stack.stop), &(stack.start));
  bottom.contin_work += strand_time;
  bottom.contin_span += strand_time;
  bottom.contin_bspan += strand_time;
}

CILKTOOL_API
void __csi_after_sync(const csi_id_t sync_id, const int32_t *has_spawned) {
  shadow_stack_t &stack = STACK;

#if TRACE_CALLS
  fprintf(stderr, "[W%d] after_sync(%ld)\n", __cilkrts_get_worker_number(),
          sync_id);
#endif

  shadow_stack_frame_t &bottom = stack.peek_bot();
  // Update the work and span recorded for the bottom-most frame on the stack.

  // Add achild_work to contin_work, and reset contin_work.
  bottom.contin_work += bottom.achild_work;
  bottom.achild_work = cilk_time_t::zero();

  // Select the largest of lchild_span and contin_span, and then reset
  // lchild_span.
  if (bottom.lchild_span > bottom.contin_span)
    bottom.contin_span = bottom.lchild_span;
  bottom.lchild_span = cilk_time_t::zero();

  if (bottom.lchild_bspan > bottom.contin_bspan)
    bottom.contin_bspan = bottom.lchild_bspan;
  bottom.lchild_bspan = cilk_time_t::zero();

  stack.start.gettime();
}

///////////////////////////////////////////////////////////////////////////
// Probes and associated routines

CILKTOOL_API wsp_t wsp_getworkspan() CILKSCALE_NOTHROW {
  shadow_stack_t &stack = STACK;

  stack.stop.gettime();

#if TRACE_CALLS
  fprintf(stderr, "getworkspan()\n");
#endif
  shadow_stack_frame_t &bottom = stack.peek_bot();

  duration_t strand_time = elapsed_time(&(stack.stop), &(stack.start));
  bottom.contin_work += strand_time;
  bottom.contin_span += strand_time;
  bottom.contin_bspan += strand_time;

  wsp_t result = {stack.peek_bot().contin_work.get_raw_duration(),
                  stack.peek_bot().contin_span.get_raw_duration(),
                  stack.peek_bot().contin_bspan.get_raw_duration()};

  // Because of the high overhead of calling gettime(), especially compared to
  // the running time of the operations in this hook, the work and span
  // measurements appear more stable if we simply use the recorded time as the
  // new start time.
  stack.start = stack.stop;

  return result;
}

__attribute__((visibility("default"))) wsp_t &
operator+=(wsp_t &lhs, const wsp_t &rhs) noexcept {
  lhs.work += rhs.work;
  lhs.span += rhs.span;
  lhs.bspan += rhs.bspan;
  return lhs;
}

__attribute__((visibility("default"))) wsp_t &
operator-=(wsp_t &lhs, const wsp_t &rhs) noexcept {
  lhs.work -= rhs.work;
  lhs.span -= rhs.span;
  lhs.bspan -= rhs.bspan;
  return lhs;
}

__attribute__((visibility("default"))) std::ostream &
operator<<(std::ostream &OS, const wsp_t &pt) {
  shadow_stack_t &stack = STACK;

  stack.stop.gettime();

  shadow_stack_frame_t &bottom = stack.peek_bot();

  duration_t strand_time = elapsed_time(&(stack.stop), &(stack.start));
  bottom.contin_work += strand_time;
  bottom.contin_span += strand_time;
  bottom.contin_bspan += strand_time;

  cilk_time_t work = cilk_time_t(pt.work);
  cilk_time_t span = cilk_time_t(pt.span);
  cilk_time_t bspan = cilk_time_t(pt.bspan);
  OS << work << ", " << span << ", " << work.get_val_d() / span.get_val_d()
     << ", " << bspan << ", " << work.get_val_d() / bspan.get_val_d();

  stack.start.gettime();
  return OS;
}

__attribute__((visibility("default"))) std::ofstream &
operator<<(std::ofstream &OS, const wsp_t &pt) {
  shadow_stack_t &stack = STACK;

  stack.stop.gettime();

  shadow_stack_frame_t &bottom = stack.peek_bot();

  duration_t strand_time = elapsed_time(&(stack.stop), &(stack.start));
  bottom.contin_work += strand_time;
  bottom.contin_span += strand_time;
  bottom.contin_bspan += strand_time;

  cilk_time_t work = cilk_time_t(pt.work);
  cilk_time_t span = cilk_time_t(pt.span);
  cilk_time_t bspan = cilk_time_t(pt.bspan);
  OS << work << ", " << span << ", " << work.get_val_d() / span.get_val_d()
     << ", " << bspan << ", " << work.get_val_d() / bspan.get_val_d();

  stack.start.gettime();
  return OS;
}

CILKTOOL_API wsp_t wsp_add(wsp_t lhs, wsp_t rhs) CILKSCALE_NOTHROW {
  lhs.work += rhs.work;
  lhs.span += rhs.span;
  lhs.bspan += rhs.bspan;
  return lhs;
}

CILKTOOL_API wsp_t wsp_sub(wsp_t lhs, wsp_t rhs) CILKSCALE_NOTHROW {
  lhs.work -= rhs.work;
  lhs.span -= rhs.span;
  lhs.bspan -= rhs.bspan;
  return lhs;
}

CILKTOOL_API void wsp_dump(wsp_t wsp, const char *tag) {
  shadow_stack_t &stack = STACK;

  stack.stop.gettime();

  shadow_stack_frame_t &bottom = stack.peek_bot();

  duration_t strand_time = elapsed_time(&(stack.stop), &(stack.start));
  bottom.contin_work += strand_time;
  bottom.contin_span += strand_time;
  bottom.contin_bspan += strand_time;

  ensure_header(OUTPUT);
  print_results(OUTPUT, tag, cilk_time_t(wsp.work), cilk_time_t(wsp.span),
                cilk_time_t(wsp.bspan));

  stack.start.gettime();
}
