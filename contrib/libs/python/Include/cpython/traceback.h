#pragma once

#ifdef USE_PYTHON3
#include <contrib/tools/python3/Include/cpython/traceback.h>
#else
#error "No <cpython/traceback.h> in Python2"
#endif
