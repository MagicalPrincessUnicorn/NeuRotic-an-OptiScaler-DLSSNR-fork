"""Check the built addon uses runtime initialization for its STL mutex."""
import subprocess, sys
tool='C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/Llvm/x64/bin/llvm-objdump.exe'
imports=subprocess.check_output([tool,'-p',sys.argv[1]],text=True)
assert '_Mtx_lock' in imports, 'Expected registry mutex consumer'
assert '_Mtx_init_in_situ' in imports, 'FAIL: registry mutex lacks runtime initialization required by Detroit local VC 14.16'
print('PASS: built addon imports runtime mutex initialization and locking')
