import os,pathlib,subprocess,sys,argparse,tempfile
if os.name=='nt':
    import ctypes
    ctypes.windll.kernel32.SetErrorMode(0x8003)
parser=argparse.ArgumentParser(description='Native core, state, RTC, FIFO and video regressions; no device access.')
parser.add_argument('--zig',required=True,help='Path to the official Zig 0.14.1 executable')
parser.add_argument('--build',help='Optional directory for generated test files')
args=parser.parse_args()
root=pathlib.Path(__file__).resolve().parents[1]
work=pathlib.Path(args.build).resolve() if args.build else root/'.audit-build'
core=root/'firmware/GBC_S3/src/gnuboy';build=work/'native-gbc038'
build.mkdir(parents=True,exist_ok=True)
zig=pathlib.Path(args.zig).resolve()
env=os.environ.copy();env['ZIG_GLOBAL_CACHE_DIR']=str(work/'zig-cache-global');env['ZIG_LOCAL_CACHE_DIR']=str(work/'zig-cache-local')
def run(args):
    p=subprocess.run([str(x) for x in args],env=env,capture_output=True,text=True,timeout=120)
    if p.stdout:print(p.stdout,flush=True)
    if p.returncode:
        print('FAILED '+str(args[0])+' exit='+str(p.returncode),flush=True)
        print(p.stderr[:16000],flush=True);raise SystemExit(p.returncode)
flags=['-O1','-g','-D_GNU_SOURCE','-DARDUINO_ARCH_ESP32','-fsanitize=undefined','-fno-sanitize-trap=undefined','-fno-sanitize-recover=undefined',
       '-I'+str(root/'tests/host_stubs'),'-I'+str(root),'-I'+str(core),'-L'+str(build)]
report=build/'ubsan_report.o'
run([zig,'cc','-c',root/'tests/ubsan_report.c','-o',report])
for lib in ['liblibclang_rt.ubsan_standalone.a','liblibclang_rt.ubsan_standalone_cxx.a']:
    run([zig,'ar','rcs',build/lib,report])
sources=['cpu.c','mem.c','lcd.c','lcdc.c','hw.c','rtc.c','sound.c','palette.c','fastmem.c','emu.c','loader.c','save.c','inflate.c','miniz_tinfl.c',
         'xz/xz_crc32.c','xz/xz_crc64.c','xz/xz_dec_lzma2.c','xz/xz_dec_stream.c','xz/xz_dec_bcj.c','gnuboy_platform.c']
objs=[]
for name in sources:
    obj=build/(name.replace('/','_')+'.o');run([zig,'cc','-std=gnu11',*flags,'-c',core/name,'-o',obj]);objs.append(obj)
    print('compiled '+name,flush=True)
adapter=build/'adapter.o';run([zig,'c++','-std=gnu++11',*flags,'-c',root/'firmware/GBC_S3/gnuboy_adapter.cpp','-o',adapter]);objs.append(adapter)
for test in ['test_video_regression','test_frame_mailbox','test_core_audit','test_frame_render_skip']:
    exe=build/(test+'.exe')
    usesCore=test in ['test_core_audit','test_frame_render_skip']
    run([zig,'c++','-std=gnu++11',*flags,root/('tests/'+test+'.cpp'),*(objs if usesCore else []),'-o',exe])
    if not usesCore:run([exe]);print(test+' native PASS',flush=True)
    else:
        for kind,cgb,cart,ram in [('dmg',0,0,0),('cgb',128,0,0),('mbc2',128,6,0),('mbc3',128,16,2),('mbc5',128,27,5),('mbc30',128,16,5)]:
            data=bytearray(32768) if kind!='mbc30' else bytearray().join(bytes([i])*16384 for i in range(256))
            data[0x100:0x102]=b'\x18\xfe';data[0x134:0x13c]=b'GBCSMOKE'
            data[0x143]=cgb;data[0x147]=cart;data[0x149]=ram
            if kind=='mbc30':data[0x148]=7
            rom=build/(kind+'.gb');rom.write_bytes(data);run([exe,rom]);print(kind+' native PASS',flush=True)
sdroot=pathlib.Path(tempfile.mkdtemp(prefix='sd-',dir=build));(sdroot/'JOGOS/GBC/save').mkdir(parents=True,exist_ok=True)
extra=['-I'+str(root/'tests/state_stubs'),'-DGBC_HOST_SD_ROOT="'+sdroot.as_posix()+'"','-DGBC_VFS_MOUNT="'+sdroot.as_posix()+'"']
state=build/'frontend_states.o';run([zig,'c++','-std=gnu++11',*extra,*flags,'-c',root/'firmware/GBC_S3/gbc_savestates.cpp','-o',state])
exe=build/'test_frontend_audit.exe'
run([zig,'c++','-std=gnu++11',*extra,*flags,root/'tests/test_frontend_audit.cpp',*objs,state,'-o',exe])
run([exe,build/'cgb.gb'])
videoFlags=['-I'+str(root/'tests/video_stubs'),*flags]
videoObj=build/'async_video.o'
run([zig,'c++','-std=gnu++11',*videoFlags,'-c',root/'firmware/GBC_S3/gbc_video_output.cpp','-o',videoObj])
videoExe=build/'test_video_output_runtime.exe'
run([zig,'c++','-std=gnu++11',*videoFlags,root/'tests/test_video_output_runtime.cpp',videoObj,'-o',videoExe])
run([videoExe])


coverFlags=['-I'+str(root/'tests/cover_stubs'),*extra,*flags]
coverExe=build/'test_covers.exe'
run([zig,'c++','-std=gnu++11',*coverFlags,root/'tests/test_covers.cpp',root/'firmware/GBC_S3/gbc_s3_covers.cpp','-o',coverExe])
run([coverExe])

metadataFlags=[*extra,'-DHALO_LIB_ROOT="'+(sdroot/'JOGOS/GBC/save').as_posix()+'"',*flags]
if os.name=='nt':metadataFlags+=['-Dfsync=_commit','-include','io.h']
metadataExe=build/'test_metadata.exe'
run([zig,'c++','-std=gnu++11',*metadataFlags,root/'tests/test_metadata.cpp',root/'firmware/GBC_S3/halo_library.cpp','-o',metadataExe])
run([metadataExe])
print('NATIVE AUDIT PASS (Clang undefined-behavior instrumentation + Zig diagnostic runtime)',flush=True)
