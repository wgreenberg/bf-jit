#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>

// Optimizations!
#define USE_OPT_RUN_LENGTH 1
#define USE_OPT_SET_ZERO 1

#define NUM_CELLS 10000
#define MAX_STACK_DEPTH NUM_CELLS/2

typedef void (*jitted_code)();

// crappy stack impl
struct stack {
  unsigned char* offsets[MAX_STACK_DEPTH];
  int ptr;
};

struct stack stack_init () {
  struct stack s = { {}, 0 };
  return s;
}

int stack_push (struct stack *s, unsigned char* offset) {
  if (s->ptr == MAX_STACK_DEPTH - 2)
    return 0;
  s->offsets[s->ptr++] = offset;
  return 1;
}

unsigned char* stack_pop (struct stack *s) {
  if (s->ptr == 0)
    return NULL;
  s->ptr--;
  return s->offsets[s->ptr];
}

// writes out a run of a single brainfuck operation as x86 machine code (which
// was derived from the operations.S file). Returns the size of the resulting
// machine code size (if any)
int write_brainfuck_ops (unsigned char *code, char command, int num_repeated) {
  unsigned char num_repeated_byte = (unsigned char)num_repeated;

  switch (command) {

    case '>': // advance to next cell
      if (code != NULL) {
        memcpy(code, "\x48\x83\xc2", 3);
        memcpy(code+3, &num_repeated_byte, 1);
      }
      return 4;

    case '<': // go back to prev cell
      if (code != NULL) {
        memcpy(code, "\x48\x83\xea", 3);
        memcpy(code+3, &num_repeated_byte, 1);
      }
      return 4;

    case '+': // increment current cell
      if (code != NULL) {
        memcpy(code, "\x80\x02", 2);
        memcpy(code+2, &num_repeated_byte, 1);
      }
      return 3;

    case '-': // decrement current cell
      if (code != NULL) {
        memcpy(code, "\x80\x2a", 2);
        memcpy(code+2, &num_repeated_byte, 1);
      }
      return 3;

    case ',': // store input in current cell
      if (code != NULL) {
        memcpy(code, "\xb8\x00\x00\x00\x00\xbf\x00\x00\x00\x00\x48\x89\xd6\x52\xba\x01\x00\x00\x00\x0f\x05\x5a", 22);
      }
      return 22;

    case '.': // output from current cell
      if (code != NULL) {
        memcpy(code, "\x48\x89\xd6\xb8\x01\x00\x00\x00\xbf\x01\x00\x00\x00\x52\xba\x01\x00\x00\x00\x0f\x05\x5a", 22);
      }
      return 22;

    default:
      return 0;
  }
}

// looks ahead in the program to see if we have a run of a single character 
// so we can squash them together, returning the number of repetitions
int count_runs (char* program, int program_len, int *program_i) {
  if (!USE_OPT_RUN_LENGTH)
    return 1;

  int i = *program_i;
  char c = program[i];

  // special case these since they're not combinable as such
  if (c == '.' || c == '[' || c == ']') {
    return 1;
  }

  // count up the number of consecutive occurrances of the first character
  i++;
  int count = 1;
  for (; i<program_len; i++) {
    if (c != program[i])
      break;
    count++;
  }

  // increment the program index since we're effectively squashing together a
  // run of brainfuck operations into one
  *program_i += count - 1;

  return count;
}

// look ahead for more nuanced optimization patterns, returning the size of the
// resulting machine code (if any)
int get_peephole_optimization (unsigned char *code, char* program, int program_len, int *program_i) {
  int lookahead_space = program_len - *program_i;
  if (lookahead_space >= 3) {
    // [-] => mov byte [rdx], 0
    if (strncmp(program + *program_i, "[-]", 3) == 0) {
      if (!USE_OPT_SET_ZERO)
        return 0;
      if (code != NULL) {
        memcpy(code, "\xc6\x02\x00", 3);
      }
      *program_i += 2;
      return 3;
    }
  }
  return 0;
}

