# The debug descriptor on the NUCLEO-F446RE through OpenOCD's GDB server (port 3333).
# Run from the repository root: gdb -q -batch -x <this file> <the controller's ELF>
set pagination off
set confirm off
set backtrace limit 64
python import sys; sys.path.insert(0, "tools/descriptor")
source tools/descriptor/emb_gdb.py
target extended-remote :3333
monitor halt
info registers pc sp msp psp
emb info
emb threads
emb bt 0
emb bt 1
emb bt 2
info registers pc sp msp psp
monitor resume
