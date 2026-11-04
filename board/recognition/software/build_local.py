"""Build the shipped accel.mk source set without requiring GNU make on Windows."""
from pathlib import Path
import concurrent.futures, gzip, hashlib, json, os, subprocess, sys

ROOT=Path(__file__).resolve().parent
TOOLS=Path(os.environ.get('RISCV_BIN','D:/Efinity/riskv/toolchain/bin'))
PREFIX=str(TOOLS/'riscv-none-elf-')
live192='--192' in sys.argv
if live192:raise SystemExit('official YOLO is a 96x96 model; --192 is not supported in this release')
out=ROOT/('build-live192' if live192 else 'build-accel')
out.mkdir(exist_ok=True)
stand='sdk/software/standalone'
flags=['-O3','-march=rv32im_zicsr_zifencei','-mabi=ilp32','-DUSE_GP',
 '-DSYSTEM_UART_A_APB=SYSTEM_UART_0_IO_APB','-DSYSTEM_GPIO_A_APB=SYSTEM_GPIO_0_IO_APB',
 '-DSYSTEM_I2C_A_APB=SYSTEM_I2C_0_IO_APB','-DPERSON_FPGA_ACCEL','-DPERSON_LIVE_CAMERA',
 '-DOPTIMIZE_X','-funroll-loops','-mdiv','-ffunction-sections','-fdata-sections',
 '-fno-common','-fomit-frame-pointer','-g','-fno-builtin','-fstack-usage',
 '-DTF_LITE_STATIC_MEMORY','-DTF_LITE_USE_GLOBAL_CMATH_FUNCTIONS','-DTF_LITE_USE_GLOBAL_MIN',
 '-DTF_LITE_USE_GLOBAL_MAX','-DTF_LITE_DISABLE_X86_NEON']
includes=['.','src','src/platform','src/platform/interrupt','src/platform/misc','src/platform/tinyml',
 'src/net','sdk/bsp/efinix/EfxSapphireSoc/app/lwip/include',
 'src/tensorflow/third_party/flatbuffers/include','src/tensorflow/third_party/gemmlowp',
 'src/tensorflow/third_party/ruy','src/sensors/vendor',stand+'/driver',stand+'/include',
 'sdk/bsp/efinix/EfxSapphireSoc/include','sdk/bsp/efinix/EfxSapphireSoc/app']
flags+=['-I'+p for p in includes]
if live192:flags+=['-DPERSON_LIVE_192']
cpp=['-std=c++11','-fstrict-aliasing','-fno-rtti','-fno-exceptions',
 '-fno-threadsafe-statics','-fno-use-cxa-atexit','-fmessage-length=0']
patterns=['src/*.c','src/*.cc','src/*.cpp','src/sensors/*.c','src/sensors/vendor/*.c','src/model/*.cc',
 'src/tensorflow/lite/*.c','src/tensorflow/lite/c/*.c','src/tensorflow/lite/core/api/*.cc',
 'src/tensorflow/lite/kernels/*.cc','src/tensorflow/lite/kernels/internal/*.cc',
 'src/tensorflow/lite/kernels/internal/optimized/*.cc','src/tensorflow/lite/kernels/internal/reference/*.cc',
 'src/tensorflow/lite/kernels/internal/reference/integer_ops/*.cc','src/tensorflow/lite/micro/*.cc',
 'src/tensorflow/lite/micro/kernels/*.cc','src/tensorflow/lite/micro/memory_planner/*.cc',
 'src/tensorflow/lite/schema/*.cc']