// does a pass over the program to see how much space we need for our
// executable mmap'd page
int get_program_size (char *program, int program_len) {
  int i;
  int size = 0;
  for (i=0; i < program_len; i++) {
    char command = program[i];
    int peephole_size = get_peephole_optimization(NULL, program, program_len, &i);
    if (peephole_size > 0) {
      size += peephole_size;
    } else if (command == '[') {
      size += 5;
    } else if (command == ']') {
      size += 9;
    } else {
      int num_repeated = count_runs(program, program_len, &i);
      size += write_brainfuck_ops(NULL, program[i], num_repeated);
    } 
  }
  return size;
}

// generate machine code for program and run it
void jit (char *program, int program_len) {
  int i;

  // allocate ourselves a data section
  unsigned char* data_section = calloc(1, NUM_CELLS);
  if (data_section == NULL) {
    printf("calloc failed bruh\n");
    return;
  }

  // create a read/write/execute page for us to write code to and run
  int page_size = get_program_size(program, program_len);
  unsigned char *page = mmap(0, page_size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (page == MAP_FAILED || page == NULL) {
    printf("mmap failed bruh\n");
    return;
  }
  unsigned char *code = page;

  // initialize rdx to the address of our data section (i.e. cell 0)
  memcpy(code, "\x48\xba", 2);
  memcpy(code+2, &data_section, sizeof(unsigned char*));
  code += 2 + sizeof(unsigned char*);

  // store offsets of '[' so we can jump to them at ']'
  struct stack offsets = stack_init();

  // iterate through the entire program and generate machine code
  for (i=0; i < program_len; i++) {
    char command = program[i];

    // if we find a peephole optimization, use that and continue
    int peephole_size = get_peephole_optimization(code, program, program_len, &i);
    if (peephole_size > 0) {
      code += peephole_size;
    } else if (command == '[') {
      // remember where this [ is so we can recall it on the next ]
      if (stack_push(&offsets, code) == 0) {
        printf("stack overflow!!!\n");
        return;
      }

      // this'll get filled in when we find the corresponding ]
      memcpy(code, "\x00\x00\x00\x00\x00", 5);
      code += 5;
    } else if (command == ']') {
      // find out where the last matching [ is
      unsigned char* loop_start = stack_pop(&offsets);
      if (loop_start == 0) {
        printf("unmatched ] brace!!!!\n");
        return;
      }

      // note our current position
      unsigned char* loop_end_check = code;

      uint32_t jump_back = -(4 + (loop_end_check - loop_start));
      uint32_t jump_forward = loop_end_check - (loop_start + 5);

      // rewrite the [ instruction to jump to us
      memcpy(loop_start, "\xe9", 1);
      memcpy(loop_start + 1, &jump_forward, 4);

      // if we want to return to the last [, we have to go back the distance
      // from its op to our current one
      memcpy(code, "\x80\x3a\x00\x0f\x85", 5);
      memcpy(code+5, &jump_back, 4);
      code += 9;
    } else {
      // otherwise we're dealing with a normal command. see if there's a run of
      // consecutive brainfuck symbols and write out the resulting machine code
      int num_repeated = count_runs(program, program_len, &i);
      int size = write_brainfuck_ops(code, program[i], num_repeated);
      code += size;
    } 
  }

  // finish up with a call to sys_exit
  memcpy(code, "\xb8\x3c\x00\x00\x00\x0f\x05", 7);
  code += 7;

  // run the jitted program!
  jitted_code func = (jitted_code)page;
  func();

  // release our data segment
  free(data_section);
}

int main (int argc, char **argv) {
  int fd = open(argv[1], O_RDONLY);
  char *program_buffer;
  struct stat s;

  if (fd < 0) {
    printf("couldn't open file!!\n");
    return 1;
  }

  fstat(fd, &s);

  program_buffer = mmap(0, s.st_size, PROT_READ, MAP_PRIVATE, fd, 0);

  if (program_buffer != (void*)-1) {
    jit(program_buffer, s.st_size);
    munmap(program_buffer, s.st_size);
  }
}
