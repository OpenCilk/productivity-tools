// Ensure that __cilkscale__ is defined, so we can provide a nontrivial
// definition of getworkspan().
#ifndef __cilkscale__
#define __cilkscale__
#endif

#include "shadow_stack.h"
#include <cassert>
#include <cilk/cilk_api.h>
#include <csi/csi.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>

#define CILKTOOL_VISIBLE __attribute__((visibility("default")))
#define CILKTOOL_API extern "C" CILKTOOL_VISIBLE

#ifndef SERIAL_TOOL
#define SERIAL_TOOL 1
#endif

#ifndef TRACE_CALLS
#define TRACE_CALLS 0
#endif

#if SERIAL_TOOL
FILE *err_io = stderr;
#else // !SERIAL_TOOL
#include <cilk/cilk_api.h>
#include <cilk/ostream_reducer.h>
using out_reducer = cilk::ostream_reducer<char>;
#endif // SERIAL_TOOL

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
class CilkscaleImplT {
public:
  // Shadow-stack data structure, for managing work-span variables.
  shadow_stack_reducer ShadowStack;

  // Output stream for printing results.
  std::ostream &Outs = std::cout;
  std::ofstream Outf;
#if !SERIAL_TOOL
  out_reducer OutfRed;
#endif

  std::basic_ostream<char> &out_view() {
#if !SERIAL_TOOL
    return OutfRed;
#endif
    if (Outf.is_open())
      return Outf;
    return Outs;
  }

  CilkscaleImplT(const char *OutputFilename = nullptr);
  ~CilkscaleImplT();
};

// Top-level Cilkscale tool.
static CilkscaleImplT *createTool(void) {
  // Ordered dynamic initalization should ensure that it's safe to create the
  // tool.
  return new CilkscaleImplT(getenv("CILKSCALE_OUT"));
}
static CilkscaleImplT *Tool = createTool();

bool CILKSCALE_INITIALIZED = false;

///////////////////////////////////////////////////////////////////////////
// Utilities for printing analysis results

// Ensure that a proper header has been emitted to OS.
template <class Out> static void ensureHeader(Out &OS) {
  static bool PrintStarted = false;
  if (PrintStarted)
    return;

  OS << "tag,work (" << cilk_time_t::units << ")"
     << ",span (" << cilk_time_t::units << ")"
     << ",parallelism"
     << ",burdened_span (" << cilk_time_t::units << ")"
     << ",burdened_parallelism\n";

  PrintStarted = true;
}

// Emit the given results to OS.
template <class Out>
static void printResults(Out &OS, const char *Tag, cilk_time_t Work,
                         cilk_time_t Span, cilk_time_t BSpan) {
  OS << Tag << "," << Work << "," << Span << ","
     << Work.get_val_d() / Span.get_val_d() << "," << BSpan << ","
     << Work.get_val_d() / BSpan.get_val_d() << "\n";
}

