#!/usr/bin/env python

import sys
from distutils.core import setup, Extension

setup(
    name         = "InputHooker",
    version      = "0.1",
    description  = "Very cheap 'readline' module handling PyOS_InputHook",
    url          = "http://pyqwt.sourceforge.net",
    author       = "Gerard Vermeulen",
    author_email = "gerard.vermeulen@grenoble.cnrs.fr",
    license      = "GPL",
    ext_modules  = [Extension('inputhooker', ['inputhooker.c']),],
    )

# Local Variables:
# compile-command: "python setup.py build"
# End:

