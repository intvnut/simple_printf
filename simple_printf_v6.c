#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Operand sizes: */   /* Mod   diouxX conversions                  cs convs */
enum {                 /* ----  ----------------------------------  -------- */
  kSizeChar,           /* hh    signed char, unsigned char                   */
  kSizeShort,          /*  h    short, unsigned short                        */
  kSizeDefault,        /* none  int, unsigned int, double,          char     */
  kSizeLong,           /*  l    long, unsigned long,                wchar_t  */
  kSizeLongLong,       /* ll    long long int, unsigned long long            */
  kSizeMax,            /*  j    intmax_t                                     */
  kSizeSizeT,          /*  z    size_t                                       */
  kSizePtrDiffT        /*  t    ptrdiff_t                                    */
};

/* Sign display: */    /* Flag   Non-negative values   Negative values       */
enum {                 /* -----  --------------------  --------------------- */
  kSignDefault,        /* none   Nothing               '-'                   */
  kSignAlways,         /*  +     '+'                   '-'                   */
  kSignSpace           /* space  ' '                   '-'                   */
};

/* Abstracts away how text gets output, and how much was actually output. */
struct printer {
  union {
    FILE *file;
    char *buf;
  };
  size_t max;
  size_t total;
  void (*copy)(struct printer *p, const char *s, size_t length);
  void (*fill)(struct printer *p, char c, size_t length);
  void (*putc)(struct printer *p, char c);
};

/* Assume MSB is sign bit. */
#define SIGN_BIT (ULLONG_MAX - ULLONG_MAX / 2)

/*
 * Buffer size for rendering integers.  This should be enough for a 128-bit
 * intmax_t, with sign or 0x prefix and terminating null, with some extra room.
 * My current platform only has a 64-bit intmax_t, however, so 128-bit is
 * not tested.
 */
#define INT_BUF_SIZE (48)

static const char *const hex_digits[2] = {
  "0123456789abcdef", "0123456789ABCDEF"
};

/*
 * Converts an integer in the specified base, stored at the _end_ of buf[].
 * Returns the index of the first character.
 */
static int conv_integer(
    unsigned long long value, int sign, bool is_signed, bool is_caps,
    bool is_alt, int prec, bool soft_prec, unsigned base, char *buf) {
  bool is_negative = false;
  int idx = INT_BUF_SIZE;

  buf[--idx] = '\0';

  /* Print nothing if precision and value are both 0. */
  if (!soft_prec && prec == 0 && value == 0) {
    return INT_BUF_SIZE - 1;
  }

  /* Remember negative numbers in signed conversions.  Convert to positive. */
  if (is_signed && (value & SIGN_BIT)) {
    is_negative = true;
    value = -value;
  }

  /* Convert the digits, starting with the least significant. */
  do {
    buf[--idx] = hex_digits[is_caps][value % base];
    value /= base;
  } while (value > 0);

  /*
   * If our precision actually came from the width field, adjust it based on
   * other things we might print before the padding zeros.
   */
  if (soft_prec) {
    if (is_alt && base == 16) { prec -= 2; }
    if (is_alt && base == 8 && buf[idx] != '0') { prec--; }
    if (is_negative || (is_signed && sign != kSignDefault)) { prec--; }

    if (prec < 1) { prec = 1; }
  }

  /*
   * Compute index for padding zeros, out to precision.  Bound the number of
   * leading zeros we support to what fits in our buffer.  This does break
   * standards compliance, as it wants us to support up to 4095 characters in
   * a conversion.
   */
  int prec_idx = prec < INT_BUF_SIZE - 1 ? INT_BUF_SIZE - prec : 1;

  /* Leave room for "0x" if alternate-form hex.  Hex is never signed. */
  if (prec_idx < 3 && is_alt && base == 16) {
    prec_idx = 3;
  }

  /* Leave room for "0" if alternate-form octal.  Octal is never signed. */
  if (prec_idx < 3 && is_alt && base == 8) {
    prec_idx = 2;
  }

  /* Leave enough room for a leading sign if we need it. */
  if (prec_idx < 2 && (is_negative || (is_signed && sign != kSignDefault))) {
    prec_idx = 2;
  }

  /* Add leading zeros out to precision index. */
  while (idx >= prec_idx) {
    buf[--idx] = '0';
  }

  /* If we're alternate-form octal, add a leading 0 if needed. */
  if (is_alt && base == 8 && buf[idx] != '0') {
    buf[--idx] = '0';
  }

  /* If we're alternate-form hex, add a leading "0x" or "0X". */
  if (is_alt && base == 16) {
    buf[--idx] = is_caps ? 'X' : 'x';
    buf[--idx] = '0';
  }
    
  /*
   * If we're negative add a negative sign.  Otherwise, if this is a signed
   * conversion, add a '+' or ' ' if directed.
   */
  if (is_negative) {
    buf[--idx] = '-';
  } else if (is_signed && sign != kSignDefault) {
    buf[--idx] = sign == kSignAlways ? '+' : ' ';
  }

  return idx;
}

