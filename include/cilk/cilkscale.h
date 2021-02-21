// -*- C++ -*-
#ifndef INCLUDED_CILK_CILKSCALE_H
#define INCLUDED_CILK_CILKSCALE_H

#ifdef __cplusplus
#include <cstdint>
#else // __cplusplus
#include <stdint.h>
#endif // __cplusplus

typedef int64_t raw_duration_t;
typedef struct wsp_t {
  raw_duration_t work;
  raw_duration_t span;
  raw_duration_t bspan;
} wsp_t;

#ifdef __cplusplus

#include <fstream>
#include <iostream>

#define CILKSCALE_EXTERN_C extern "C"
#define CILKSCALE_NOTHROW noexcept

wsp_t &operator+=(wsp_t &lhs, const wsp_t &rhs) noexcept;
wsp_t &operator-=(wsp_t &lhs, const wsp_t &rhs) noexcept;
std::ostream &operator<<(std::ostream &os, const wsp_t &pt);
std::ofstream &operator<<(std::ofstream &os, const wsp_t &pt);

#ifndef __cilkscale__
// Default implementations when the program is not compiled with Cilkscale.
inline wsp_t &operator+=(wsp_t &lhs, const wsp_t &rhs) noexcept { return lhs; }

inline wsp_t &operator-=(wsp_t &lhs, const wsp_t &rhs) noexcept { return lhs; }

inline std::ostream &operator<<(std::ostream &os, const wsp_t &pt) {
  return os;
}

inline std::ofstream &operator<<(std::ofstream &os, const wsp_t &pt) {
  return os;
}
#endif // #ifndef __cilkscale__

#else // #ifdef __cplusplus

#define CILKSCALE_EXTERN_C
#define CILKSCALE_NOTHROW __attribute__((nothrow))

#endif // #ifdef __cplusplus

#ifdef __cplusplus
inline wsp_t operator+(wsp_t lhs, const wsp_t &rhs) noexcept {
  lhs += rhs;
  return lhs;
}
inline wsp_t operator-(wsp_t lhs, const wsp_t &rhs) noexcept {
  lhs -= rhs;
  return lhs;
}

extern "C" {
#endif // #ifdef __cplusplus
inline wsp_t wsp_zero(void) CILKSCALE_NOTHROW {
  wsp_t res = {0, 0, 0};
  return res;
}
#ifdef __cplusplus
}
#endif // #ifdef __cplusplus

#ifndef __cilkscale__

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

// Default implementations when the program is not compiled with Cilkscale.
static inline wsp_t wsp_getworkspan() CILKSCALE_NOTHROW {
  wsp_t res = {0, 0, 0};
  return res;
}

static inline wsp_t wsp_add(wsp_t lhs, wsp_t rhs) CILKSCALE_NOTHROW {
  wsp_t res = {0, 0, 0};
  return res;
}

static inline wsp_t wsp_sub(wsp_t lhs, wsp_t rhs) CILKSCALE_NOTHROW {
  wsp_t res = {0, 0, 0};
  return res;
}

static inline void wsp_dump(wsp_t wsp, const char *tag) { return; }

#ifdef __cplusplus
} // extern "C"
#endif // __cplusplus

#else // #ifndef __cilkscale__

CILKSCALE_EXTERN_C wsp_t wsp_getworkspan() CILKSCALE_NOTHROW;

CILKSCALE_EXTERN_C
wsp_t wsp_add(wsp_t lhs, wsp_t rhs) CILKSCALE_NOTHROW;

CILKSCALE_EXTERN_C
wsp_t wsp_sub(wsp_t lhs, wsp_t rhs) CILKSCALE_NOTHROW;

CILKSCALE_EXTERN_C
void wsp_dump(wsp_t wsp, const char *tag);

#endif // #ifndef __cilkscale__

#endif // INCLUDED_CILK_CILKSCALE_H