sources=sorted({p.relative_to(ROOT).as_posix() for pat in patterns for p in ROOT.glob(pat)})
sources=[s for s in sources if s not in ('src/cpu_ops.cc','src/main.cc' if live192 else 'src/main192.cc')]
sources+=['src/platform/interrupt/intc.c',stand+'/common/start.S',stand+'/common/trap.S']
lwip=ROOT/'sdk/bsp/efinix/EfxSapphireSoc/app/lwip'
sources += [p.relative_to(ROOT).as_posix() for pattern in ('core/*.c','core/ipv4/*.c','netif/ethernet.c') for p in lwip.glob(pattern)]
if live192:sources+=['src/live192_assets.S']
def run(args):
 p=subprocess.run([str(a) for a in args],cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
 if p.returncode:raise RuntimeError(p.stdout)
 return p.stdout
def compile_source(source):
 obj=out/(source+'.o');obj.parent.mkdir(parents=True,exist_ok=True)
 run([PREFIX+('g++.exe' if source.endswith(('.cc','.cpp')) else 'gcc.exe'),*flags,
      *(cpp if source.endswith(('.cc','.cpp')) else []),'-c',source,'-o',obj])
 return obj
if __name__=='__main__':
 page=(ROOT/'src/net/board.html').read_text(encoding='utf-8')
 (ROOT/'src/net/board_page.h').write_text('static const char board_page[] = R"V6PAGE('+page+')V6PAGE";\n',encoding='utf-8')
 assets=[]; definitions=[]
 for name in ('style.css','app.js','care.html','tracking.html','privacy.js','media/observer.png','media/recorder.png','media/companions.png','media/demo-frame.jpg'):
  data=(ROOT/'src/net'/name).read_bytes(); compressed=name.endswith(('.css','.js'))
  if compressed:data=gzip.compress(data,mtime=0)
  symbol='asset_'+str(len(assets)); mime='text/html; charset=utf-8' if name.endswith('.html') else 'text/css; charset=utf-8' if name.endswith('.css') else 'application/javascript; charset=utf-8' if name.endswith('.js') else 'image/png' if name.endswith('.png') else 'image/jpeg'
  definitions.append('static const unsigned char '+symbol+'[]={'+','.join(map(str,data))+'};')
  assets.append('{"/'+name+'","'+mime+'",'+symbol+',sizeof('+symbol+'),"'+('Content-Encoding: gzip\\r\\n' if compressed else '')+'"}')
 (ROOT/'src/net/board_assets.h').write_text('#include <string.h>\nstruct BoardAsset {const char* path;const char* type;const unsigned char* bytes;unsigned size;const char* extra;};\n'+'\n'.join(definitions)+'\nstatic const BoardAsset board_assets[]={'+','.join(assets)+'};\nstatic const BoardAsset* board_asset(const char* path){for(const auto& asset:board_assets)if(!strcmp(path,asset.path))return &asset;return nullptr;}\n',encoding='utf-8')
 with concurrent.futures.ThreadPoolExecutor(max_workers=min(8,os.cpu_count() or 2)) as pool:
  objects=list(pool.map(compile_source,sources))
 run([PREFIX+'ar.exe','--output='+str(out),'x','vendor_tinyml_lib.a','yolo.o'])
 yolo=out/'yolo_fast.o'
 run([PREFIX+'objcopy.exe','--weaken-symbol=_Z11perform_nmsP3boxiif',
      '--add-symbol=vendor_perform_nms=.text._Z11perform_nmsP3boxiif:0,global,function',out/'yolo.o',yolo])
 elf=out/'personInferenceDemo.elf'
 print(run([PREFIX+'g++.exe',*flags,*cpp,*objects,yolo,stand+'/common/tinyml_lib.a','-o',elf,
  '-specs=nosys.specs','-nostartfiles','-Wl,-T,ddr.ld,-Map,'+str(out/'personInferenceDemo.map')+
  ',--gc-sections,--print-memory-usage','-lm']))
 run([PREFIX+'objcopy.exe','-O','binary',elf,out/'personInferenceDemo.bin'])
 inputs={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in ROOT.rglob('*')
  if p.is_file() and (p.is_relative_to(ROOT/'src') or p.is_relative_to(lwip) or p.name in ('ddr.ld','build_local.py','tinyml_lib.a','vendor_tinyml_lib.a'))}
 (out/'build_receipt.json').write_text(json.dumps({'compiler':run([PREFIX+'gcc.exe','--version']).splitlines()[0],'inputs':inputs,'elf_sha256':hashlib.sha256(elf.read_bytes()).hexdigest()},indent=2))
 print('SOFTWARE_BUILD_PASS',elf)