// Emit the results from the overall program execution to the proper output
// stream.
static void printAnalysis(void) {
  assert(CILKSCALE_INITIALIZED);
  shadow_stack_frame_t &Bottom = Tool->ShadowStack.peek_bot();

  assert(frame_type::NONE != Bottom.type);

  cilk_time_t Work = Bottom.contin_work;
  cilk_time_t Span = Bottom.contin_span;
  cilk_time_t BSpan = Bottom.contin_bspan;

  std::basic_ostream<char> &OS = Tool->out_view();
  ensureHeader(OS);
  printResults(OS, "", Work, Span, BSpan);
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

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

CilkscaleImplT::CilkscaleImplT(const char *OutputFilename)
    : Outf(OutputFilename)
#if !SERIAL_TOOL
      ,
      OutfRed(Outf.is_open() ? Outf : Outs)
#endif
{
  // TODO: Verify that this push() is not necessary.
  // shadow_stack.push(frame_type::SPAWNER);
  ShadowStack.start.gettime();
}

CilkscaleImplT::~CilkscaleImplT() {
  ShadowStack.stop.gettime();
  shadow_stack_frame_t &Bottom = ShadowStack.peek_bot();

  duration_t StrandTime = ShadowStack.elapsed_time();
  Bottom.contin_work += StrandTime;
  Bottom.contin_span += StrandTime;
  Bottom.contin_bspan += StrandTime;

  printAnalysis();

  if (Outf.is_open())
    Outf.close();
}

#pragma clang diagnostic pop

///////////////////////////////////////////////////////////////////////////
// Hooks for operating the tool.

static void destroyTool(void) {
  if (Tool) {
    delete Tool;
    Tool = nullptr;
  }

  CILKSCALE_INITIALIZED = false;
}

CILKTOOL_API void __csi_init() {
#if TRACE_CALLS
  fprintf(stderr, "__csi_init()\n");
#endif

  atexit(destroyTool);

#if SERIAL_TOOL
  ensure_serial_tool();
#endif

  CILKSCALE_INITIALIZED = true;
}

CILKTOOL_API void __csi_unit_init(const char *const FileName,
                                  const instrumentation_counts_t Counts) {
  return;
}

CILKTOOL_API
void __csi_bb_entry(const csi_id_t bb_id, const bb_prop_t prop) {
  if (!CILKSCALE_INITIALIZED)
    return;

  shadow_stack_frame_t &Bottom = Tool->ShadowStack.peek_bot();
  get_bb_time(&Bottom.contin_work, &Bottom.contin_span, &Bottom.contin_bspan,
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

  Tool->ShadowStack.stop.gettime();

#if TRACE_CALLS
  fprintf(stderr, "[W%d] func_entry(%ld)\n", __cilkrts_get_worker_number(),
          func_id);
#endif

  shadow_stack_frame_t &Bottom = Tool->ShadowStack.peek_bot();

  duration_t StrandTime = Tool->ShadowStack.elapsed_time();
  Bottom.contin_work += StrandTime;
  Bottom.contin_span += StrandTime;
  Bottom.contin_bspan += StrandTime;

  shadow_stack_frame_t &PBottom = Tool->ShadowStack.peek_bot();
  cilk_time_t PContinWork = PBottom.contin_work;
  cilk_time_t PContinSpan = PBottom.contin_span;
  cilk_time_t PContinBSpan = PBottom.contin_bspan;

  // Push new frame onto the stack
  shadow_stack_frame_t &CBottom = Tool->ShadowStack.push(frame_type::SPAWNER);
  CBottom.contin_work = PContinWork;
  CBottom.contin_span = PContinSpan;
  CBottom.contin_bspan = PContinBSpan;

  // Because of the high overhead of calling gettime(), especially compared to
  // the running time of the operations in this hook, the work and span
  // measurements appear more stable if we simply use the recorded time as the
  // new start time.
  Tool->ShadowStack.start = Tool->ShadowStack.stop;
}

CILKTOOL_API
void __csi_func_exit(const csi_id_t func_exit_id, const csi_id_t func_id,
                     const func_exit_prop_t prop) {
  if (!CILKSCALE_INITIALIZED)
    return;
  if (!prop.may_spawn)
    return;

  Tool->ShadowStack.stop.gettime();

#if TRACE_CALLS
  fprintf(stderr, "[W%d] func_exit(%ld)\n", __cilkrts_get_worker_number(),
          func_id);
#endif

  duration_t StrandTime = Tool->ShadowStack.elapsed_time();

  assert(cilk_time_t::zero() == Tool->ShadowStack.peek_bot().lchild_span);
  assert(cilk_time_t::zero() == Tool->ShadowStack.peek_bot().achild_work);

  // Pop the stack
  shadow_stack_frame_t &CBottom = Tool->ShadowStack.pop();
  shadow_stack_frame_t &PBottom = Tool->ShadowStack.peek_bot();

  PBottom.contin_work = CBottom.contin_work + StrandTime;
  PBottom.contin_span = CBottom.contin_span + StrandTime;
  PBottom.contin_bspan = CBottom.contin_bspan + StrandTime;

  // Because of the high overhead of calling gettime(), especially compared to
  // the running time of the operations in this hook, the work and span
  // measurements appear more stable if we simply use the recorded time as the
  // new start time.
  Tool->ShadowStack.start = Tool->ShadowStack.stop;
}

CILKTOOL_API
void __csi_detach(const csi_id_t detach_id, const unsigned sync_reg,
                  const detach_prop_t prop) {
  Tool->ShadowStack.stop.gettime();

#if TRACE_CALLS
  fprintf(stderr, "[W%d] detach(%ld)\n", __cilkrts_get_worker_number(),
          detach_id);
#endif

  shadow_stack_frame_t &Bottom = Tool->ShadowStack.peek_bot();

  duration_t StrandTime = Tool->ShadowStack.elapsed_time();
  Bottom.contin_work += StrandTime;
  Bottom.contin_span += StrandTime;
  Bottom.contin_bspan += StrandTime;
}

CILKTOOL_API
void __csi_task(const csi_id_t task_id, const csi_id_t detach_id,
                const task_prop_t prop) {
#if TRACE_CALLS
  fprintf(stderr, "[W%d] task(%ld, %ld)\n", __cilkrts_get_worker_number(),
          task_id, detach_id);
#endif

  shadow_stack_frame_t &PBottom = Tool->ShadowStack.peek_bot();
  cilk_time_t PContinWork = PBottom.contin_work;
  cilk_time_t PContinSpan = PBottom.contin_span;
  cilk_time_t PContinBSpan = PBottom.contin_bspan;

  // Push new frame onto the stack.
  shadow_stack_frame_t &CBottom = Tool->ShadowStack.push(frame_type::HELPER);
  CBottom.contin_work = PContinWork;
  CBottom.contin_span = PContinSpan;
  CBottom.contin_bspan = PContinBSpan;

  Tool->ShadowStack.start.gettime();
}

CILKTOOL_API
void __csi_task_exit(const csi_id_t task_exit_id, const csi_id_t task_id,
                     const csi_id_t detach_id, const unsigned sync_reg,
                     const task_exit_prop_t prop) {
  Tool->ShadowStack.stop.gettime();

#if TRACE_CALLS
  fprintf(stderr, "[W%d] task_exit(%ld, %ld, %ld)\n",
          __cilkrts_get_worker_number(), task_exit_id, task_id, detach_id);
#endif

  shadow_stack_frame_t &Bottom = Tool->ShadowStack.peek_bot();

  duration_t StrandTime = Tool->ShadowStack.elapsed_time();
  Bottom.contin_work += StrandTime;
  Bottom.contin_span += StrandTime;
  Bottom.contin_bspan += StrandTime;

  assert(cilk_time_t::zero() == Bottom.lchild_span);

  // Pop the stack
  shadow_stack_frame_t &CBottom = Tool->ShadowStack.pop();
  shadow_stack_frame_t &PBottom = Tool->ShadowStack.peek_bot();
  PBottom.achild_work += CBottom.contin_work - PBottom.contin_work;
  // Check if the span of c_bottom exceeds that of the previous longest child.
  if (CBottom.contin_span > PBottom.lchild_span)
    PBottom.lchild_span = CBottom.contin_span;
  if (CBottom.contin_bspan + cilkscale_timer_t::burden > PBottom.lchild_bspan)
    PBottom.lchild_bspan = CBottom.contin_bspan + cilkscale_timer_t::burden;
}

CILKTOOL_API
void __csi_detach_continue(const csi_id_t detach_continue_id,
                           const csi_id_t detach_id, const unsigned sync_reg,
                           const detach_continue_prop_t prop) {
  // In the continuation
#if TRACE_CALLS
  fprintf(stderr, "[W%d] detach_continue(%ld, %ld, %ld)\n",
          __cilkrts_get_worker_number(), detach_continue_id, detach_id, prop);
#endif

  shadow_stack_frame_t &Bottom = Tool->ShadowStack.peek_bot();

  if (prop.is_unwind) {
    // In opencilk, upon reaching the unwind destination of a detach, all
    // spawned child computations have been synced.  Hence we replicate the
    // logic from after_sync here to compute work and span.

    // Add achild_work to contin_work, and reset contin_work.
    Bottom.contin_work += Bottom.achild_work;
    Bottom.achild_work = cilk_time_t::zero();

    // Select the largest of lchild_span and contin_span, and then reset
    // lchild_span.
    if (Bottom.lchild_span > Bottom.contin_span)
      Bottom.contin_span = Bottom.lchild_span;
    Bottom.lchild_span = cilk_time_t::zero();

    if (Bottom.lchild_bspan > Bottom.contin_bspan)
      Bottom.contin_bspan = Bottom.lchild_bspan;
    Bottom.lchild_bspan = cilk_time_t::zero();
  } else {
    Bottom.contin_bspan += cilkscale_timer_t::burden;
  }

  Tool->ShadowStack.start.gettime();
}

CILKTOOL_API
void __csi_before_sync(const csi_id_t sync_id, const unsigned sync_reg) {
  Tool->ShadowStack.stop.gettime();

#if TRACE_CALLS
  fprintf(stderr, "[W%d] before_sync(%ld, %d)\n", __cilkrts_get_worker_number(),
          sync_id, sync_reg);
#endif

  shadow_stack_frame_t &Bottom = Tool->ShadowStack.peek_bot();

  duration_t StrandTime = Tool->ShadowStack.elapsed_time();
  Bottom.contin_work += StrandTime;
  Bottom.contin_span += StrandTime;
  Bottom.contin_bspan += StrandTime;
}

CILKTOOL_API
void __csi_after_sync(const csi_id_t sync_id, const unsigned sync_reg) {
#if TRACE_CALLS
  fprintf(stderr, "[W%d] after_sync(%ld, %d)\n", __cilkrts_get_worker_number(),
          sync_id, sync_reg);
#endif

  shadow_stack_frame_t &Bottom = Tool->ShadowStack.peek_bot();
  // Update the work and span recorded for the bottom-most frame on the stack.

  // Add achild_work to contin_work, and reset achild_work.
  Bottom.contin_work += Bottom.achild_work;
  Bottom.achild_work = cilk_time_t::zero();

  // Select the largest of lchild_span and contin_span, and then reset
  // lchild_span.
  if (Bottom.lchild_span > Bottom.contin_span)
    Bottom.contin_span = Bottom.lchild_span;
  Bottom.lchild_span = cilk_time_t::zero();

  if (Bottom.lchild_bspan > Bottom.contin_bspan)
    Bottom.contin_bspan = Bottom.lchild_bspan;
  Bottom.lchild_bspan = cilk_time_t::zero();

  Tool->ShadowStack.start.gettime();
}

///////////////////////////////////////////////////////////////////////////
// Probes and associated routines

CILKTOOL_API wsp_t wsp_getworkspan() CILKSCALE_NOTHROW {
  Tool->ShadowStack.stop.gettime();

#if TRACE_CALLS
  fprintf(stderr, "getworkspan()\n");
#endif
  shadow_stack_frame_t &Bottom = Tool->ShadowStack.peek_bot();

  duration_t StrandTime = Tool->ShadowStack.elapsed_time();
  Bottom.contin_work += StrandTime;
  Bottom.contin_span += StrandTime;
  Bottom.contin_bspan += StrandTime;

  wsp_t Result = {Tool->ShadowStack.peek_bot().contin_work.get_raw_duration(),
                  Tool->ShadowStack.peek_bot().contin_span.get_raw_duration(),
                  Tool->ShadowStack.peek_bot().contin_bspan.get_raw_duration()};

  // Because of the high overhead of calling gettime(), especially compared to
  // the running time of the operations in this hook, the work and span
  // measurements appear more stable if we simply use the recorded time as the
  // new start time.
  Tool->ShadowStack.start = Tool->ShadowStack.stop;

  return Result;
}

CILKTOOL_VISIBLE wsp_t &operator+=(wsp_t &Lhs, const wsp_t &Rhs) noexcept {
  Lhs.work += Rhs.work;
  Lhs.span += Rhs.span;
  Lhs.bspan += Rhs.bspan;
  return Lhs;
}

CILKTOOL_VISIBLE wsp_t &operator-=(wsp_t &Lhs, const wsp_t &Rhs) noexcept {
  Lhs.work -= Rhs.work;
  Lhs.span -= Rhs.span;
  Lhs.bspan -= Rhs.bspan;
  return Lhs;
}

CILKTOOL_VISIBLE std::ostream &operator<<(std::ostream &OS, const wsp_t &pt) {
  Tool->ShadowStack.stop.gettime();

  shadow_stack_frame_t &Bottom = Tool->ShadowStack.peek_bot();

  duration_t StrandTime = Tool->ShadowStack.elapsed_time();
  Bottom.contin_work += StrandTime;
  Bottom.contin_span += StrandTime;
  Bottom.contin_bspan += StrandTime;

  cilk_time_t Work = cilk_time_t(pt.work);
  cilk_time_t Span = cilk_time_t(pt.span);
  cilk_time_t BSpan = cilk_time_t(pt.bspan);
  OS << Work << ", " << Span << ", " << Work.get_val_d() / Span.get_val_d()
     << ", " << BSpan << ", " << Work.get_val_d() / BSpan.get_val_d();

  Tool->ShadowStack.start.gettime();
  return OS;
}

CILKTOOL_VISIBLE std::ofstream &operator<<(std::ofstream &OS, const wsp_t &pt) {
  Tool->ShadowStack.stop.gettime();

  shadow_stack_frame_t &Bottom = Tool->ShadowStack.peek_bot();

  duration_t StrandTime = Tool->ShadowStack.elapsed_time();
  Bottom.contin_work += StrandTime;
  Bottom.contin_span += StrandTime;
  Bottom.contin_bspan += StrandTime;

  cilk_time_t Work = cilk_time_t(pt.work);
  cilk_time_t Span = cilk_time_t(pt.span);
  cilk_time_t BSpan = cilk_time_t(pt.bspan);
  OS << Work << ", " << Span << ", " << Work.get_val_d() / Span.get_val_d()
     << ", " << BSpan << ", " << Work.get_val_d() / BSpan.get_val_d();

  Tool->ShadowStack.start.gettime();
  return OS;
}

CILKTOOL_API wsp_t wsp_add(wsp_t Lhs, wsp_t Rhs) CILKSCALE_NOTHROW {
  Lhs.work += Rhs.work;
  Lhs.span += Rhs.span;
  Lhs.bspan += Rhs.bspan;
  return Lhs;
}

CILKTOOL_API wsp_t wsp_sub(wsp_t Lhs, wsp_t Rhs) CILKSCALE_NOTHROW {
  Lhs.work -= Rhs.work;
  Lhs.span -= Rhs.span;
  Lhs.bspan -= Rhs.bspan;
  return Lhs;
}

CILKTOOL_API void wsp_dump(wsp_t Wsp, const char *Tag) {
  Tool->ShadowStack.stop.gettime();

  shadow_stack_frame_t &Bottom = Tool->ShadowStack.peek_bot();

  duration_t StrandTime = Tool->ShadowStack.elapsed_time();
  Bottom.contin_work += StrandTime;
  Bottom.contin_span += StrandTime;
  Bottom.contin_bspan += StrandTime;

  std::basic_ostream<char> &OS = Tool->out_view();
  ensureHeader(OS);
  printResults(OS, Tag, cilk_time_t(Wsp.work), cilk_time_t(Wsp.span),
               cilk_time_t(Wsp.bspan));

  Tool->ShadowStack.start.gettime();
}
