"""Run the real portable C control core and Python companion tests."""
import argparse, datetime, hashlib, json, os, pathlib, shlex, subprocess, sys
ROOT=pathlib.Path(__file__).resolve().parents[1]

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--cc',default=os.environ.get('CC','cc'),help='C compiler, e.g. cc or "path/to/zig cc"')
    args=ap.parse_args()
    cc=shlex.split(args.cc, posix=os.name!='nt')
    cc=[x.strip('"') for x in cc]
    build=ROOT/'build'/'tests'; build.mkdir(parents=True,exist_ok=True)
    report=build/'test-report.json'; report.unlink(missing_ok=True)
    log=[]
    def run(command):
        result=subprocess.run(command,cwd=ROOT,capture_output=True,text=True)
        output=result.stdout+result.stderr; log.append(output); print(output,end='')
        result.check_returncode()
    exe=build/('test_core.exe' if os.name=='nt' else 'test_core')
    command=cc+['-std=c11','-Wall','-Wextra','-Werror','-O1','-UNDEBUG','-g','-I',str(ROOT/'firmware/include'),
        str(ROOT/'firmware/src/plant.c'),str(ROOT/'firmware/src/codecs.c'),str(ROOT/'tests/test_core.c'),'-o',str(exe)]
    run(command)
    run([str(exe)])
    simulator=build/('plant_sim.exe' if os.name=='nt' else 'plant_sim')
    run(cc+['-std=c11','-Wall','-Wextra','-Werror','-O1','-UNDEBUG','-I',str(ROOT/'firmware/include'),
        str(ROOT/'firmware/src/plant.c'),str(ROOT/'firmware/src/codecs.c'),str(ROOT/'tests/simulator.c'),'-o',str(simulator)])
    run([sys.executable,'-m','unittest','discover','-s','tests','-p','test_*.py','-v'])
    (build/'results.txt').write_text(''.join(log),encoding='utf-8')
    sources=list((ROOT/'tests').glob('*.c'))+list((ROOT/'tests').glob('*.py'))+list((ROOT/'desktop').glob('*.py'))
    sources += [ROOT/'firmware/src/plant.c',ROOT/'firmware/src/codecs.c',ROOT/'firmware/include/plant.h',ROOT/'tools/test.py']
    report.write_text(json.dumps({'passed':True,'date_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'native_compiler':pathlib.Path(cc[0]).name,'source_sha256':{p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}},indent=2)+'\n',encoding='utf-8')

if __name__=='__main__': main()
