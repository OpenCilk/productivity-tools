# Building a standalone copy of the OpenCilk tools

These instructions assume that you are building the OpenCilk tools
using the OpenCilk compiler.

## Building with CMake

1. Make a build directory at the top level and enter it:

   ```console
   mkdir build
   cd build
   ```

2. Configure CMake.  Make sure to specify `CMAKE_C_COMPILER`,
   `CMAKE_CXX_COMPILER`, and `LLVM_CMAKE_DIR` to point to the
   corresponding build or installation of the OpenCilk compiler
   binaries.  In addition, set `CMAKE_BUILD_TYPE` to specify the build
   type, such as, `Debug`, for an unoptimized build with all
   assertions enabled; `Release`, for an fully optimized build with
   assertions disabled; or `RelWithDebInfo`, to enable some
   optimizations and assertions.  (The default build type is `Debug`.)

   Example configuration:

   ```console
   cmake -DCMAKE_C_COMPILER=/path/to/opencilk-project/build/bin/clang -DCMAKE_C_COMPILER=/path/to/opencilk-project/build/bin/clang++ -DCMAKE_BUILD_TYPE=Release -DLLVM_CMAKE_DIR=/path/to/opencilk-project/build ../

3. Build the runtime:

   ```console
   cmake --build . -- -j<number of build threads>
   ```

To clean the build, run `cmake --build . --target clean` from the build
directory.
