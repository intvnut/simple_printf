#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>

/* Assume MSB is sign bit. */
#define SIGN_BIT (ULLONG_MAX - ULLONG_MAX / 2)

static const char *const hex_digits[2] = {
  "0123456789abcdef", "0123456789ABCDEF"
};

/* Prints an integer in the specified base. */
void print_integer(unsigned long long value, bool is_signed, bool is_caps,
                   unsigned base) {
  bool is_negative = false;
  char buf[24];

  if (is_signed && (value & SIGN_BIT)) {
    is_negative = true;
    value = -value;
  }

  int idx = 24;
  buf[--idx] = '\0';
  do {
    buf[--idx] = hex_digits[is_caps][value % base];
    value /= base;
  } while (value > 0);

  if (is_negative) {
    buf[--idx] = '-';
  }

  fputs(buf + idx, stdout);
}

/*
 * Simplified printf that understands:
 *  -- Strings: %s
 *  -- Signed decimal integers: %d, %ld, %lld, %i, %li, %lli
 *  -- Unsigned decimal integers: %u, %lu, %llu
 *  -- Lowercase hexadecimal integers: %x, %lx, %llx
 *  -- Uppercase hexadecimal integers: %X, %lX, %llX
 *  -- Printing % with %%.
 */
void simple_printf(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);

  for (int ch = *fmt++; ch; ch = *fmt++) {
    /* If it's not %, just print the character. */
    if (ch != '%') {
      fputc(ch, stdout);
      continue;
    }

    /* It's (potentially) a conversion. Let's take a look. */
    const char *initial = fmt;
    int conv = *fmt++;
    int longlong = 0;

    /* Look for long and long long modifiers. */
    while (conv == 'l') {
        longlong++;
        conv = *fmt++;
    }

    if (longlong > 2) {
        /* Overly long "long long".  Print the '%' and skip the conversion. */
        fmt = initial;
        putchar('%');
        continue;
    }

    bool is_caps = false;
    bool is_signed = true;
    int base = 10;

    switch (conv) {
      case 's': {
        /* If longlong is non-zero, this is a failed conversion. */
        if (longlong > 0) {
            fmt = initial;
            putchar('%');
            continue;
        }

        /* %s is a string. */
        const char *s = va_arg(args, const char *);
        fputs(s, stdout);
        break;
      }

      case 'o':     /* %o is an unsigned octal integer. */
        base = 8;
        /* FALLTHROUGH_INTENDED */
      case 'X':     /* %X is an unsigned hexadecimal integer in caps. */
        is_caps = true;
        /* FALLTHROUGH_INTENDED */
      case 'x':     /* %x is an unsigned hexadecimal integer in lowercase. */
        base = base == 10 ? 16 : base;  /* Set hex unless we fell through. */
        /* FALLTHROUGH_INTENDED */
      case 'u':     /* %u is an unsigned decimal integer. */
        is_signed = false;
        /* FALLTHROUGH_INTENDED */
      case 'i':     /* %d and %i are signed decimal integers. */
      case 'd': {
        unsigned long long v;
        if (is_signed) {
          v = longlong == 0 ? (unsigned long long)va_arg(args, int)
            : longlong == 1 ? (unsigned long long)va_arg(args, long)
            :                 (unsigned long long)va_arg(args, long long);
        } else {
          v = longlong == 0 ? va_arg(args, unsigned)
            : longlong == 1 ? va_arg(args, unsigned long)
            :                 va_arg(args, unsigned long long);
        }

        print_integer(v, is_signed, is_caps, base);
        break;
      }

      case '%': {
        /* %% prints '%' */
        putchar('%');
        break;
      }

      default: {
        /* Not a valid conversion.  Print the '%' and back up. */
        putchar('%');
        --fmt;
        break;
      }
    }
  }

  va_end(args);
}

int main() {
  simple_printf("Hello %s, the answer is %d.\n", "world", 42);
  simple_printf("Zero: %d\n", 0);
  simple_printf("Positive %%d: %d\n", 123456789);
  simple_printf("Negative %%d: %d\n", -123456789);
  simple_printf("Positive %%i: %i\n", 123456789);
  simple_printf("Negative %%i: %i\n", -123456789);
  simple_printf("Unsigned %%u: %u\n", 4000000000U);
  simple_printf("Octal    %%o: %o\n", 123456789);
  simple_printf("Octal    %%o: %o\n", -123456789);
  simple_printf("Octal    %%o: %o\n", 4000000000U);
  simple_printf("Hex      %%x: %x\n", 123456789);
  simple_printf("Hex      %%x: %x\n", -123456789);
  simple_printf("Hex      %%x: %x\n", 4000000000U);
  simple_printf("Hex      %%X: %X\n", 123456789);
  simple_printf("Hex      %%X: %X\n", -123456789);
  simple_printf("Hex      %%X: %X\n", 4000000000U);

  simple_printf("Positive %%ld: %ld\n", 123456789L);
  simple_printf("Negative %%ld: %ld\n", -123456789L);
  simple_printf("Positive %%li: %li\n", 123456789L);
  simple_printf("Negative %%li: %li\n", -123456789L);
  simple_printf("Unsigned %%lu: %lu\n", 4000000000UL);
  simple_printf("Octal    %%lx: %lo\n", 123456789L);
  simple_printf("Octal    %%lx: %lo\n", -123456789L);
  simple_printf("Octal    %%lx: %lo\n", 4000000000UL);
  simple_printf("Hex      %%lx: %lx\n", 123456789L);
  simple_printf("Hex      %%lx: %lx\n", -123456789L);
  simple_printf("Hex      %%lx: %lx\n", 4000000000UL);
  simple_printf("Hex      %%lX: %lX\n", 123456789L);
  simple_printf("Hex      %%lX: %lX\n", -123456789L);
  simple_printf("Hex      %%lX: %lX\n", 4000000000UL);

  simple_printf("Positive %%lld: %lld\n", 123456789123456789LL);
  simple_printf("Negative %%lld: %lld\n", -123456789123456789LL);
  simple_printf("Positive %%lli: %lli\n", 123456789123456789LL);
  simple_printf("Negative %%lli: %lli\n", -123456789123456789LL);
  simple_printf("Unsigned %%llu: %llu\n", 4000000000000000000ULL);
  simple_printf("Octal    %%llo: %llo\n", 123456789123456789LL);
  simple_printf("Octal    %%llo: %llo\n", -123456789123456789LL);
  simple_printf("Octal    %%llo: %llo\n", 4000000000000000000ULL);
  simple_printf("Hex      %%llx: %llx\n", 123456789123456789LL);
  simple_printf("Hex      %%llx: %llx\n", -123456789123456789LL);
  simple_printf("Hex      %%llx: %llx\n", 4000000000000000000ULL);
  simple_printf("Hex      %%llX: %llX\n", 123456789123456789LL);
  simple_printf("Hex      %%llX: %llX\n", -123456789123456789LL);
  simple_printf("Hex      %%llX: %llX\n", 4000000000000000000ULL);
}
