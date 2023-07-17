#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>

/* Prints an unsigned decimal integer to stdout. */
void print_unsigned_decimal_int(unsigned u) {
  /* Inefficient, but portable. */
  unsigned pow10 = 1;
  unsigned tmp = u;

  while (tmp >= 10) {
    pow10 *= 10;
    tmp /= 10;
  }

  /* Now print the decimalized value. */
  while (pow10 > 0) {
    putchar('0' + u / pow10);
    u %= pow10;
    pow10 /= 10;
  }
}

/* Prints a signed decimal integer to stdout. */
void print_signed_decimal_int(int i) {
  unsigned u = i;

  /*
   * Handle negative numbers.  This might look odd, but it avoids undefined
   * behavior for the largest negative number by negating the _unsigned_ value
   * after testing the sign of the signed value.
   */
  if (i < 0) {
    putchar('-');
    u = -u;
  }

  print_unsigned_decimal_int(u);
}


/* Prints a hexadecimal integer to stdout. */
void print_hexadecimal_int(unsigned u, bool caps) {
  /* Inefficient, but portable. */
  unsigned pow16 = 1;
  unsigned tmp = u;

  while (tmp >= 16) {
    pow16 *= 16;
    tmp /= 16;
  }

  /* Now print the decimalized value. */
  const char *hex_digits = caps ?"0123456789ABCDEF" : "0123456789abcdef";
  while (pow16 > 0) {
    putchar(hex_digits[u / pow16]);
    u %= pow16;
    pow16 /= 16;
  }
}

/* Simplified printf that only understands %s, %d, %i, %u, %x, %X and %%. */
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
    int conv = *fmt++;

    switch (conv) {
      case 's': {
        /* %s is a string. */
        const char *s = va_arg(args, const char *);
        fputs(s, stdout);
        break;
      }

      case 'i':
      case 'd': {
        /* %d and %i are signed integers. */
        int i = va_arg(args, int);
        print_signed_decimal_int(i);
        break;
      }

      case 'u': {
        /* %u is an unsigned integer. */
        unsigned u = va_arg(args, unsigned);
        print_unsigned_decimal_int(u);
        break;
      }

      case 'x':
      case 'X': {
        /* %x and %X are hexadecimal integers. */
        unsigned u = va_arg(args, unsigned);
        print_hexadecimal_int(u, conv == 'X');
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
  simple_printf("Hex      %%x: %x\n", 123456789);
  simple_printf("Hex      %%x: %x\n", -123456789);
  simple_printf("Hex      %%x: %x\n", 4000000000U);
  simple_printf("Hex      %%X: %X\n", 123456789);
  simple_printf("Hex      %%X: %X\n", -123456789);
  simple_printf("Hex      %%X: %X\n", 4000000000U);
}
