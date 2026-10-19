"""Build with a locally installed, licensed Arm Compiler 6; never downloads it."""
import argparse, hashlib, json, os, pathlib, subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1]

def run(args):
    print(' '.join(str(x) for x in args), flush=True)
    subprocess.run([str(x) for x in args], cwd=ROOT, check=True)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--bin', help='Arm Compiler 6 bin directory, containing armclang.exe')
    args = ap.parse_args()
    candidates = [args.bin, os.environ.get('ARMCLANG_BIN'), r'C:\Keil_v5\ARM\ARMCLANG\bin']
    binary = next((pathlib.Path(x) for x in candidates if x and (pathlib.Path(x)/'armclang.exe').exists()), None)
    if not binary:
        ap.error('Set ARMCLANG_BIN or --bin to your Arm Compiler 6 bin folder.')
    build=ROOT/'build'/'keil'; build.mkdir(parents=True, exist_ok=True)
    includes=['firmware/include','vendor/cmsis-device-f1/Include','vendor/CMSIS/Include']
    objects=[]
    for source in sorted((ROOT/'firmware'/'src').glob('*.c')):
        obj=build/(source.stem+'.o'); objects.append(obj)
        cmd=[binary/'armclang.exe','--target=arm-arm-none-eabi','-mcpu=cortex-m3','-mthumb','-std=c11',
             '-Oz','-g','-Wall','-Wextra','-Werror','-ffunction-sections','-fdata-sections',
             '-DSTM32F103xB','-D__MICROLIB']
        for inc in includes: cmd += ['-I',ROOT/inc]
        run(cmd+['-c',source,'-o',obj])
    elf=build/'plant-care.axf'
    run([binary/'armlink.exe','--cpu','Cortex-M3','--library_type=microlib','--strict','--scatter',ROOT/'firmware/plant.sct',
         '--map','--symbols','--info=sizes,totals,unused','--list',build/'plant-care.map','--output',elf,*objects])
    run([binary/'fromelf.exe','--i32combined','--output',build/'plant-care.hex',elf])
    run([binary/'fromelf.exe','--bin','--output',build/'plant-care.bin',elf])
    run([binary/'fromelf.exe','--text','-z','--output',build/'size.txt',elf])
    size=(build/'plant-care.bin').stat().st_size
    if size>0xf800: raise RuntimeError('Firmware overlaps reserved settings pages')
    version=subprocess.run([str(binary/'armclang.exe'),'--version'],capture_output=True,text=True,check=True).stdout.strip()
    source_paths=list((ROOT/'firmware').rglob('*'))+list((ROOT/'vendor').rglob('*'))+[ROOT/'tools/build_keil.py']
    source_sha={p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(source_paths) if p.is_file()}
    report={'tool':'Arm Compiler 6','tool_version':version,'target':'STM32F103C8T6','code_bytes':size,'code_limit':0xf800,
            'hardware_tested':False,'source_sha256':source_sha,
            'image_sha256':{name:hashlib.sha256((build/name).read_bytes()).hexdigest() for name in ['plant-care.bin','plant-care.hex']}}
    (build/'build-report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(f'BUILT: {size} bytes. Hardware validation is still required.')

if __name__=='__main__': main()
