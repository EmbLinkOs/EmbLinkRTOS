import hashlib
import json
import subprocess
from pathlib import Path

ROOT = Path('/workspace/EmbCC')
OUT = Path('/workspace/EmbLinkRTOS/build/embcc-capability-review')
OUT.mkdir(exist_ok=False)
record = dict(embcc_sha=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
              rtos_sha=subprocess.check_output(['git','rev-parse','HEAD'],cwd=OUT.parents[1],text=True).strip(),
              commands=[], checks=[])
def run(args, expect=0):
    args=list(map(str,args))
    result=subprocess.run(args,cwd=ROOT,capture_output=True,text=True,timeout=60)
    record['commands'].append(dict(args=args,exit_code=result.returncode,stdout=result.stdout,stderr=result.stderr))
    (OUT/'manifest.json').write_text(json.dumps(record,indent=2,sort_keys=True)+'\n')
    if expect==0 and result.returncode!=0:
        raise RuntimeError(f'{args}: {result.stderr[-1000:]}')
    if expect!=0 and result.returncode==0:
        raise RuntimeError(f'expected refusal: {args}')
    return result.stdout+result.stderr
def source(name,body):
    path=OUT/name
    path.write_text(body)
    return path
def compile(path,target,extra=()):
    obj=OUT/(path.stem+'-'+target+'.o')
    run([ROOT/'embcc','--target='+target,'-O2',*extra,'-c',path,'-o',obj])
    return obj
def check(number,description,scope):
    record['checks'].append(dict(legacy_item=number,description=description,scope=scope,outcome='pass'))
    print(number,description,scope,flush=True)
    (OUT/'manifest.json').write_text(json.dumps(record,indent=2,sort_keys=True)+'\n')

splim=source('splim.c','void set(unsigned x){__asm__ volatile("msr msplim, %0"::"r"(x));__asm__ volatile("msr psplim, %0"::"r"(x));}\nunsigned get(void){unsigned x;__asm__ volatile("mrs %0, psplim":"=r"(x));return x;}\n')
compile(splim,'thumbv8m.main-none-eabi')
bad=run([ROOT/'embcc','--target=thumbv7m-none-eabi','-c',splim,'-o',OUT/'bad.o'],expect=1)
assert 'not an ARMv7-M special register' in bad
check(1,'MSPLIM/PSPLIM compile on Mainline; refused on ARMv7-M','compile/refusal; hardware unrun')

# Use upstream's real AVR startup/I/O/runtime and linker-script fixture.
avr_objs=[]
for name in ('tests/harness/avr/boot.S','tests/harness/avr/io.c','lib/rt/avr.c','tests/golden/avr-ldscript/prog.c'):
    avr_objs.append(compile(ROOT/name,'avr'))
image=OUT/'avr-script.elf'
run([ROOT/'embld','-T',ROOT/'tests/golden/avr-ldscript/m328p.ld',*avr_objs,'-o',image])
sections=run(['readelf','-SW',image])
assert '.data' in sections and '00800100' in sections
expected='zero one two three \n140 12 300000 13 24 -12 abcdefghijklmno\n==END=='
text=run([ROOT/'embsim',image,'--board','uno','--until','==END==','--max-insns','2000000'])
assert expected in text.replace('\r','')
check(2,'AVR GNU linker script lays out .data at 0x800100 and executes fixture','EmbSim AVR; no physical/QEMU observation')

flash=compile(ROOT/'tests/golden/avr-flash/prog.c','avr')
assert '.progmem.data' in run(['readelf','-SW',flash])
flash_image=OUT/'avr-flash.elf'
run([ROOT/'embld','-e','__vectors','-Ttext','0x0','-Tdata','0x100','--rom-limit','32768',*avr_objs[:3],flash,'-o',flash_image])
text=run([ROOT/'embsim',flash_image,'--board','uno','--until','==END==','--max-insns','2000000'])
assert 'flash ok\n29071 2147283647 126 232 \n16909060 84281096 -5 \none zero\n==END==' in text.replace('\r','')
check(3,'AVR __flash objects use .progmem.data and fixture reads values correctly','EmbSim AVR; RTOS wrappers unchanged')

debug=source('debug.c','volatile int observed;\nint increment(int x){int y=x+2;observed=y;return y;}\n')
debug_obj=compile(debug,'avr',['-g','-O0'])
info=run(['readelf','--debug-dump=info',debug_obj])
assert 'DW_TAG_subprogram' in info and 'increment' in info and 'observed' in info
assert 'Pointer Size:  4' in info
assert 'increment' in run([ROOT/'embdbg',debug_obj,'funcs'])
assert 'debug.c' in run([ROOT/'embdbg',debug_obj,'lines'])
check(4,'AVR DWARF carries function/global/line info readable by EmbDBG','object inspection; live debugging unrun')

