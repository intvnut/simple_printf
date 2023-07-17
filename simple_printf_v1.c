#include <stdarg.h>
#include <stdio.h>

/* Prints a signed integer to stdout. */
void print_int(int d) {
  unsigned u = d;

  /*
   * Handle negative numbers.  This might look odd, but it avoids undefined
   * behavior for the largest negative number by negating the _unsigned_ value
   * after testing the sign of the signed value.
   */
  if (d < 0) {
    putchar('-');
    u = -u;
  }

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


/* Simplified printf that only understands %s, %d, and %%. */
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

      case 'd': {
        /* %d is a signed integer. */
        int d = va_arg(args, int);
        print_int(d);
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
  simple_printf("Positive: %d\n", 123456789);
  simple_printf("Negative: %d\n", -123456789);
  simple_printf("Print a percent: %%\n");
  simple_printf("Invalid conversion: %q\n");
}
