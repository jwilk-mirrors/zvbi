/*
 *  libzvbi test
 *  Copyright (C) 2003 Michael H. Schimek
 */

/* $Id: test-bcd.cc,v 1.1.2.6 2006-05-07 06:05:00 mschimek Exp $ */

#include <iostream>
#include <iomanip>

#include <assert.h>

#include "src/zvbi.h"

#define INT_BITS (sizeof (int) * 8)

namespace vbi {
  class Bcd {
    int	value;

  public:
    Bcd () {};
    Bcd (int n) : value (n) {};

    operator int () const
      { return value; };

    int bin (void)
      { return vbi3_bcd2bin (value); };
    Bcd& bcd (int n)
      { value = vbi3_bin2bcd (n); return *this; };

    Bcd operator+ ()
      { return *this; };
    Bcd operator- ()
      { Bcd t (vbi3_neg_bcd (value)); return t; };

    Bcd& operator+= (const Bcd& other)
      { value = vbi3_add_bcd (value, other.value); return *this; };
    Bcd& operator++ ()
      { value = vbi3_add_bcd (value, 0x1); return *this; };
    Bcd operator++ (int)
      { Bcd t = *this; ++*this; return t; };

    Bcd& operator-= (const Bcd& other)
      { value = vbi3_sub_bcd (value, other.value); return *this; };
    Bcd& operator-- ()
      { value = vbi3_sub_bcd (value, 0x1); return *this; };
    Bcd operator-- (int)
      { Bcd t = *this; --*this; return t; };

    bool operator== (int n) const
      { return value == n; };
    bool operator== (const Bcd& other) const
      { return value == other.value; };
    bool operator!= (int n) const
      { return value != n; };
    bool operator!= (const Bcd& other) const
      { return value != other.value; };

    bool valid () const
      { return vbi3_is_bcd (value); };
  };

  static inline Bcd operator+ (Bcd a, const Bcd& b)
    { a += b; return a; }
  static inline Bcd operator- (Bcd a, const Bcd& b)
    { a -= b; return a; }
  static inline
    std::ostream& operator<<	(std::ostream&		stream,
				 const Bcd&		b)
    {
      std::ios::fmtflags flags = stream.flags ();

      stream << std::hex << (int) b;
      stream.flags (flags);
      return stream;
    }

  typedef Bcd Pgno;
  typedef Bcd Subno;

  static const Bcd any_subno (VBI3_ANY_SUBNO);
  static const Bcd no_subno (VBI3_NO_SUBNO);
};

static int
modulo				(int			n)
{
	static const int m = -VBI3_BCD_BIN_MIN;

	if (n == VBI3_BCD_BIN_MIN)
		return n;
	else if (n < 0)
		return -((-n) % m);
	else
		return n % m;
}

static int
long_random			()
{
	int r = mrand48 ();

	if (INT_BITS > 32)
		r ^= mrand48 () << (INT_BITS - 32);

	return r;
}

static vbi3_bool
digits_greater			(unsigned int		a,
				 unsigned int		b)
{
	unsigned int j;
	for (j = 0; j < (INT_BITS - 4); j += 4)
		if (((a >> j) & 0xF) > ((b >> j) & 0xF))
			return TRUE;

	return FALSE;
}

int
main				(void)
{
	int i;
	int x, y, z;
	vbi::Bcd a, b, c;
	vbi::Bcd d (0x1234);

	assert (0x1234 == d);

	assert (0x3F7F == vbi::any_subno);
	assert (0x3F7F == vbi::no_subno);

	assert (VBI3_BCD_BIN_MIN == vbi3_bcd2bin (VBI3_BCD_MIN));
	assert (VBI3_BCD_BIN_MAX == vbi3_bcd2bin (VBI3_BCD_MAX));

	for (i = -70000; i < 110000; ++i) {
		switch (i / 10000) {
		case 7:
			x = VBI3_BCD_BIN_MAX;
			break;
		case 8:
			x = 0;
			break;
		case 9:
			x = VBI3_BCD_BIN_MIN;
			break;
		case 10:
			x = modulo (long_random ());
			break;
		default:
			x = i;
			break;
		}

		y = vbi3_bin2bcd (x);
		z = vbi3_bcd2bin (y);
		assert (x == z);

		a.bcd (x);

		assert (y == (int) a);
		assert (x == a.bin ());

		assert (a.valid ());

		b = y;
		c.bcd (b.bin ());
		assert ((c == b) == b.valid ());

		b = -a;
		c = -b;
		assert (modulo (-x) == b.bin ());
		assert (modulo (x) == c.bin ());

		switch (i & 0xF00) {
		case 0x200:
			y = VBI3_BCD_BIN_MAX;
			break;
		case 0x700:
			y = 0;
			break;
		case 0xB00:
			y = VBI3_BCD_BIN_MIN;
			break;
		default:
			y = modulo (long_random ());
			break;
		}

		a.bcd (x);
		b.bcd (y);
		c = a; c += b;
		assert (modulo (x + y) == c.bin ());
		c = a + b;
		assert (modulo (x + y) == c.bin ());
		c = a; ++c;
		assert (modulo (x + 1) == c.bin ());
		c = a++;
		assert (modulo (x + 0) == c.bin ());
		assert (modulo (x + 1) == a.bin ());

		assert (a != c);
		assert (a == a);

		a.bcd (x);
		b.bcd (y);
		c = a; c -= b;
		assert (modulo (x - y) == c.bin ());
		c = a - b;
		assert (modulo (x - y) == c.bin ());
		c = a; --c;
		assert (modulo (x - 1) == c.bin ());
		c = a--;
		assert (modulo (x - 0) == c.bin ());
		assert (modulo (x - 1) == a.bin ());

		a.bcd (x);
		b.bcd (y);
		c = a + (-b);
		assert (modulo (x - y) == c.bin ());
		c = a - (-b);
		assert (modulo (x + y) == c.bin ());

		if (x > 0) {
			const unsigned int d = (1 << (INT_BITS - 4)) - 1;

			a.bcd (x);
			b.bcd (y);

			assert (digits_greater (d & (int) b, (int) a)
				== vbi3_bcd_digits_greater
				(d & (int) b, (int) a));

			y = d & random();

			assert (digits_greater (y, (int) a)
				== vbi3_bcd_digits_greater (y, (int) a));
		}
	}

	return 0;
}
