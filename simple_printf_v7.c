#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
/*
 * Copyright 2023, J. Zbiciak <joe.zbiciak@leftturnonly.info>
 * Author:  Joe Zbiciak <joe.zbiciak@leftturnonly.info>
 * SPDX-License-Identifier:  CC-BY-SA-4.0
 */

/*
 * Some conversions need the "signed integer type corresponding to size_t."
 * The language spec doesn't name the type, so try to determine it.
 */
#if ULLONG_MAX == SIZE_MAX
typedef long long ssize_type;
#elif ULONG_MAX == SIZE_MAX
typedef long ssize_type;
#elif UINT_MAX == SIZE_MAX
typedef int ssize_type;
#else
#error Could not determine signed type corresponding to size_t.
#endif

/*
 * Some conversions need the "unsigned integer type corresponding to ptrdiff_t."
 * The language spec doesn't name the type, so try to determine it.
 */
#if LLONG_MAX == PTRDIFF_MAX
typedef unsigned long long uptrdiff_type;
#elif LONG_MAX == PTRDIFF_MAX
typedef unsigned long uptrdiff_type;
#elif INT_MAX == PTRDIFF_MAX
typedef unsigned uptrdiff_type;
#else
#error Could not determine unsigned type corresponding to ptrdiff_t.
#endif


/* Operand sizes: */    /* Mod   diouxX conversions                 cs convs */
enum {                  /* ----  ---------------------------------  -------- */
  kLengthChar     = -2, /* hh    signed char, unsigned char                  */
  kLengthShort    = -1, /*  h    short, unsigned short                       */
  kLengthDefault  =  0, /* none  int, unsigned int, double,         char     */
  kLengthLong     =  1, /*  l    long, unsigned long,               wchar_t  */
  kLengthLongLong =  2, /* ll    long long int, unsigned long long           */
  kLengthIntMaxT  =  3, /*  j    intmax_t                                    */
  kLengthSizeT    =  4, /*  z    size_t                                      */
  kLengthPtrDiffT =  5, /*  t    ptrdiff_t                                   */

  kLengthVoidP    =  6  /* For %p, ignoring modifiers. */
};

/* Sign display: */     /* Flag   Non-negative values   Negative values      */
enum {                  /* -----  --------------------  -------------------- */
  kSignDefault = 0,     /* none   Nothing               '-'                  */
  kSignAlways,          /*  +     '+'                   '-'                  */
  kSignSpace            /* space  ' '                   '-'                  */
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

/*
 * Conversion spec.  This is designed so that a default of all-zeros / false 
 * gives the desired result.
 */
struct conv {
  bool          leading_zero;        /* Leading-zero flag.                   */
  bool          left_justify;        /* Left justification flag.             */
  bool          is_alt;              /* Alternate form flag.                 */
  char          sign;                /* Sign display flag.                   */
  char          length;              /* Length modifier (operand size).      */
  bool          explicit_width;      /* True if user-provided width.         */
  bool          explicit_prec;       /* True if user-provided precision.     */
  short         width;               /* Width of the field.                  */
  short         prec;                /* Precision of the field.              */
  bool          soft_prec;           /* Width converted to "precision."      */
  bool          is_caps;             /* Print hex values in capital letters. */
  bool          is_signed;           /* Perform a signed integer conversion. */
  unsigned char base;                /* Default radix is decimal.            */
  char          type;                /* Actual conversion to perform.        */
  