/* Prints a string in a particular width field. */
static void print_string(
    struct printer *p, const char *s, int len, int width, bool left_justify) {
  if (!left_justify && len < width) {
    p->fill(p, ' ', width - len);
  }
  
  p->copy(p, s, len);

  if (left_justify && len < width) {
    p->fill(p, ' ', width - len);
  }
}

/* Gets a signed argument of the specified size. */
static unsigned long long get_signed_integer(va_list *args, int size) {
  unsigned long long v;

  switch (size) {
    case kSizeChar:     { return (signed char )va_arg(*args, int); }
    case kSizeShort:    { return (signed short)va_arg(*args, int); }
    case kSizeDefault:  { return va_arg(*args, int);               }
    case kSizeLong:     { return va_arg(*args, long);              }
    case kSizeLongLong: { return va_arg(*args, long long);         }
    case kSizeMax:      { return va_arg(*args, intmax_t);          }
    case kSizePtrDiffT: { return va_arg(*args, ptrdiff_t);         }
    case kSizeSizeT:    {
      /*
       * C doesn't give a name to the signed type that corresponds to
       * size_t, but that's what we need to extract here.  Guess among
       * long long, long, and int based on size.
       */
      if (sizeof(size_t) == sizeof(unsigned long long)) {
        return va_arg(*args, long long);
      } else if (sizeof(size_t) == sizeof(unsigned long)) {
        return va_arg(*args, long);
      } else {
        return va_arg(*args, int);
      }
    }
  }

  /* Unknown size.  Fall back to int. */
  return va_arg(*args, int);
}

/* Gets a signed argument of the specified size. */
static unsigned long long get_unsigned_integer(va_list *args, int size) {
  switch (size) {
    case kSizeChar:     { return (unsigned char)va_arg(*args, int); }
    case kSizeShort:    { return (unsigned short)va_arg(*args,int); }
    case kSizeDefault:  { return va_arg(*args, unsigned int);       }
    case kSizeLong:     { return va_arg(*args, unsigned long);      }
    case kSizeLongLong: { return va_arg(*args, unsigned long long); }
    case kSizeMax:      { return va_arg(*args, uintmax_t);          }
    case kSizeSizeT:    { return va_arg(*args, size_t);             }
    case kSizePtrDiffT: {
      /*
       * C doesn't give a name to the unsigned type that corresponds to
       * ptrdiff_t, but that's what we need to extract here.  Guess
       * among unsigned long long, unsigned long, and unsigned based on
       * size.
       */
      if (sizeof(ptrdiff_t) == sizeof(long long)) {
        return va_arg(*args, unsigned long long);
      } else if (sizeof(size_t) == sizeof(long)) {
        return va_arg(*args, unsigned long);
      } else {
        return va_arg(*args, unsigned);
      }
      break;
    }
  }

  /* Unknown size.  Fall back to unsigned. */
  return va_arg(*args, unsigned);
}

/* 
 * Implements simplified printf that understands:
 *  -- Strings: "s"
 *  -- Characters: "c"
 *  -- Integers:
 *      -- Lengths: "hh", "h", "l", "ll", "j", "z", "t" and default int.
 *      -- Signed decimal: "d", "i"
 *      -- Unsigned decimal: "u"
 *      -- Octal: "o"
 *      -- Hexadecimal: "x", "X"
 *  -- Pointers: "p".
 *  -- Flags:
 *      -- "#" for alternate form on "o", "x", and "X" conversions.
 *      -- " " and "+" for sign on "d", "i" conversions.
 *      -- "0" for leading zeros on integer conversions.
 *      -- "-" for left-justified fields.
 *  -- Width and precision specifiers:
 *      -- Supports dynamic values via "*".
 *      -- Max integer precision is limited by INT_BUF_MAX.
 *  -- Printing "%" with "%%".
 *
 * Not supported:
 *  -- Floating point.
 *  -- Returning length of printed string.
 *  -- Reporting length of printed string with "n".
 *  -- Printing to a stream other than stdout.
 *  -- Printing to a buffer.
 *
 * I guess it's no longer so simple...
 */
