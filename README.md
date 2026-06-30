# Simple runtime x86-64 assembler and executor

## Usage
Populate 'asm.s' with your x86_64 code. This will then be assembled and run at runtime. If you need to pass arguments to your assembly function, modify the asm_fn_t type in assemble_file.c, and then the call site, if necessary.

## Prerequisites
  - `keystone`

## Building
    make
