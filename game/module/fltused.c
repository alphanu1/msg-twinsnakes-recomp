/* Definition of the Windows floating-point usage marker _fltused.
 *
 * LLVM marks a translation unit that uses floating point (and may call
 * varargs) by referencing _fltused; the MSVC CRT provides it, the
 * llvm-mingw runtime in this link does not. The GGSJA4 REL generation
 * references it from 7988 objects (the GGSEA4 generation references it
 * from none), so the module link failed with "undefined symbol:
 * _fltused" for that build only. Nothing reads the value; it only has
 * to exist. Built as its own static library (see CMakeLists.txt) so the
 * linker pulls it only when some object actually references it -
 * builds that define it themselves or never reference it are unaffected.
 */
int _fltused = 0;
