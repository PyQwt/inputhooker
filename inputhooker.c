// Copyright (C) 2007 Gerard Vermeulen
//
// InputHooker is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// InputHooker is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with InputHooker; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.


#include "Python.h"

#if !defined(HAVE_READLINE)
#if defined(MS_WINDOWS) || defined(HAVE_SELECT)

// ih_fgets and ih_readline have been borrowed from:
// (1) Python-2.5/Parser/myreadline.c
// (2) myreadline.c.20050803.diff (patch applies cleanly)
// https://sourceforge.net/tracker/?func=detail&atid=305470&aid=1049855&group_id=5470
// Thanks to Michiel de Hoon for his patch 

#ifdef MS_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include "windows.h"
#include "conio.h"
#endif /* MS_WINDOWS */

/* This function restarts a fgets() after an EINTR error occurred
   except if PyOS_InterruptOccurred() returns true. */

static int
ih_fgets(char *buf, int len, FILE *fp)
{
    char *p;
#ifdef WITH_THREAD
    PyThreadState *thread_state = PyThreadState_GET();
#endif
    for (;;) {
        int has_input = 0;
        int fn = fileno(fp);
#ifdef MS_WINDOWS
        HANDLE hStdIn = (HANDLE) _get_osfhandle(fn);
        int wait_for_kbhit = _isatty(fn);
        
        errno = 0;
        
        while (!has_input) {
            DWORD timeout = 100; /* 100 milliseconds */
            DWORD result;
            if (PyOS_InputHook)
                PyOS_InputHook();
            result = WaitForSingleObject(hStdIn, timeout);
            if (result==WAIT_OBJECT_0) {
                if (!wait_for_kbhit || _kbhit())
                    has_input = 1;
                /* _kbhit returns nonzero if a keystroke is
                 * waiting in the console buffer. Since
                 * WaitForSingleObject returns WAIT_OBJECT_0
                 * in case of both keyboard events and mouse
                 * events, we need to check explicitly if
                 * a keystroke is available. */
                else
                    FlushConsoleInputBuffer(hStdIn);
            }
        }
#else
        fd_set selectset;
        errno = 0;
        
        FD_ZERO(&selectset);
        
        while (!has_input) {
            struct timeval timeout = {0, 100000};
            /* 100000 microseconds */
            FD_SET(fn, &selectset);
            if (PyOS_InputHook)
                PyOS_InputHook();
            /* select resets selectset if no input was available */
            has_input = select(fn+1, &selectset, NULL, NULL, &timeout);
        }
#endif

        if (has_input > 0) {
            p = fgets(buf, len, fp);
            if (p != NULL)
                return 0; /* No error */
        }

#ifdef MS_WINDOWS
        /* In the case of a Ctrl+C or some other external event 
           interrupting the operation:
           Win2k/NT: ERROR_OPERATION_ABORTED is the most recent Win32 
           error code (and feof() returns TRUE).
           Win9x: Ctrl+C seems to have no effect on fgets() returning
           early - the signal handler is called, but the fgets()
           only returns "normally" (ie, when Enter hit or feof())
        */
        if (GetLastError()==ERROR_OPERATION_ABORTED) {
            /* Signals come asynchronously, so we sleep a brief 
               moment before checking if the handler has been 
               triggered (we cant just return 1 before the 
               signal handler has been called, as the later 
               signal may be treated as a separate interrupt).
            */
            Sleep(1);
            if (PyOS_InterruptOccurred()) {
                return 1; /* Interrupt */
            }
            /* Either the sleep wasn't long enough (need a
               short loop retrying?) or not interrupted at all
               (in which case we should revisit the whole thing!)
               Logging some warning would be nice.  assert is not
               viable as under the debugger, the various dialogs
               mean the condition is not true.
            */
        }
#endif /* MS_WINDOWS */
        if (feof(fp)) {
            return -1; /* EOF */
        }
#ifdef EINTR
        if (errno == EINTR) {
            int s;
#ifdef WITH_THREAD
            PyEval_RestoreThread(thread_state);
#endif
            s = PyErr_CheckSignals();
#ifdef WITH_THREAD
            PyEval_SaveThread();
#endif
            if (s < 0) {
                return 1;
            }
        }
#endif
        if (PyOS_InterruptOccurred()) {
            return 1; /* Interrupt */
        }
        return -2; /* Error */
    }
    /* NOTREACHED */
}


/* Readline implementation using ih_fgets() */

char *
ih_readline(FILE *sys_stdin, FILE *sys_stdout, char *prompt)
{
    size_t n;
    char *p;
    n = 100;
    if ((p = (char *)PyMem_MALLOC(n)) == NULL)
        return NULL;
    fflush(sys_stdout);
    if (prompt)
        fprintf(stderr, "%s", prompt);
    fflush(stderr);
    switch (ih_fgets(p, (int)n, sys_stdin)) {
    case 0: /* Normal case */
        break;
    case 1: /* Interrupt */
        PyMem_FREE(p);
        return NULL;
    case -1: /* EOF */
    case -2: /* Error */
    default: /* Shouldn't happen */
        *p = '\0';
        break;
    }
    n = strlen(p);
    while (n > 0 && p[n-1] != '\n') {
        size_t incr = n+2;
        p = (char *)PyMem_REALLOC(p, n + incr);
        if (p == NULL)
            return NULL;
        if (incr > INT_MAX) {
            PyErr_SetString(PyExc_OverflowError, "input line too long");
        }
        if (ih_fgets(p+n, (int)incr, sys_stdin) != 0)
            break;
        n += strlen(p+n);
    }
    return (char *)PyMem_REALLOC(p, n+1);
}

#endif // defined(MS_WINDOWS) || defined(HAVE_SELECT)
#endif // !defined(HAVE_READLINE)


static struct PyMethodDef ih_methods[] =
{
    {0, 0}
};

PyDoc_STRVAR(doc_module,
"Importing this module enables handling of PyOS_InputHook.");

PyMODINIT_FUNC
initinputhooker(void)
{
    PyObject *m;

    m = Py_InitModule4("inputhooker", ih_methods, doc_module,
                       (PyObject *)NULL, PYTHON_API_VERSION);
    if (m == NULL)
        return;
    
#if !defined(HAVE_READLINE)
#if defined(MS_WINDOWS) || defined(HAVE_SELECT)
    PyOS_ReadlineFunctionPointer = ih_readline;
#endif // defined(MS_WINDOWS) || defined(HAVE_SELECT)
#endif // !defined(HAVE_READLINE)
}

// Local Variables:
// mode: C++
// c-file-style: "stroustrup"
// compile-command: "python setup.py build"
// End:
