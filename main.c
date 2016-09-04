#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>

#define NUM_CELLS 128
#define MAX_STACK_DEPTH NUM_CELLS/2

typedef void (*jitted_code)();

struct machine_op {
  char *code;
  int size;
};

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

struct machine_op brainfuck2op (char command) {
  struct machine_op ret;

  switch (command) {

    case '>': // advance to next cell
      ret.code = "\x48\xff\xc2"; // inc rdx
      ret.size = 3;
      break;

    case '<': // go back to prev cell
      ret.code = "\x48\xff\xca"; // dec rdx
      ret.size = 3;
      break;

    case '+': // increment current cell
      ret.code = "\x80\x02\x01";
      ret.size = 3;
      break;

    case '-': // decrement current cell
      ret.code = "\x80\x2a\x01";
      ret.size = 3;
      break;

    case ',': // store input in current cell
      ret.code = "\xb8\x00\x00\x00\x00\xbf\x00\x00\x00\x00\x48\x89\xd6\x52\xba\x01\x00\x00\x00\x0f\x05\x5a";
      ret.size = 22;
      break;

    case '.': // output from current cell
      ret.code = "\x48\x89\xd6\xb8\x01\x00\x00\x00\xbf\x01\x00\x00\x00\x52\xba\x01\x00\x00\x00\x0f\x05\x5a";
      ret.size = 22;
      break;

    default:
      ret.code = NULL;
      ret.size = 0;
      break;
  }

  return ret;
}

int get_program_size (char *program, int program_len) {
  int i;
  int size = 0;
  for (i=0; i < program_len; i++) {
    char command = program[i];
    if (command == '[') {
      size += 5;
    } else if (command == ']') {
      size += 9;
    } else {
      size += brainfuck2op(program[i]).size;
    } 
  }
  return size;
}

void jit (char *program, int program_len) {
  int i;

  // allocate ourselves a data section
  unsigned char* data_section = calloc(1, NUM_CELLS);
  if (data_section == NULL) {
    printf("calloc failed bruh\n");
    return;
  }

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

  for (i=0; i < program_len; i++) {
    char command = program[i];
    struct machine_op op = { NULL, 0 };
    if (command == '[') {
      // remember where this [ is so we can recall it on the next ]
      if (stack_push(&offsets, code) == 0) {
        printf("stack overflow!!!\n");
        return;
      }

      // this'll get filled in when we find the corresponding ]
      op = (struct machine_op){ "\x00\x00\x00\x00\x00", 5 };
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
      op = brainfuck2op(program[i]);
    } 

    if (op.code != NULL) {
      memcpy(code, op.code, op.size);
      code += op.size;
    }
  }

  memcpy(code, "\xb8\x3c\x00\x00\x00\x0f\x05", 7);
  code += 7;

  jitted_code func = (jitted_code)page;
  func();
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
