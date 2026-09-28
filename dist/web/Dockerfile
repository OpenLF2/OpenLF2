FROM emscripten/emsdk:4.0.20@sha256:460fff8f8ac87e11b16447fbd66538a686eafa0e4fb977aa0989ed19fe2079f7

# The base image has CMake 3.22; OpenLF2 requires 3.25 or newer.
RUN python3 -m pip install --no-cache-dir cmake==3.31.6
