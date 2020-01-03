/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/string_util.h>

#include <tilck/kernel/user.h>
#include <tilck/kernel/sys_types.h>
#include <tilck/mods/tracing.h>

static bool
dump_param_int(uptr __val, char *dest, size_t dest_buf_size)
{
   const sptr val = (sptr)__val;
   int rc;

   rc = snprintk(dest,
                 dest_buf_size,
                 NBITS == 32 ? "%d" : "%lld",
                 val);

   return rc < (int)dest_buf_size;
}

static bool
dump_param_voidp(uptr val, char *dest, size_t dest_buf_size)
{
   int rc = snprintk(dest, dest_buf_size, "%p", val);
   return rc < (int)dest_buf_size;
}

static bool
dump_param_oct(uptr __val, char *dest, size_t dest_buf_size)
{
   int val = (int)__val;
   int rc;

   rc = snprintk(dest, dest_buf_size, "0%03o", val);
   return rc < (int)dest_buf_size;
}

static bool
dump_param_errno_or_val(uptr __val, char *dest, size_t dest_buf_size)
{
   int val = (int)__val;
   int rc;

   rc = (val >= 0)
      ? snprintk(dest, dest_buf_size, "%d", val)
      : snprintk(dest, dest_buf_size, "-%s", get_errno_name(-val));

   return rc < (int)dest_buf_size;
}

static bool
buf_append(char *dest, int *used, int *rem, char *str)
{
   int rc;
   ASSERT(*rem >= 0);

   if (*rem == 0)
      return false;

   rc = snprintk(dest + *used, (size_t) *rem, "%s", str);

   if (rc >= *rem)
      return false;

   *used += rc;
   *rem -= rc;
   return true;
}

static ALWAYS_INLINE bool
is_flag_on(uptr var, uptr fl)
{
   return (var & fl) == fl;
}

#define OPEN_CHECK_FLAG(x)                                           \
   if (is_flag_on(fl, x))                                            \
      if (!buf_append(dest, &used, &rem, #x "|"))                    \
         return false;

static bool
dump_param_open_flags(uptr fl, char *dest, size_t dest_buf_size)
{
   int rem = (int) dest_buf_size;
   int used = 0;

   if (fl == 0) {
      memcpy(dest, "0", 2);
      return true;
   }

   OPEN_CHECK_FLAG(O_APPEND)
   OPEN_CHECK_FLAG(O_ASYNC)
   OPEN_CHECK_FLAG(O_CLOEXEC)
   OPEN_CHECK_FLAG(O_CREAT)
   OPEN_CHECK_FLAG(O_DIRECT)
   OPEN_CHECK_FLAG(O_DIRECTORY)
   OPEN_CHECK_FLAG(O_DSYNC)
   OPEN_CHECK_FLAG(O_EXCL)
   OPEN_CHECK_FLAG(O_LARGEFILE)
   OPEN_CHECK_FLAG(O_NOATIME)
   OPEN_CHECK_FLAG(O_NOCTTY)
   OPEN_CHECK_FLAG(O_NOFOLLOW)
   OPEN_CHECK_FLAG(O_NONBLOCK)
   OPEN_CHECK_FLAG(O_NDELAY)
   OPEN_CHECK_FLAG(O_PATH)
   OPEN_CHECK_FLAG(O_SYNC)
   OPEN_CHECK_FLAG(O_TMPFILE)
   OPEN_CHECK_FLAG(O_TRUNC)

   ASSERT(dest[used - 1] == '|');
   dest[used - 1] = 0;
   return true;
}

const struct sys_param_type ptype_int = {

   .name = "int",
   .slot_size = 0,

   .save = NULL,
   .dump_from_data = NULL,
   .dump_from_val = dump_param_int,
};

const struct sys_param_type ptype_voidp = {

   .name = "void *",
   .slot_size = 0,

   .save = NULL,
   .dump_from_data = NULL,
   .dump_from_val = dump_param_voidp,
};

const struct sys_param_type ptype_oct = {

   .name = "oct",
   .slot_size = 0,

   .save = NULL,
   .dump_from_data = NULL,
   .dump_from_val = dump_param_oct,
};

const struct sys_param_type ptype_errno_or_val = {

   .name = "errno_or_val",
   .slot_size = 0,

   .save = NULL,
   .dump_from_data = NULL,
   .dump_from_val = dump_param_errno_or_val,
};

const struct sys_param_type ptype_open_flags = {

   .name = "int",
   .slot_size = 0,

   .save = NULL,
   .dump_from_data = NULL,
   .dump_from_val = dump_param_open_flags,
};
