# bf-compiler
A JIT compiler I created that generates and runs machine code for this [toy programming language](https://en.wikipedia.org/wiki/Brainfuck).

Note that this was written in a few hours, and is not necessarily bug-free/memory safe. Use at your own risk!

### Setup:

Currently, the JIT only generates Linux-x64 compatible code.

Just run <code>bash test</code> to generate a JIT executable in the **build/** directory.

### Usage:

* Input source code in shell: <code>bfc</code>
* Run the JIT on a source file: <code>bfc \<FILENAME\></code>
* Produce an ELF executable from a source file: <code>bfc -c \<SOURCE_FILENAME\> -o \<OUTPUT_FILENAME\></code>

Sample programs are located in the **samples/** directory.
