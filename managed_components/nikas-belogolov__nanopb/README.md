# Nanopb - Protocol Buffers for Embedded Systems

Nanopb is a small code-size Protocol Buffers implementation in ansi C. It is especially suitable for use in microcontrollers, but fits any memory restricted system.

## Using the nanopb library

To use the nanopb library, you need to do two things:

1. Compile your .proto files for nanopb, using protoc.
2. Include pb_encode.c, pb_decode.c and pb_common.c in your project.