static void printf_core(struct printer *p, const char *fmt, va_list args) {
  char buf[INT_BUF_SIZE];
  const char *pfmt = NULL;

  for (int ch = *fmt++; ch; ch = *fmt++) {
    /*
     * If it's not %, just print the character. The span [pfmt, fmt) holds
     * the fixed characters.
     */
    if (ch != '%') {
      if (!pfmt) {
        pfmt = fmt - 1;
      }
      continue;
    }

    /* Output any batched up non-conversion characters in format. */
    if (pfmt) {
      p->copy(p, pfmt, fmt - pfmt - 1);
      pfmt = NULL;
    }

    /* It's (potentially) a conversion. Let's look. */
    const char *initial = fmt;  /* Start of conversion specifier.            */
    int  conv = *fmt++;         /* Fetch first character of conversion spec. */
    bool leading_zero = false;  /* Leading-zero flag.                        */
    bool left_justify = false;  /* Left justification flag.                  */
    bool is_alt = false;        /* Alternate form flag.                      */
    int  sign = kSignDefault;   /* Sign display flag.                        */
    int  size = kSizeDefault;   /* Operand size.                             */
    bool default_width = true;  /* Use the default width.                    */
    int  width = 0;             /* User-provided width of the field.         */
    bool default_prec = true;   /* Use the default precision.                */
    int  prec = 0;              /* User-provided precision of the field.     */
    bool is_caps = false;       /* Print hex values in capital letters.      */
    bool is_signed = true;      /* Perform a signed integer conversion.      */
    int  base = 10;             /* Default radix is decimal.                 */

    /* Check for flags.  Flags can appear in any order. */
    for (bool done_flags = false; !done_flags;) {
      switch (conv) {
        case '0': { leading_zero = true; conv = *fmt++; continue; }
        case '-': { left_justify = true; conv = *fmt++; continue; }
        case '+': { sign = kSignAlways;  conv = *fmt++; continue; }
        case '#': { is_alt = true;       conv = *fmt++; continue; }

        case ' ': {
          if (sign == kSignDefault) {
            /* ' ' only takes effect if '+' isn't also provided. */
            sign = kSignSpace;
          }

          conv = *fmt++;
          continue;
        }

        default: {
          done_flags = true;
        }
      }
    }

    /* Check for width. */
    if (conv == '*') {  /* Width provided as an int argument. */
      default_width = false;
      width = va_arg(args, int);

      if (width < 0) {  /* Negative width specifies left justification. */
        left_justify = true;
        width = -width;
      }

      conv = *fmt++;
    } else {  /* Width is a decimal number in the format string. */
      while (isdigit(conv)) {
        default_width = false;
        width = width * 10 + (conv - '0');
        conv = *fmt++;
      }
    }

    /* Check for precision. Always preceded by a "." */
    if (conv == '.') {
      default_prec = false;
      conv = *fmt++;

      if (conv == '*') {  /* Precision provided as an int argument. */
        prec = va_arg(args, int);

        if (prec < 0) { prec = 0; } /* Negative precision acts like 0. */

        conv = *fmt++;
      } else {  /* Precision is a decimal number in the format string. */
        while (isdigit(conv)) {
          prec = prec * 10 + (conv - '0');
          conv = *fmt++;
        }
      }
    }

    /* Check for a size modifier: "hh", "h", "l", "ll", "j", "z", "t". */
    switch (conv) {
      case 'h': {
        size = kSizeShort;  /* "h" is short. */
        
        if (*fmt == 'h') {  /* "hh" is char. */
            size = kSizeChar;
            fmt++;
        }

        conv = *fmt++;
        break;
      }

      case 'l': {
        size = kSizeLong;   /* "l" is long. */

        if (*fmt == 'l') {  /* "ll" is long long. */
            size = kSizeLongLong;
            fmt++;
        }

        conv = *fmt++;
        break;
      }

      case 'j': { size = kSizeMax;      conv = *fmt++; break; }
      case 'z': { size = kSizeSizeT;    conv = *fmt++; break; }
      case 't': { size = kSizePtrDiffT; conv = *fmt++; break; }
    }

    /* Now look for the actual conversion. */
    switch (conv) {
      case 'c': {
        /* For now, we don't support %lc. */
        if (size != kSizeDefault) {
          fmt = initial;
          p->putc(p, '%');
          break;
        }

        putchar((unsigned char)va_arg(args, int));
        break;
      } 

      case 's': {
        /* For now, we don't support %ls. */
        if (size != kSizeDefault) {
          fmt = initial;
          p->putc(p, '%');
          break;
        }

        /* %s is a string. */
        if (default_prec) {
          prec = INT_MAX;
        }

        const char *const s = va_arg(args, const char *);
        const char *const e = memchr(s, '\0', prec);
        const int len = !e ? prec : e - s;
        print_string(p, s, len, width, left_justify);
        break;
      }

      case 'o':     /* %o is an unsigned octal integer. */
        base = 8;
        /* FALLTHROUGH_INTENDED */
      case 'X':     /* %X is an unsigned hexadecimal integer in caps. */
        is_caps = true;
        /* FALLTHROUGH_INTENDED */
      case 'x':     /* %x is an unsigned hexadecimal integer in lowercase. */
        base = base == 10 ? 16 : base;  /* Hex unless fallthrough from octal. */
        /* FALLTHROUGH_INTENDED */
      case 'u':     /* %u is an unsigned decimal integer. */
        is_signed = false;
        /* FALLTHROUGH_INTENDED */
      case 'i':     /* %d and %i are signed decimal integers. */
      case 'd': {
        unsigned long long val = is_signed ? get_signed_integer(&args, size)
                                           : get_unsigned_integer(&args, size);
        bool soft_prec = false;

        if (default_prec) {
          /*
           * If provided an explicit width but no precision, and asked to zero
           * pad treat the width like a "soft" precision that can be eaten into
           * by the sign and a radix prefix if needed.
           */
          if (leading_zero && !default_width && !left_justify) {
            prec = width;
            soft_prec = true;
          } else {
            prec = 1;
          } 
        }

        int idx = conv_integer(val, sign, is_signed, is_caps, is_alt, prec,
                               soft_prec, base, buf);
        int len = INT_BUF_SIZE - idx - 1;
        print_string(p, buf + idx, len, width, left_justify);
        break;
      }

      case 'p': {
        /* Print pointers like %#x with an appropriate length modifier. */
        void *ptr = va_arg(args, void *);
        unsigned long long val = (uintptr_t)ptr;

        int idx = conv_integer(val, false, false, false, true, 1, false,
                               16, buf);

        int len = INT_BUF_SIZE - idx - 1;
        print_string(p, buf + idx, len, width, left_justify);
        break;
      }

      case '%': {
        /* %% prints '%' */
        p->putc(p, '%');
        break;
      }

      default: {
        /* Not a valid conversion.  Print the '%' and back up. */
        p->putc(p, '%');
        fmt = initial;
        break;
      }
    }
  }

  if (pfmt) {
    p->copy(p, pfmt, fmt - pfmt - 1);
    pfmt = NULL;
  }
}