layout=source('rsp-layout.c','#include <string.h>\n#include "tools/embdbg/remote.h"\nint main(void){const struct rsp_regdef *r=rsp_regs_for("avr");if(!r)return 1;for(int i=0;i<32;i++)if(r[i].off!=i||r[i].size!=1)return 2;return strcmp(r[32].name,"sreg")||r[32].off!=32||r[32].size!=1||strcmp(r[33].name,"sp")||r[33].off!=33||r[33].size!=2||strcmp(r[34].name,"pc")||r[34].off!=35||r[34].size!=4||r[35].name!=0;}\n')
run(['gcc','-std=c99','-D_POSIX_C_SOURCE=200809L','-I',ROOT,layout,ROOT/'tools/embdbg/remote.c','-o',OUT/'rsp-layout'])
run([OUT/'rsp-layout'])
check(5,'EmbDBG AVR register table has 32 byte regs, SREG, SP and byte-address PC','executed host layout check; remote session unrun')

atomic=source('small-atomic.c','void puts_(const char *);\nstatic unsigned char a=1;static unsigned short b=2;\nint main(void){unsigned char x=__atomic_fetch_add(&a,3,__ATOMIC_SEQ_CST);unsigned short y=__atomic_fetch_add(&b,4,__ATOMIC_SEQ_CST);puts_(x==1&&a==4&&y==2&&b==6?"ATOMIC OK\\n==END==\\n":"ATOMIC BAD\\n==END==\\n");return 0;}\n')
for target,board in [('riscv32-unknown-elf','virt'),('avr','uno')]:
    obj=compile(atomic,target)
    image=OUT/f'small-atomic-{board}.elf'
    if board=='uno':
        run([ROOT/'embld','-e','__vectors','-Ttext','0x0','-Tdata','0x100',*avr_objs[:3],obj,'-o',image])
    else:
        boot=compile(ROOT/'tests/harness/riscv/boot.c',target)
        io=compile(ROOT/'tests/harness/riscv/io.c',target)
        run([ROOT/'embld','-e','_start','-Ttext','0x80000000','-Tstack','0x80800000',boot,io,obj,'-o',image])
    assert 'ATOMIC OK\n==END==' in run([ROOT/'embsim',image,'--board',board,'--until','==END==','--max-insns','2000000'])
check(6,'1-/2-byte RMW executes expected values on RV32 and AVR','EmbSim; no interrupt-race/SMP/DMA proof')

cmse=source('cmse.c','int __attribute__((cmse_nonsecure_entry)) secure_add(int a,int b){return a+b;}\n')
for target in ['thumbv8m.main-none-eabi','thumbv8m.base-none-eabi']:
    obj=compile(cmse,target,['-mcmse'])
    assert '__acle_se_secure_add' in run(['readelf','-sW',obj])
refusal=run([ROOT/'embcc','--target=thumbv8m.main-none-eabihf','-mcmse','-c',cmse,'-o',OUT/'bad.o'],expect=1)
assert '-mcmse with an FPU' in refusal
check(7,'CMSE entry symbols emitted for both profiles; FPU combination refused','compile/refusal; security crossings unrun locally')

m23=compile(splim,'thumbv8m.base-none-eabi',['-mcpu=cortex-m23'])
assert 'v8-M.baseline' in run(['readelf','-A',m23])
check(8,'Cortex-M23 selection emits ARMv8-M Baseline attributes','object inspection; M23 hardware unrun')

header=OUT/'features.h'; data=OUT/'features.json'
run([ROOT/'embsvd',ROOT/'tests/golden/svd-clusters/features.svd','--no-cmsis','--header',header,'--json',data])
run(['python3',ROOT/'tests/golden/svd-clusters/jsoncheck.py',data,'features.h',OUT/'regs.txt',OUT/'asserts.c'])
reference=run(['python3',ROOT/'tests/golden/svd-clusters/svdref.py',ROOT/'tests/golden/svd-clusters/features.svd'])
assert reference.strip()==(OUT/'regs.txt').read_text().strip()
compile(OUT/'asserts.c','thumbv7em-none-eabi',['-I',OUT])
compile(ROOT/'tests/golden/svd-clusters/layout.c','thumbv7em-none-eabi',['-I',OUT])
check(9,'Nested SVD clusters/arrays: JSON addresses match independent reference and compiled header offsets','tool/layout check; RTOS YAML importer unimplemented')

cxx=source('wrapper.cc','template<class T>struct Box{T value;constexpr explicit Box(T x):value(x){} constexpr T get()const{return value;}};\nextern "C" unsigned query(unsigned x){Box<unsigned>b(x);return b.get()+1;}\n')
for target in ['thumbv7m-none-eabi','riscv32-unknown-elf']:
    compile(cxx,target,['-std=c++17','-fno-exceptions','-fno-rtti'])
refusal=run([ROOT/'embcc','--target=avr','-fno-exceptions','-c',cxx,'-o',OUT/'bad.o'],expect=1)
assert 'C++ is not yet supported for avr' in refusal
check(10,'C++17 template wrapper compiles for Cortex-M/RV32; AVR refused','compile/refusal; complete RTOS wrapper/runtime suite unrun')

record['complete']=True
record['binary_sha256']={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in ['embcc','embld','embdbg','embsvd','embsim']}
record['source_sha256']={str(p.relative_to(OUT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in OUT.glob('*') if p.suffix in ('.c','.cc')}
(OUT/'manifest.json').write_text(json.dumps(record,indent=2,sort_keys=True)+'\n')
