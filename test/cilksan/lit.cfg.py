# -*- Python -*-

import os
import platform
import re

import lit.formats

# Get shlex.quote if available (added in 3.3), and fall back to pipes.quote if
# it's not available.
try:
  import shlex
  sh_quote = shlex.quote
except:
  import pipes
  sh_quote = pipes.quote

def get_required_attr(config, attr_name):
  attr_value = getattr(config, attr_name, None)
  if attr_value == None:
    lit_config.fatal(
      "No attribute %r in test configuration! You may need to run "
      "tests from your build directory or add this attribute "
      "to lit.site.cfg.py " % attr_name)
  return attr_value

def push_dynamic_library_lookup_path(config, new_path):
  if platform.system() == 'Windows':
    dynamic_library_lookup_var = 'PATH'
  elif platform.system() == 'Darwin':
    dynamic_library_lookup_var = 'DYLD_LIBRARY_PATH'
  else:
    dynamic_library_lookup_var = 'LD_LIBRARY_PATH'

  new_ld_library_path = os.path.pathsep.join(
    (new_path, config.environment.get(dynamic_library_lookup_var, '')))
  config.environment[dynamic_library_lookup_var] = new_ld_library_path

  if platform.system() == 'FreeBSD':
    dynamic_library_lookup_var = 'LD_32_LIBRARY_PATH'
    new_ld_32_library_path = os.path.pathsep.join(
      (new_path, config.environment.get(dynamic_library_lookup_var, '')))
    config.environment[dynamic_library_lookup_var] = new_ld_32_library_path

# Setup config name.
config.name = 'CilkSanitizer' + config.name_suffix

# Platform-specific default CILKSAN_OPTIONS for lit tests.
default_cilktool_opts = list(config.default_cilktool_opts)

# Setup source root.
config.test_source_root = os.path.dirname(__file__)

if config.host_os not in ['FreeBSD', 'NetBSD']:
  libdl_flag = "-ldl"
else:
  libdl_flag = ""

extra_link_flags = []

# Setup default compiler flags used with -fsanitize=cilk option.
# FIXME: Review the set of required flags and check if it can be reduced.
target_cflags = [get_required_attr(config, "target_cflags")] + extra_link_flags
target_cxxflags = config.cxx_mode_flags + target_cflags
clang_cilksan_static_cflags = (["-fsanitize=cilk"] +
                            config.debug_info_flags + target_cflags)
clang_cilksan_static_cxxflags = config.cxx_mode_flags + clang_cilksan_static_cflags

cilksan_dynamic_flags = []
if config.cilksan_dynamic:
  cilksan_dynamic_flags = ["-shared-libsan"]
  if platform.system() == 'FreeBSD':
    # On FreeBSD, we need to add -pthread to ensure pthread functions are available.
    cilksan_dynamic_flags += ['-pthread']
  config.available_features.add("cilksan-dynamic-runtime")
else:
  config.available_features.add("cilksan-static-runtime")
clang_cilksan_cflags = clang_cilksan_static_cflags + cilksan_dynamic_flags
clang_cilksan_cxxflags = clang_cilksan_static_cxxflags + cilksan_dynamic_flags

def build_invocation(compile_flags):
  return " " + " ".join([config.clang] + compile_flags) + " "

config.substitutions.append( ("%clang ", build_invocation(target_cflags)) )
config.substitutions.append( ("%clangxx ", build_invocation(target_cxxflags)) )
config.substitutions.append( ("%clang_cilksan ", build_invocation(clang_cilksan_cflags)) )
config.substitutions.append( ("%clangxx_cilksan ", build_invocation(clang_cilksan_cxxflags)) )
if config.cilksan_dynamic:
  if config.host_os in ['Linux', 'FreeBSD', 'NetBSD', 'SunOS']:
    shared_libcilksan_path = os.path.join(config.cilktools_libdir, "libclang_rt.cilksan{}.so".format(config.target_suffix))
  elif config.host_os == 'Darwin':
    shared_libcilksan_path = os.path.join(config.cilktools_libdir, 'libclang_rt.cilksan_{}_dynamic.dylib'.format(config.apple_platform))
  else:
    lit_config.warning('%shared_libcilksan substitution not set but dynamic Cilksan is available.')
    shared_libcilksan_path = None

  if shared_libcilksan_path is not None:
    config.substitutions.append( ("%shared_libcilksan", shared_libcilksan_path) )
  config.substitutions.append( ("%clang_cilksan_static ", build_invocation(clang_cilksan_static_cflags)) )
  config.substitutions.append( ("%clangxx_cilksan_static ", build_invocation(clang_cilksan_static_cxxflags)) )

# Some tests uses C++11 features such as lambdas and need to pass -std=c++11.
config.substitutions.append(("%stdcxx11 ", "-std=c++11 "))

# FIXME: De-hardcode this path.
cilksan_source_dir = os.path.join(
  get_required_attr(config, "cilktools_src_root"), "cilksan")
python_exec = sh_quote(get_required_attr(config, "python_executable"))
# # Setup path to asan_symbolize.py script.
# asan_symbolize = os.path.join(asan_source_dir, "scripts", "asan_symbolize.py")
# if not os.path.exists(asan_symbolize):
#   lit_config.fatal("Can't find script on path %r" % asan_symbolize)
# config.substitutions.append( ("%asan_symbolize", python_exec + " " + asan_symbolize + " ") )
# Setup path to sancov.py script.

# Determine kernel bitness
if config.host_arch.find('64') != -1 and not config.android:
  kernel_bits = '64'
else:
  kernel_bits = '32'

config.substitutions.append( ('CHECK-%kernel_bits', ("CHECK-kernel-" + kernel_bits + "-bits")))

config.substitutions.append( ("%libdl", libdl_flag) )

config.available_features.add("cilksan-" + config.bits + "-bits")

# Fast unwinder doesn't work with Thumb
if re.search('mthumb', config.target_cflags) is None:
  config.available_features.add('fast-unwinder-works')

# Set LD_LIBRARY_PATH to pick dynamic runtime up properly.
push_dynamic_library_lookup_path(config, config.cilktools_libdir)

# Default test suffixes.
config.suffixes = ['.c', '.cpp']

if config.host_os == 'Darwin':
  config.suffixes.append('.mm')

config.substitutions.append(('%fPIC', '-fPIC'))
config.substitutions.append(('%fPIE', '-fPIE'))
config.substitutions.append(('%pie', '-pie'))

# Only run the tests on supported OSs.
if config.host_os not in ['Linux', 'Darwin', 'FreeBSD']:
  config.unsupported = True

if not config.parallelism_group:
  config.parallelism_group = 'shadow-memory'