/* Copies string to a file. */
static void printer_file_copy(struct printer *p, const char *s, size_t len) {
  p->total += len;
  fwrite(s, 1, len, p->file);
}

/* Writes a block of fill characters to a file. */
static void printer_file_fill(struct printer *p, char c, size_t len) {
  char buf[16];
  memset(buf, c, len);

  p->total += len;

  while (len > 16) {
    fwrite(buf, 1, 16, p->file);
    len -= 16;
  }

  if (len > 0) {
    fwrite(buf, 1, len, p->file);
  }
}

/* Copies a character to a file. */
static void printer_file_putc(struct printer *p, char c) {
  p->total++;
  fputc(c, p->file);
}

/* Wrapper around printf_core for printing to stdout. */
int simple_printf(const char *fmt, ...) {
  struct printer p = {
    .file = stdout,
    .total = 0,
    .copy = printer_file_copy,
    .fill = printer_file_fill,
    .putc = printer_file_putc
  };

  va_list args;
  va_start(args, fmt);
  printf_core(&p, fmt, args);
  va_end(args);

  return p.total;
}


/* Copies a string to a buffer */
static void printer_buf_copy(struct printer *p, const char *s, size_t len) {
  if (p->total >= p->max) {
    p->total += len;
    return;
  }

  size_t avail = p->max - p->total;
  char *target = p->buf + p->total;
  p->total += len;

  if (len > avail) { len = avail; }  /* Limit our output to what's allowed. */
  memcpy(target, s, len);
}

