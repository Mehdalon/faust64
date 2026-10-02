#!/usr/bin/env python3
"""Post-process Faust's -fx output so it actually compiles.

Faust declares each wavetable as `sfx_t(m,l) ftbl...[]` but declares the helper
that fills it as taking `fixpoint_t*`. Those are two different types, so the
call does not compile. This is a bug in Faust's fixed-point C backend, not
something a support header can paper over - it fails against Xilinx's ap_fixed
in exactly the same way.

The least invasive fix is to declare the tables as `fixpoint_t` too, which is
the type the fill helper already expects. Every read of the table is already
wrapped in an explicit cast to the format the caller wants, so the conversion
is handled by the type's own constructor. Nothing else is touched.
"""
import re, sys

path = sys.argv[1]
src = open(path).read()
src, n = re.subn(r"static sfx_t\(\s*-?\d+\s*,\s*-?\d+\s*\)(\s+ftbl\w+\[)",
                 r"static fixpoint_t\1", src)
open(path, "w").write(src)
print("fxfixup: retyped %d wavetable(s) to fixpoint_t" % n)
