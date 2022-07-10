#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Ensure that __cilkscale__ is defined, so we can provide a nontrivial
// definition of getworkspan().
#ifndef __cilkscale__
#define __cilkscale__
#endif

#include "cilkscale_timer.h"
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

#include <cilk/cilk_api.h>
#if !SERIAL_TOOL
#include <cilk/ostream_reducer.h>
using out_reducer = cilk::ostream_reducer<char>;
#endif

///////////////////////////////////////////////////////////////////////////
// Data structures for timing.

#if !SERIAL_TOOL
// Simple reducer for a cilkscale_timer.
//
// This reducer ensures that each stolen subcomputation gets a separate
// cilkscale_timer object for probing the computation.
static void timer_identity(void *view) {
  new (view) cilkscale_timer_t();
}
static void timer_reduce(void *left, void *right) {
  static_cast<cilkscale_timer_t *>(right)->~cilkscale_timer_t();
}

using cilkscale_timer_reducer =
  cilkscale_timer_t _Hyperobject(timer_identity, timer_reduce);
  
#endif

// Suppress diagnostic warning that reducer callbacks are not implemented for
// structure members.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcilk-ignored"

// Top-level class to manage the state of the global benchmarking tool.  This
// class interface allows the tool to initialize data structures, such as a
// std::ostream and a std::ofstream, only after the standard libraries they rely
// on have been initialized, and to destroy those structures before those
// libraries are deinitialized.
class BenchmarkImpl_t {
public:
  // Timer for tracking execution time.
  cilkscale_timer_t start, stop;
#if SERIAL_TOOL
  cilkscale_timer_t timer;
#else
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcilk-ignored"
  cilkscale_timer_reducer timer;
#pragma clang diagnostic pop
#endif

  std::ostream &outs = std::cout;
  std::ofstream outf;
#if !SERIAL_TOOL
  out_reducer *outf_red = nullptr;
#endif

  std::basic_ostream<char> *out_view() {
#if !SERIAL_TOOL
    // TODO: The compiler does not correctly bind the hyperobject
    // type to a reference, otherwise a reference return value would
    // be more conventional C++.
    if (outf_red)
      return &*outf_red;
#endif
    if (outf.is_open())
      return &outf;
    return &outs;
  }

  BenchmarkImpl_t();
  ~BenchmarkImpl_t();
};

#pragma clang diagnostic pop

// Top-level benchmarking tool.
static BenchmarkImpl_t *create_tool(void) {
  if (!__cilkrts_is_initialized())
    // If the OpenCilk runtime is not yet initialized, then csi_init will
    // register a call to init_tool to initialize the tool after the runtime is
    // initialized.
    return nullptr;

  // Otherwise, ordered dynamic initalization should ensure that it's safe to
  // create the tool.
  return new BenchmarkImpl_t();
}
static BenchmarkImpl_t *tool = create_tool();

static bool TOOL_INITIALIZED = false;

///////////////////////////////////////////////////////////////////////////
// Routines to results

// Ensure that a proper header has been emitted to OS.
template<class Out>
static void ensure_header(Out &OS) {
  static bool PRINT_STARTED = false;
  if (PRINT_STARTED)
    return;

  OS << "tag,time (" << cilk_time_t::units << ")\n";

  PRINT_STARTED = true;
}

// Emit the given results to OS.
template<class Out>
static void print_results(Out &OS, const char *tag, cilk_time_t time) {
  OS << tag << "," << time << "\n";
}

// Emit the results from the overall program execution to the proper output
// stream.
static void print_analysis(void) {
  assert(TOOL_INITIALIZED);

  std::basic_ostream<char> &output = *tool->out_view();
  ensure_header(output);
  print_results(output, "", elapsed_time(&tool->stop, &tool->start));
}

///////////////////////////////////////////////////////////////////////////
// Startup and shutdown the tool

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

BenchmarkImpl_t::BenchmarkImpl_t() {
  const char *envstr = getenv("CILKSCALE_OUT");
  if (envstr)
    outf.open(envstr);

#if !SERIAL_TOOL
  __cilkrts_reducer_register
    (&timer, sizeof timer, timer_identity, timer_reduce);

  outf_red = new out_reducer((outf.is_open() ? outf : outs));
  __cilkrts_reducer_register
    (outf_red, sizeof *outf_red,
     &cilk::ostream_view<char, std::char_traits<char>>::identity,
     &cilk::ostream_view<char, std::char_traits<char>>::reduce);
#endif

  start.gettime();
}

BenchmarkImpl_t::~BenchmarkImpl_t() {
  stop.gettime();
  print_analysis();
  
  if (outf.is_open())
    outf.close();

#if !SERIAL_TOOL
  __cilkrts_reducer_unregister(outf_red);
  delete outf_red;
  outf_red = nullptr;
  __cilkrts_reducer_unregister(&timer);
#endif
}

#pragma clang diagnostic pop

///////////////////////////////////////////////////////////////////////////
// Hooks for operating the tool.

// Custom function to intialize tool after the OpenCilk runtime is initialized.
static void init_tool(void) {
  assert(nullptr == tool && "Tool already initialized");
  tool = new BenchmarkImpl_t();
}

static void destroy_tool(void) {
  if (tool) {
    delete tool;
    tool = nullptr;
  }

  TOOL_INITIALIZED = false;
}

CILKTOOL_API void __csi_init() {
#if TRACE_CALLS
  fprintf(stderr, "__csi_init()\n");
#endif

  if (!__cilkrts_is_initialized())
    __cilkrts_atinit(init_tool);

  __cilkrts_atexit(destroy_tool);

  TOOL_INITIALIZED = true;
}

CILKTOOL_API void __csi_unit_init(const char *const file_name,
                                  const instrumentation_counts_t counts) {
  return;
}

///////////////////////////////////////////////////////////////////////////
// Probes and associated routines

CILKTOOL_API wsp_t wsp_getworkspan() CILKSCALE_NOTHROW {
  if (!tool)
    return wsp_zero();

  tool->timer.gettime();
  duration_t time_since_start = elapsed_time(&tool->timer, &tool->start);
  wsp_t result = {cilk_time_t(time_since_start).get_raw_duration(), 0, 0};

  return result;
}

__attribute__((visibility("default"))) wsp_t &
operator+=(wsp_t &lhs, const wsp_t &rhs) noexcept {
  lhs.work += rhs.work;
  return lhs;
}

__attribute__((visibility("default"))) wsp_t &
operator-=(wsp_t &lhs, const wsp_t &rhs) noexcept {
  lhs.work -= rhs.work;
  return lhs;
}

__attribute__((visibility("default"))) std::ostream &
operator<<(std::ostream &OS, const wsp_t &pt) {
  OS << cilk_time_t(pt.work);
  return OS;
}

__attribute__((visibility("default"))) std::ofstream &
operator<<(std::ofstream &OS, const wsp_t &pt) {
  OS << cilk_time_t(pt.work);
  return OS;
}

CILKTOOL_API wsp_t wsp_add(wsp_t lhs, wsp_t rhs) CILKSCALE_NOTHROW {
  lhs.work += rhs.work;
  return lhs;
}

CILKTOOL_API wsp_t wsp_sub(wsp_t lhs, wsp_t rhs) CILKSCALE_NOTHROW {
  lhs.work -= rhs.work;
  return lhs;
}

CILKTOOL_API void wsp_dump(wsp_t wsp, const char *tag) {
  std::basic_ostream<char> &output = *tool->out_view();
  ensure_header(output);
  print_results(output, tag, cilk_time_t(wsp.work));
}