/* Fills a block of output with a character. */
static void printer_buf_fill(struct printer *p, char c, size_t len) {
  if (p->total >= p->max) {
    p->total += len;
    return;
  }

  size_t avail = p->max - p->total;
  char *target = p->buf + p->total;
  p->total += len;

  if (len > avail) { len = avail; }  /* Limit our output to what's allowed. */
  memset(target, c, len);
}

/* Copies a character to a buffer */
static void printer_buf_putc(struct printer *p, char c) {
  if (p->total >= p->max) {
    p->total++;
    return;
  }

  p->buf[p->total++] = c;
}

/* Wrapper around printf_core for printing to a buffer. */
int simple_snprintf(char *buf, size_t max, const char *fmt, ...) {
  struct printer p = {
    .buf = buf,
    .max = max > 0 ? max - 1 : 0,  /* Save room for null! */
    .total = 0,
    .copy = printer_buf_copy,
    .fill = printer_buf_fill,
    .putc = printer_buf_putc
  };

  va_list args;
  va_start(args, fmt);
  printf_core(&p, fmt, args);
  va_end(args);

  /* Null terminate result. */
  size_t end = p.total < p.max ? p.total : p.max;
  p.buf[end] = '\0';

  return p.total;  /* Total converted characters, possibly more than max. */
}