  /* Additional details for handling the conversion. */
  va_list        *restrict args;     /* Argument list.                       */
  struct printer *restrict printer;  /* Where to send output.                */
};


/* Forward declarations for parsing functions. */
static const char *parse_flags (const char *fmt, struct conv *restrict conv);
static const char *parse_width (const char *fmt, struct conv *restrict conv);
static const char *parse_prec  (const char *fmt, struct conv *restrict conv);
static const char *parse_length(const char *fmt, struct conv *restrict conv);

/* Forward declarations for format conversions. */
static bool print_conversion         (struct conv *restrict conv);
static bool print_char_conversion    (struct conv *restrict conv);
static bool print_string_conversion  (struct conv *restrict conv);
static bool print_diouxXp_conversions(struct conv *restrict conv);
static bool store_character_count    (struct conv *restrict conv);

/* Forward declarations for argument fetches. */
static uintmax_t get_signed_integer  (struct conv *restrict conv);
static uintmax_t get_unsigned_integer(struct conv *restrict conv);

/* Utility functions used by the various conversions. */
static int convert_integer_to_string(
    uintmax_t value, struct conv *restrict conv, char *restrict buf);
static bool print_converted_string(struct conv *restrict conv,
                                   const char *restrict str, int str_len);


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
 *  -- Reporting length of printed string with "n".
 *  -- Returning length of printed string.
 *  -- Printing to a stream other than stdout.
 *  -- Printing to a buffer.
 *
 * Not supported:
 *  -- Floating point.
 *  -- Wide characters ("%lc").
 *  -- Wide character strings ("%ls").
 *
 * I guess it's no longer so simple...
 */
static void printf_core(struct printer *p, const char *fmt, va_list args) {
  const char *curr_fmt = fmt;
  const char *prev_fmt = fmt;                 /* Previous format pointer. */
  const char *term_fmt = fmt + strlen(fmt);   /* Null terminator in format. */

  for (curr_fmt = memchr(curr_fmt, '%', term_fmt - curr_fmt); curr_fmt;
       prev_fmt = curr_fmt,
       curr_fmt = memchr(curr_fmt, '%', term_fmt - curr_fmt)) {
    /* Output any batched up non-conversion characters in format. */
    if (prev_fmt != curr_fmt) { p->copy(p, prev_fmt, curr_fmt - prev_fmt); }

    /* It's (potentially) a conversion. Let's take a look. */
    const char *conv_fmt = curr_fmt++;  /* Point to first char of conversion. */
    struct conv conv = { .base = 10, .args = &args, .printer = p };

    /* Look for exactly "%%", so that errors like "%l%d" don't print as '%'. */
    if (*curr_fmt == '%') { p->putc(p, '%'); curr_fmt++; continue; }

    curr_fmt = parse_flags (curr_fmt, &conv);
    curr_fmt = parse_width (curr_fmt, &conv);
    curr_fmt = parse_prec  (curr_fmt, &conv);
    curr_fmt = parse_length(curr_fmt, &conv);

    conv.type = *curr_fmt++;

    if (!print_conversion(&conv)) {
      /* Failed conversion. Print failed conversion specifier. */
      p->copy(p, conv_fmt, curr_fmt - conv_fmt);
    }
  }

  /* Print the tail. */
  if (prev_fmt != term_fmt) { p->copy(p, prev_fmt, term_fmt - prev_fmt); }
}


/* Parses any flags that are present.  They can appear in any order. */
static const char *parse_flags(const char *fmt, struct conv *restrict conv) {
  bool sign_space = false;

  for (bool done_flags = false; !done_flags;) {
    char ch = *fmt++;
    switch (ch) {
      case '0': { conv->leading_zero = true; break; }
      case '-': { conv->left_justify = true; break; }
      case '+': { conv->sign = kSignAlways;  break; }
      case '#': { conv->is_alt = true;       break; }
      case ' ': { sign_space = true;         break; }
      default:  { done_flags = true; --fmt;  break; }
    }
  }

  /* ' ' takes effect only if '+' isn't also provided. */
  if (sign_space && conv->sign == kSignDefault) { conv->sign = kSignSpace; }

  return fmt;
}

/* Parses the width specifier, if present. */
static const char *parse_width(const char *fmt, struct conv *restrict conv) {
  char ch = *fmt;
  int width = 0;

  /* Check for width. */
  if (ch == '*') {  /* Width provided as an int argument. */
    fmt++;
    conv->explicit_width = true;
    width = va_arg(*conv->args, int);

    if (width < 0) {  /* Negative width specifies left justification. */
      conv->left_justify = true;
      width = -width;
    }
  } else {  /* Width is a decimal number in the format string. */
    conv->explicit_width = isdigit(ch);
    while (isdigit(ch)) {
      width = width * 10 + (ch - '0');
      ch = *++fmt;
    }
  }

  conv->width = width;
  return fmt;
}

/* Parses the precision specifier, if present. */
static const char *parse_prec(const char *fmt, struct conv *restrict conv) {
  char ch = *fmt;
  int prec = 0;

  /* Check for precision. Always preceded by a "." */
  if (ch == '.') {
    conv->explicit_prec = true;
    ch = *++fmt;

    if (ch == '*') {  /* Precision provided as an int argument. */
      ++fmt;
      prec = va_arg(*conv->args, int);
      if (prec < 0) { prec = 0; } /* Negative precision acts like 0. */
    } else {  /* Precision is a decimal number in the format string. */
      while (isdigit(ch)) {
        prec = prec * 10 + (ch - '0');
        ch = *++fmt;
      }
    }
  }

  conv->prec = prec;
  return fmt;
}

/*
 * Parses length modifierss "hh", "h", "l", "ll", "j", "z", "t", and peeks
 * ahead for "p" as it has an implicit, fixed size.
 */
static const char *parse_length(const char *fmt, struct conv *restrict conv) {
  int length = kLengthDefault;
  int ch1 = fmt[0], ch2 = fmt[1];

# define PACK_CHAR_(a, b) (((a) & UCHAR_MAX) | (((a) == (b)) << CHAR_BIT))

  switch (PACK_CHAR_(ch1, ch2)) {
    case PACK_CHAR_('h', 'h'): { length = kLengthChar;     fmt += 2; break; }
    case PACK_CHAR_('h', ' '): { length = kLengthShort;    fmt += 1; break; }
    case PACK_CHAR_('l', ' '): { length = kLengthLong;     fmt += 1; break; }
    case PACK_CHAR_('l', 'l'): { length = kLengthLongLong; fmt += 2; break; }
    case PACK_CHAR_('j', ' '): { length = kLengthIntMaxT;  fmt += 1; break; }
    case PACK_CHAR_('z', ' '): { length = kLengthSizeT;    fmt += 1; break; }
    case PACK_CHAR_('t', ' '): { length = kLengthPtrDiffT; fmt += 1; break; }
    case PACK_CHAR_('p', ' '): { length = kLengthVoidP;              break; }
  }

# undef PACK_CHAR_

  conv->length = length;
  return fmt;
}

/* Dispatches to appropriate conversion and prints. Returns true on success. */
static bool print_conversion(struct conv *restrict conv) {
  /* Now look for the actual conversion. */
  switch (conv->type) {
    case 'n': { return store_character_count  (conv); }
    case 'c': { return print_char_conversion  (conv); }
    case 's': { return print_string_conversion(conv); }
    case 'd': case 'i': case 'u': case 'o': case 'x': case 'X': case 'p': {
      return print_diouxXp_conversions(conv);
    }
  }

  return false;  /* Not a valid / supported conversion. */
}

/* Prints %c conversions. */
static bool print_char_conversion(struct conv *restrict conv) {
  /* For now, we don't support %lc. */
  if (conv->length != kLengthDefault) { return false; }

  char c = (unsigned char)va_arg(*conv->args, int);
  return print_converted_string(conv, &c, 1);
}

/* Prints %s conversions. */
static bool print_string_conversion(struct conv *restrict conv) {
  /* For now, we don't support %ls. */
  if (conv->length != kLengthDefault) { return false; }

  const size_t      max_len = conv->explicit_prec ? conv->prec : SIZE_MAX;
  const char *const str     = va_arg(*conv->args, const char *);
  const char *const end     = memchr(str, '\0', max_len);
  const int         str_len = end ? end - str : max_len;
  return print_converted_string(conv, str, str_len);
}

/*
 * Buffer size for converting integers.  This should be enough for a 128-bit
 * intmax_t, with sign or 0x prefix and terminating null, with some extra room.
 * My current platform only has a 64-bit intmax_t, however, so 128-bit is
 * not tested.
 */
#define INT_BUF_SIZE (48)

/* Prints various integer conversions. */
static bool print_diouxXp_conversions(struct conv *restrict conv) {
  char buf[INT_BUF_SIZE];
  conv->is_signed = false;  /* default. */

  switch (conv->type) {
    case 'd': { conv->is_signed = true;                break; }  /* signed   */
    case 'i': { conv->is_signed = true;                break; }  /* signed   */
    case 'u': {                                        break; }  /* unsigned */
    case 'o': { conv->base = 8;                        break; }  /* octal    */
    case 'x': { conv->base = 16;                       break; }  /* hex      */
    case 'X': { conv->base = 16; conv->is_caps = true; break; }  /* HEX      */
    case 'p': { conv->base = 16; conv->is_alt = true;  break; }  /* (void *) */
  }

  if (!conv->explicit_prec) {
    /*
     * If provided an explicit non-zero width but no precision and asked to
     * zero pad, treat the width like a "soft" precision that can be eaten
     * into by the sign and a radix prefix if needed.
     */
    if (conv->leading_zero && conv->explicit_width && conv->width &&
        !conv->left_justify) {
      conv->prec      = conv->width;
      conv->soft_prec = true;
    } else {
      conv->prec = 1;
    }
  }

  const uintmax_t value = conv->is_signed ? get_signed_integer(conv)
                                          : get_unsigned_integer(conv);
  const int idx         = convert_integer_to_string(value, conv, buf);
  const int str_len     = INT_BUF_SIZE - idx - 1;
  return print_converted_string(conv, buf + idx, str_len);
}




/* Stores the current character count to the appropriate sort of pointer. */
static bool store_character_count(struct conv *restrict conv) {
  const uintmax_t t = conv->printer->total;

  /* The length modifier determines the data type of the pointer argument. */
  switch (conv->length) {
    case kLengthChar:     { *va_arg(*conv->args, signed char *) = t; break; }
    case kLengthShort:    { *va_arg(*conv->args, short *)       = t; break; }
    case kLengthDefault:  { *va_arg(*conv->args, int *)         = t; break; }
    case kLengthLong:     { *va_arg(*conv->args, long *)        = t; break; }
    case kLengthLongLong: { *va_arg(*conv->args, long long *)   = t; break; }
    case kLengthIntMaxT:  { *va_arg(*conv->args, intmax_t *)    = t; break; }
    case kLengthSizeT:    { *va_arg(*conv->args, size_t *)      = t; break; }
    case kLengthPtrDiffT: { *va_arg(*conv->args, ptrdiff_t *)   = t; break; }
    /* Unknown:  Guess (int *). */
    default:              { *va_arg(*conv->args, int *)         = t; break; }
  }

  return true;
}

/* Gets a signed argument of the specified size. */
static uintmax_t get_signed_integer(struct conv *restrict conv) {
  switch (conv->length) {
    case kLengthChar:     { return (signed char )va_arg(*conv->args, int);   }
    case kLengthShort:    { return (signed short)va_arg(*conv->args, int);   }
    case kLengthDefault:  { return va_arg(*conv->args, int);                 }
    case kLengthLong:     { return va_arg(*conv->args, long);                }
    case kLengthLongLong: { return va_arg(*conv->args, long long);           }
    case kLengthIntMaxT:  { return va_arg(*conv->args, intmax_t);            }
    case kLengthSizeT:    { return va_arg(*conv->args, ssize_type);          }
    case kLengthPtrDiffT: { return va_arg(*conv->args, ptrdiff_t);           }
    case kLengthVoidP:    { return (uintptr_t)va_arg(*conv->args, void *);   }
    /* Unknown:  Guess int. */
    default:              { return va_arg(*conv->args, int);                 }
  }
}

/* Gets an unsigned argument of the specified length. */
static uintmax_t get_unsigned_integer(struct conv *restrict conv) {
  switch (conv->length) {
    case kLengthChar:     { return (unsigned char )va_arg(*conv->args, int); }
    case kLengthShort:    { return (unsigned short)va_arg(*conv->args, int); }
    case kLengthDefault:  { return va_arg(*conv->args, unsigned);            }
    case kLengthLong:     { return va_arg(*conv->args, unsigned long);       }
    case kLengthLongLong: { return va_arg(*conv->args, unsigned long long);  }
    case kLengthIntMaxT:  { return va_arg(*conv->args, uintmax_t);           }
    case kLengthSizeT:    { return va_arg(*conv->args, size_t);              }
    case kLengthPtrDiffT: { return va_arg(*conv->args, uptrdiff_type);       }
    case kLengthVoidP:    { return (uintptr_t)va_arg(*conv->args, void *);   }
    /* Unknown:  Guess unsigned. */
    default:              { return va_arg(*conv->args, unsigned);            }
  }
}


/* Assume MSB is sign bit. */
#define SIGN_BIT (ULLONG_MAX - ULLONG_MAX / 2)

/* Digits for printing. */
static const char hex_digits[2][17] = {
  "0123456789abcdef", "0123456789ABCDEF"
};

/*
 * Converts an integer in the specified base, stored at the _end_ of buf[].
 * Returns the index of the first character.
 */
static int convert_integer_to_string(
    uintmax_t value, struct conv *restrict conv, char *restrict buf) {
  char sign_char = '\0';  /* Default, no sign character. */
  int idx = INT_BUF_SIZE;

  buf[--idx] = '\0';

  /* Print nothing if value and precision are both 0, and not alt octal. */
  if (!value && !conv->prec && !(conv->is_alt && conv->base == 8)) {
    return INT_BUF_SIZE - 1;
  }

  /* Determine sign and take absolutve value, for signed conversions. */
  if (conv->is_signed) {
    if      (value & SIGN_BIT)          { sign_char = '-'; value = -value; }
    else if (conv->sign == kSignAlways) { sign_char = '+';                 }
    else if (conv->sign == kSignSpace)  { sign_char = ' ';                 } 
  }

  /* Convert the digits, starting with the least significant. */
  const char *const hd = hex_digits[conv->is_caps];
  const unsigned base = conv->base;
  do {
    buf[--idx] = hd[value % base];
    value /= base;
  } while (value > 0);

  /*
   * If our precision actually came from the width field, adjust it based on
   * other things we might print before the padding zeros.
   */
  if (conv->soft_prec) {
    if (conv->is_alt && conv->base == 16)                   { conv->prec -= 2; }
    if (conv->is_alt && conv->base == 8 && buf[idx] != '0') { conv->prec -= 1; }
    if (sign_char)                                          { conv->prec -= 1; }

    if (conv->prec < 1) { conv->prec = 1; }  /* Not too small! */
  }

  /*
   * Compute index for padding zeros, out to precision.  Bound the number of
   * leading zeros we support to what fits in our buffer.  This does break
   * standards compliance, as it wants us to support up to 4095 characters in
   * a conversion.
   */
  int prec_idx = conv->prec < INT_BUF_SIZE - 1 ? INT_BUF_SIZE - conv->prec : 1;

  /* Leave room for "0x"/"0" if alt-form hex or octal, and room for sign. */
  if (prec_idx < 3 && conv->is_alt && conv->base == 16) { prec_idx = 3; }
  if (prec_idx < 2 && conv->is_alt && conv->base == 8)  { prec_idx = 2; }
  if (prec_idx < 2 && sign_char)                        { prec_idx = 2; }

  /* Add leading zeros out to precision index. */
  while (idx >= prec_idx) { buf[--idx] = '0'; }

  /* If we're alternate-form octal, add a leading 0 if needed. */
  if (conv->is_alt && conv->base == 8 && buf[idx] != '0') { buf[--idx] = '0'; }

  /* If we're alternate-form hex, add a leading "0x" or "0X". */
  if (conv->is_alt && conv->base == 16) {
    buf[--idx] = conv->is_caps ? 'X' : 'x';
    buf[--idx] = '0';
  }

  /* Add the sign character, if any. */
  if (sign_char) { buf[--idx] = sign_char; }

  return idx;
}

/* Prints a converted string in a particular width field. */
static bool print_converted_string(struct conv *restrict conv,
                                   const char *restrict str, int str_len) {
  const int fill_count = conv->width > str_len ? conv->width - str_len : 0;
  struct printer *restrict p = conv->printer;

  if (!conv->left_justify && fill_count) { p->fill(p, ' ', fill_count); }
  p->copy(p, str, str_len);
  if ( conv->left_justify && fill_count) { p->fill(p, ' ', fill_count); }

  return true;
}







/* Copies a string to a file. */
static void printer_file_copy(struct printer *p, const char *s, size_t len) {
  p->total += len;
  fwrite(s, 1, len, p->file);
}

/* Writes a block of fill characters to a file. */
static void printer_file_fill(struct printer *p, char c, size_t len) {
  char buf[32];
  memset(buf, c, sizeof(buf));

  p->total += len;

  while (len >= sizeof(buf)) {
    fwrite(buf, 1, sizeof(buf), p->file);
    len -= sizeof(buf);
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


/* Copies a string to a buffer. */
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

/* Writes a block of fill characters to a buffer. */
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

/* Copies a character to a buffer. */
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
  simple_printf("%%c:    [%c]        %%-10c:    [%-10c] %%10c:    [%10c]\n",
                '*', '*', '*');
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
  for (int i = 50; i >= 0; i -= 5) {
    char buf[50];
    x = simple_snprintf(buf, i, "This is a test: %.16llX%.16llX",
                        0xDEADBEEFDEADBEEFULL, 0xABCDABCDABCDABCDULL);
    simple_printf("x=%d, buf=[%s]\n", x, buf);
  }

  simple_printf("\nTesting %%n with different widths.\n");
  char       hh0 = -99,   hh1 = -99;
  short      h0  = -9999, h1  = -9999;
  int        i0  = -9999, i1  = -9999;
  long       l0  = -9999, l1  = -9999;
  long long  ll0 = -9999, ll1 = -9999;
  intmax_t   j0  = -9999, j1  = -9999;
  ssize_type z0  = -9999, z1  = -9999;
  ptrdiff_t  t0  = -9999, t1  = -9999;

  simple_printf(
      "ABCDE%hhn%hn%n%ln%lln%jn%zn%tnFGHIJ%hhn%hn%n%ln%lln%jn%zn%tn\n",
      &hh0, &h0, &i0, &l0, &ll0, &j0, &z0, &t0,  /* all should be 5 */
      &hh1, &h1, &i1, &l1, &ll1, &j1, &z1, &t1); /* all should be 10 */
  simple_printf(
      "hh0=%d, h0=%d, i0=%d, l0=%ld, ll0=%lld, j0=%jd, z0=%zd, t0=%td\n",
      hh0, h0, i0, l0, ll0, j0, z0, t0);
  simple_printf(
      "hh1=%d, h1=%d, i1=%d, l1=%ld, ll1=%lld, j1=%jd, z1=%zd, t1=%td\n",
      hh1, h1, i1, l1, ll1, j1, z1, t1);
}
