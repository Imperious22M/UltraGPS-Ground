# NotcursesTmuxDa1.cmake - patch for the bundled notcurses sources, so that the
# TUI starts inside tmux >= 3.4.
#
# Usage: run as a CMake script (`cmake -P`) from the notcurses source
# directory; CMakeLists.txt does this through the PATCH_COMMAND of the
# notcurses_ext ExternalProject when ULTRAGPS_TUI_TERMINAL=tmux. It runs on a
# copy of the notcurses submodule in the build directory, so the submodule
# checkout itself is never modified.
#
# tmux >= 3.4 built with sixel support (Ubuntu 24.04's tmux) answers the
# Primary Device Attributes query with "CSI ? 1 ; 2 ; 4 c". notcurses' input
# automaton has a fixed "[?1;2c" entry that shadows its generic
# "[?\N;\Dc" rule, so the reply is never recognised and notcurses_init()
# blocks forever waiting for it (the TUI calls notcurses_core_init(), which
# does the same). Adding the exact tmux reply as another fixed entry, handled
# by the same da1_cb callback, fixes the hang.
#
# notcurses also asks the terminal to set 256 and then 1024 colour registers
# (XTSMGRAPHICS), and tmux answers with "CSI ? 1 ; 3 ; 256 S" and
# "CSI ? 1 ; 3 ; 1024 S" (status 3 = failure). These do not match either, so
# notcurses passes them on as keystrokes. They are added as ignored entries
# (no callback).
#
# The three entries go into the escape-sequence table of src/lib/in.c, just
# above the stock "[?1;2c" entry, each marked "UltraGPS patch".
# The patch is idempotent.

# The file to patch, relative to the working directory (the notcurses source root)
set(_in "src/lib/in.c")
file(READ "${_in}" _src)
# Already patched (e.g. a local source tree patched by an earlier build):
# nothing to do
string(FIND "${_src}" "[?1;2;4c" _already)
if(NOT _already EQUAL -1)
    message(STATUS "notcurses: tmux DA1 patch already applied")
    return()
endif()
# The stock DA1 entry is the anchor: the new entries are inserted in front of
# it. A notcurses version without it fails the build here instead of being
# left unpatched.
set(_anchor "    { \"[?1;2c\", da1_cb, },")
string(FIND "${_src}" "${_anchor}" _pos)
if(_pos EQUAL -1)
    message(FATAL_ERROR "notcurses: cannot find DA1 table in ${_in}; update notcurses-tmux-patch-cmake/NotcursesTmuxDa1.cmake")
endif()
# Replace the anchor with the three new lines followed by the anchor itself
# (it occurs once in in.c, so only the table changes)
string(REPLACE "${_anchor}"
    "    { \"[?1;2;4c\", da1_cb, }, // tmux >= 3.4 with sixel (UltraGPS patch)\n    { \"[?1;3;256S\", NULL, }, // tmux XTSMGRAPHICS failure (UltraGPS patch)\n    { \"[?1;3;1024S\", NULL, }, // tmux XTSMGRAPHICS failure (UltraGPS patch)\n${_anchor}"
    _src "${_src}")
file(WRITE "${_in}" "${_src}")
message(STATUS "notcurses: applied tmux DA1 patch")