int main() {
  simple_printf("Hello %s, the answer is %d.\n", "world", 42);
  simple_printf("Zero: %d %i %o %x %X char: '%c'\n", 0, 0, 0, 0, 0, '*');

  simple_printf("\nIntegers: signed char / unsigned char\n");
  simple_printf("Positive %%hhd %%#hhd: %hhd %#hhd\n", 123456789,  123456789);
  simple_printf("Negative %%hhd %%#hhd: %hhd %#hhd\n", -123456789, -123456789);
  simple_printf("Positive %%hhi %%#hhi: %hhi %#hhi\n", 123456789,  123456789);
  simple_printf("Negative %%hhi %%#hhi: %hhi %#hhi\n", -123456789, -123456789);
  simple_printf("Unsigned %%hhu %%#hhu: %hhu %#hhu\n", 4000000000U,4000000000U);
  simple_printf("Octal    %%hho %%#hho: %hho %#hho\n", 123456789,  123456789);
  simple_printf("Octal    %%hho %%#hho: %hho %#hho\n", -123456789, -123456789);
  simple_printf("Octal    %%hho %%#hho: %hho %#hho\n", 4000000000U,4000000000U);
  simple_printf("Hex      %%hhx %%#hhx: %hhx %#hhx\n", 123456789,  123456789);
  simple_printf("Hex      %%hhx %%#hhx: %hhx %#hhx\n", -123456789, -123456789);
  simple_printf("Hex      %%hhx %%#hhx: %hhx %#hhx\n", 4000000000U,4000000000U);
  simple_printf("Hex      %%hhX %%#hhX: %hhX %#hhX\n", 123456789,  123456789);
  simple_printf("Hex      %%hhX %%#hhX: %hhX %#hhX\n", -123456789, -123456789);
  simple_printf("Hex      %%hhX %%#hhX: %hhX %#hhX\n", 4000000000U,4000000000U);

  simple_printf("\nIntegers: short / unsigned short\n");
  simple_printf("Positive %%hd %%#hd: %hd %#hd\n", 123456789,  123456789);
  simple_printf("Negative %%hd %%#hd: %hd %#hd\n", -123456789, -123456789);
  simple_printf("Positive %%hi %%#hi: %hi %#hi\n", 123456789,  123456789);
  simple_printf("Negative %%hi %%#hi: %hi %#hi\n", -123456789, -123456789);
  simple_printf("Unsigned %%hu %%#hu: %hu %#hu\n", 4000000000U,4000000000U);
  simple_printf("Octal    %%ho %%#ho: %ho %#ho\n", 123456789,  123456789);
  simple_printf("Octal    %%ho %%#ho: %ho %#ho\n", -123456789, -123456789);
  simple_printf("Octal    %%ho %%#ho: %ho %#ho\n", 4000000000U,4000000000U);
  simple_printf("Hex      %%hx %%#hx: %hx %#hx\n", 123456789,  123456789);
  simple_printf("Hex      %%hx %%#hx: %hx %#hx\n", -123456789, -123456789);
  simple_printf("Hex      %%hx %%#hx: %hx %#hx\n", 4000000000U,4000000000U);
  simple_printf("Hex      %%hX %%#hX: %hX %#hX\n", 123456789,  123456789);
  simple_printf("Hex      %%hX %%#hX: %hX %#hX\n", -123456789, -123456789);
  simple_printf("Hex      %%hX %%#hX: %hX %#hX\n", 4000000000U,4000000000U);

  simple_printf("\nIntegers: int / unsigned int\n");
  simple_printf("Positive %%d %%#d: %d %#d\n", 123456789,  123456789);
  simple_printf("Negative %%d %%#d: %d %#d\n", -123456789, -123456789);
  simple_printf("Positive %%i %%#i: %i %#i\n", 123456789,  123456789);
  simple_printf("Negative %%i %%#i: %i %#i\n", -123456789, -123456789);
  simple_printf("Unsigned %%u %%#u: %u %#u\n", 4000000000U,4000000000U);
  simple_printf("Octal    %%o %%#o: %o %#o\n", 123456789,  123456789);
  simple_printf("Octal    %%o %%#o: %o %#o\n", -123456789, -123456789);
  simple_printf("Octal    %%o %%#o: %o %#o\n", 4000000000U,4000000000U);
  simple_printf("Hex      %%x %%#x: %x %#x\n", 123456789,  123456789);
  simple_printf("Hex      %%x %%#x: %x %#x\n", -123456789, -123456789);
  simple_printf("Hex      %%x %%#x: %x %#x\n", 4000000000U,4000000000U);
  simple_printf("Hex      %%X %%#X: %X %#X\n", 123456789,  123456789);
  simple_printf("Hex      %%X %%#X: %X %#X\n", -123456789, -123456789);
  simple_printf("Hex      %%X %%#X: %X %#X\n", 4000000000U,4000000000U);

  simple_printf("\nIntegers: long / unsigned long\n");
  simple_printf("Positive %%ld %%#ld: %ld %#ld\n", 123456789,  123456789);
  simple_printf("Negative %%ld %%#ld: %ld %#ld\n", -123456789, -123456789);
  simple_printf("Positive %%li %%#li: %li %#li\n", 123456789,  123456789);
  simple_printf("Negative %%li %%#li: %li %#li\n", -123456789, -123456789);
  simple_printf("Unsigned %%lu %%#lu: %lu %#lu\n", 4000000000U,4000000000U);
  simple_printf("Octal    %%lx %%#lx: %lo %#lo\n", 123456789,  123456789);
  simple_printf("Octal    %%lx %%#lx: %lo %#lo\n", -123456789, -123456789);
  simple_printf("Octal    %%lx %%#lx: %lo %#lo\n", 4000000000U,4000000000U);
  simple_printf("Hex      %%lx %%#lx: %lx %#lx\n", 123456789,  123456789);
  simple_printf("Hex      %%lx %%#lx: %lx %#lx\n", -123456789, -123456789);
  simple_printf("Hex      %%lx %%#lx: %lx %#lx\n", 4000000000U,4000000000U);
  simple_printf("Hex      %%lX %%#lX: %lX %#lX\n", 123456789,  123456789);
  simple_printf("Hex      %%lX %%#lX: %lX %#lX\n", -123456789, -123456789);
  simple_printf("Hex      %%lX %%#lX: %lX %#lX\n", 4000000000U,4000000000U);

  simple_printf("\nIntegers: long long / unsigned long long\n");
  simple_printf("Positive %%lld %%#lld: %lld %#lld\n", 123456789123456789LL,
                                                       123456789123456789LL);
  simple_printf("Negative %%lld %%#lld: %lld %#lld\n", -123456789123456789LL,
                                                       -123456789123456789LL);
  simple_printf("Positive %%lli %%#lli: %lli %#lli\n", 123456789123456789LL,
                                                       123456789123456789LL);
  simple_printf("Negative %%lli %%#lli: %lli %#lli\n", -123456789123456789LL,
                                                       -123456789123456789LL);
  simple_printf("Unsigned %%llu %%#llu: %llu %#llu\n", 4000000000000000000ULL,
                                                       4000000000000000000ULL);
  simple_printf("Octal    %%llo %%#llo: %llo %#llo\n", 123456789123456789LL,
                                                       123456789123456789LL);
  simple_printf("Octal    %%llo %%#llo: %llo %#llo\n", -123456789123456789LL,
                                                       -123456789123456789LL);
  simple_printf("Octal    %%llo %%#llo: %llo %#llo\n", 4000000000000000000ULL,
                                                       4000000000000000000ULL);
  simple_printf("Hex      %%llx %%#llx: %llx %#llx\n", 123456789123456789LL,
                                                       123456789123456789LL);
  simple_printf("Hex      %%llx %%#llx: %llx %#llx\n", -123456789123456789LL,
                                                       -123456789123456789LL);
  simple_printf("Hex      %%llx %%#llx: %llx %#llx\n", 4000000000000000000ULL,
                                                       4000000000000000000ULL);
  simple_printf("Hex      %%llX %%#llX: %llX %#llX\n", 123456789123456789LL,
                                                       123456789123456789LL);
  simple_printf("Hex      %%llX %%#llX: %llX %#llX\n", -123456789123456789LL,
                                                       -123456789123456789LL);
  simple_printf("Hex      %%llX %%#llX: %llX %#llX\n", 4000000000000000000ULL,
                                                       4000000000000000000ULL);

  simple_printf("\nIntegers: size_t\n");
  simple_printf("Positive %%zd: %jd  (expected: -1)\n", SIZE_MAX);
  simple_printf("Unsigned %%zu: %ju\n", SIZE_MAX);
  simple_printf("Octal    %%zo: %jo\n", SIZE_MAX);
  simple_printf("Hex      %%zx: %jx\n", SIZE_MAX);
  simple_printf("Hex      %%zX: %jX\n", SIZE_MAX);

  simple_printf("\nIntegers: ptrdiff_t\n");
  simple_printf("Positive %%td: %jd\n", PTRDIFF_MAX);
  simple_printf("Negative %%td: %jd\n", PTRDIFF_MIN);
  simple_printf("Unsigned %%tu: %ju\n", PTRDIFF_MAX);
  simple_printf("Octal    %%to: %jo\n", PTRDIFF_MAX);
  simple_printf("Hex      %%tx: %jx\n", PTRDIFF_MAX);
  simple_printf("Hex      %%tX: %jX\n", PTRDIFF_MAX);

  simple_printf("\nIntegers: intmax_t / uintmax_t\n");
  simple_printf("Positive %%jd: %jd\n", INTMAX_MAX);
  simple_printf("Negative %%jd: %jd\n", INTMAX_MIN);
  simple_printf("Unsigned %%ju: %ju\n", UINTMAX_MAX);
  simple_printf("Octal    %%jo: %jo\n", UINTMAX_MAX);
  simple_printf("Hex      %%jx: %jx\n", UINTMAX_MAX);
  simple_printf("Hex      %%jX: %jX\n", UINTMAX_MAX);

  simple_printf("Positive %%#jd: %#jd\n", INTMAX_MAX);
  simple_printf("Negative %%#jd: %#jd\n", INTMAX_MIN);
  simple_printf("Unsigned %%#ju: %#ju\n", UINTMAX_MAX);
  simple_printf("Octal    %%#jo: %#jo\n", UINTMAX_MAX);
  simple_printf("Hex      %%#jx: %#jx\n", UINTMAX_MAX);
  simple_printf("Hex      %%#jX: %#jX\n", UINTMAX_MAX);
 
  simple_printf("\nField width & precision:\n");
  simple_printf("%%s:    [%s]    %%-10s:    [%-10s] %%10s:    [%10s]\n",
                "Hello", "Hello", "Hello");
  simple_printf("%%.2s:  [%.2s]       %%-10.2s:  [%-10.2s] %%10.2s:  "
                "[%10.2s]\n", "Hello", "Hello", "Hello");
  simple_printf("%%d:    [%d]    %%-10d:    [%-10d] %%10d:    [%10d]\n",
                12345, 12345, 12345);
  simple_printf("%%d:    [%d]    %%-10d:    [%-10d] %%10d:    [%10d]\n",
                -1234, -1234, -1234);

  simple_printf("%% d:   [% d]    %% -10d:   [% -10d] %% 10d:   [% 10d]\n",
                1234, 1234, 1234);
  simple_printf("%% d:   [% d]    %% -10d:   [% -10d] %% 10d:   [% 10d]\n",
                -1234, -1234, -1234);
  simple_printf("%%+d:   [%+d]    %%+-10d:   [%+-10d] %%+10d:   [%+10d]\n",
                1234, 1234, 1234);
  simple_printf("%%+d:   [%+d]    %%+-10d:   [%+-10d] %%+10d:   [%+10d]\n",
                -1234, -1234, -1234);

  simple_printf("%% .7d: [% .7d] %% -10.7d: [% -10.7d] %% 10.7d: [% 10.7d]\n",
                1234, 1234, 1234);
  simple_printf("%% .7d: [% .7d] %% -10.7d: [% -10.7d] %% 10.7d: [% 10.7d]\n",
                -1234, -1234, -1234);
  simple_printf("%%+.7d: [%+.7d] %%+-10.7d: [%+-10.7d] %%+10.7d: [%+10.7d]\n",
                1234, 1234, 1234);
  simple_printf("%%+.7d: [%+.7d] %%+-10.7d: [%+-10.7d] %%+10.7d: [%+10.7d]\n",
                -1234, -1234, -1234);

  simple_printf("%%07d:  [%07d]  %%-07d:    [%-07d]    %%07d:    [%07d]\n",
                1234, 1234, 1234);
  simple_printf("%%07d:  [%07d]  %%-07d:    [%-07d]    %%07d:    [%07d]\n",
                -1234, -1234, -1234);
  simple_printf("%% 07d: [% 07d]  %% -07d:   [% -07d]    %% 07d:   [% 07d]\n",
                1234, 1234, 1234);
  simple_printf("%% 07d: [% 07d]  %% -07d:   [% -07d]    %% 07d:   [% 07d]\n",
                -1234, -1234, -1234);
  simple_printf("%%+07d: [%+07d]  %%+-07d:   [%+-07d]    %%+07d:   [%+07d]\n",
                1234, 1234, 1234);
  simple_printf("%%+07d: [%+07d]  %%+-07d:   [%+-07d]    %%+07d:   [%+07d]\n",
                -1234, -1234, -1234);

  simple_printf("\nWidth from '*', string \"x\":\n");
  for (int i = -10; i <= 10; ++i) {
    simple_printf("%%*s, * == %+3d:  [%*s]\n", i, i, "x");
  }

  simple_printf("\nPrecision from '*', string \"01234567\":\n");
  for (int i = -10; i <= 10; ++i) {
    simple_printf("%%.*s, * == %+3d:  [%.*s]\n", i, i, "01234567");
  }

  simple_printf("\nZero precision zeros should print nothing: "
                "[%%.d%%.i%%.u%%.o%%.x%%.X] -> [%.d%.i%.u%.o%.x%.X]\n",
                 0, 0, 0, 0, 0, 0);
  simple_printf("Zero width zeros should print something: "
                "[%%*d%%*i%%*u%%*o%%*x%%*X] -> \[%*d%*i%*u%*o%*x%*X]\n",
                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);

  int x;
  simple_printf("Pointer: (void *)&x = %p\n", (void *)&x);

  simple_printf("\nsimple_snprintf with various size buffers:\n");
  for (int i = 0; i <= 50; i += 5) {
    char buf[50];
    x = simple_snprintf(buf, i, "This is a test: %.16llX%.16llX", 
                        0xDEADBEEFDEADBEEFULL, 0xABCDABCDABCDABCDULL);
    simple_printf("x=%d, buf=[%s]\n", x, buf);
  }
}
