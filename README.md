Brainfuck JIT compiler
======================

I wanted to learn more about how JITs work, so I wrote one! So far this doesn't
do anything terribly exciting besides being much, much faster than your average
brainfuck interpreter, but it does currently do a few neato optimizations:

* Convert runs of operations like `+` or `>` into single ops
* Turns `[-]` into a single "set current cell to 0" op

# Building it

This was written and tested on a 64 bit i386 Linux machine, and as far as I can
tell that's the only place it'll run.

```
$ gcc -m64 main.c -o bfjit
$ ./bfjit tests/mandelbrot.b
```

# Acknowledgements
* [This article](https://toastedcornflakes.github.io/articles/jit-brainfuck.html)
  was absolutely instrumental in learning the basics of Brainfuck, x86 and JIT
  execution
