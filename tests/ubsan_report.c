#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
struct location {const char *file;uint32_t line,column;};
static void report(const char *kind,void *data){
  const struct location *p=data;
  fprintf(stderr,"UBCHECK %s at %s:%u:%u\n",kind,p&&p->file?p->file:"?",p?p->line:0,p?p->column:0);
  fflush(stderr);exit(86);
}
#define HANDLER(name) void __ubsan_handle_##name(void *data,...){report(#name,data);}
HANDLER(type_mismatch_v1_abort)
HANDLER(shift_out_of_bounds_abort)
HANDLER(add_overflow_abort)
HANDLER(sub_overflow_abort)
HANDLER(mul_overflow_abort)
HANDLER(divrem_overflow_abort)
HANDLER(negate_overflow_abort)
HANDLER(out_of_bounds_abort)
HANDLER(pointer_overflow_abort)
HANDLER(nonnull_arg_abort)
HANDLER(load_invalid_value_abort)
HANDLER(builtin_unreachable)
HANDLER(missing_return)
HANDLER(alignment_assumption_abort)
HANDLER(function_type_mismatch_abort)
HANDLER(float_cast_overflow_abort)
HANDLER(invalid_builtin_abort)
