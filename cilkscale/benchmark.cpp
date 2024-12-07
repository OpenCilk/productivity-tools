// Ensure that __cilkscale__ is defined, so we can provide a nontrivial
// definition of getworkspan().
#ifndef __cilkscale__
#define __cilkscale__
#endif

#include "cilkscale_timer.h"
#include <cassert>
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
#include <cilk/cilk_stub.h>
#else // !SERIAL_TOOL
#include <cilk/cilk.h>
#include <cilk/ostream_reducer.h>
using out_reducer = cilk::ostream_reducer<char>;
#endif // SERIAL_TOOL

///////////////////////////////////////////////////////////////////////////
// Data structures for timing.

#if SERIAL_TOOL
using cilkscale_timer_reducer = cilkscale_timer_t;
#else
// Simple reducer for a cilkscale_timer.
//
// This reducer ensures that each stolen subcomputation gets a separate
// cilkscale_timer object for probing the computation.
static void timerIdentity(void *View) { new (View) cilkscale_timer_t(); }
static void timerReduce(void *Left, void *Right) {
  static_cast<cilkscale_timer_t *>(Right)->~cilkscale_timer_t();
}

using cilkscale_timer_reducer = cilkscale_timer_t cilk_reducer(timerIdentity,
                                                               timerReduce);

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
class BenchmarkImplT {
public:
  // Timer for tracking execution time.
  cilkscale_timer_t Start, Stop;
#if SERIAL_TOOL
  cilkscale_timer_t timer;
#else
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcilk-ignored"
  cilkscale_timer_reducer Timer;
#pragma clang diagnostic pop
#endif

  std::ostream &Outs = std::cout;
  std::ofstream Outf;
#if !SERIAL_TOOL
  out_reducer OutfRed;
#endif

  std::basic_ostream<char> &outView() {
#if !SERIAL_TOOL
    return OutfRed;
#endif
    if (Outf.is_open())
      return Outf;
    return Outs;
  }

  BenchmarkImplT(const char *OutputFilename = nullptr);
  ~BenchmarkImplT();
};

#pragma clang diagnostic pop

// Top-level benchmarking tool.
static BenchmarkImplT *createTool(void) {
  // Ordered dynamic initalization should ensure that it's safe to create the
  // tool.
  return new BenchmarkImplT(getenv("CILKSCALE_OUT"));
}
static BenchmarkImplT *Tool = createTool();

static bool CILKSCALE_BENCHMARK_INITIALIZED = false;

///////////////////////////////////////////////////////////////////////////
// Routines to results

// Ensure that a proper header has been emitted to OS.
template <class Out> static void ensureHeader(Out &OS) {
  static bool PrintStarted = false;
  if (PrintStarted)
    return;

  OS << "tag,time (" << cilk_time_t::units << ")\n";

  PrintStarted = true;
}

// Emit the given results to OS.
template <class Out>
static void printResults(Out &OS, const char *Tag, cilk_time_t Time) {
  OS << Tag << "," << Time << "\n";
}

// Emit the results from the overall program execution to the proper output
// stream.
static void printAnalysis(void) {
  assert(CILKSCALE_BENCHMARK_INITIALIZED);

  std::basic_ostream<char> &OS = Tool->outView();
  ensureHeader(OS);
  printResults(OS, "", elapsed_time(&Tool->Stop, &Tool->Start));
}

///////////////////////////////////////////////////////////////////////////
// Startup and shutdown the tool

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

BenchmarkImplT::BenchmarkImplT(const char *OutputFilename)
    : Outf(OutputFilename)
#if !SERIAL_TOOL
      ,
      OutfRed(Outf.is_open() ? Outf : Outs)
#endif
{
  Start.gettime();
}

BenchmarkImplT::~BenchmarkImplT() {
  Stop.gettime();
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

  CILKSCALE_BENCHMARK_INITIALIZED = false;
}

CILKTOOL_API void __csi_init() {
#if TRACE_CALLS
  fprintf(stderr, "__csi_init()\n");
#endif

  atexit(destroyTool);

  CILKSCALE_BENCHMARK_INITIALIZED = true;
}

CILKTOOL_API void __csi_unit_init(const char *const FileName,
                                  const instrumentation_counts_t Counts) {
  return;
}

///////////////////////////////////////////////////////////////////////////
// Probes and associated routines

CILKTOOL_API wsp_t wsp_getworkspan() CILKSCALE_NOTHROW {
  if (!Tool)
    return wsp_zero();

  Tool->Timer.gettime();
  duration_t TimeSinceStart = elapsed_time(&Tool->Timer, &Tool->Start);
  wsp_t Result = {cilk_time_t(TimeSinceStart).get_raw_duration(), 0, 0};

  return Result;
}

CILKTOOL_VISIBLE wsp_t &operator+=(wsp_t &Lhs, const wsp_t &Rhs) noexcept {
  Lhs.work += Rhs.work;
  return Lhs;
}

CILKTOOL_VISIBLE wsp_t &operator-=(wsp_t &Lhs, const wsp_t &Rhs) noexcept {
  Lhs.work -= Rhs.work;
  return Lhs;
}

CILKTOOL_VISIBLE std::ostream &operator<<(std::ostream &OS, const wsp_t &Pt) {
  OS << cilk_time_t(Pt.work);
  return OS;
}

CILKTOOL_VISIBLE std::ofstream &operator<<(std::ofstream &OS, const wsp_t &Pt) {
  OS << cilk_time_t(Pt.work);
  return OS;
}

CILKTOOL_API wsp_t wsp_add(wsp_t Lhs, wsp_t Rhs) CILKSCALE_NOTHROW {
  Lhs.work += Rhs.work;
  return Lhs;
}

CILKTOOL_API wsp_t wsp_sub(wsp_t Lhs, wsp_t Rhs) CILKSCALE_NOTHROW {
  Lhs.work -= Rhs.work;
  return Lhs;
}

CILKTOOL_API void wsp_dump(wsp_t Wsp, const char *Tag) {
  std::basic_ostream<char> &Output = Tool->outView();
  ensureHeader(Output);
  printResults(Output, Tag, cilk_time_t(Wsp.work));
}
