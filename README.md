# Simple `printf()`

I recently received an answer request on Quora regarding the algorithms
behind `printf()`.  I decided to throw together a simple `printf()`
implementation to show the basics.

And then I got carried away, adding more functionality.

This directory contains the results.

None of these is a complete implementation of `printf()`, although version 6
implements most of what C99 specifies.  None of them implements floating point
conversions.

From version 6:

```
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
```

I've performed some basic testing, but otherwise _caveat emptor._  I wrote
these from scratch over a weekend.

Enjoy!

----
```
Copyright 2023, Joe Zbiciak <joe.zbiciak@leftturnonly.info>
SPDX-License-Identifier:  CC-BY-SA-4.0
```
