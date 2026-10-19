"""Package previously built and tested artifacts; refuses stale source/image hashes."""
import datetime,hashlib,json,pathlib,shutil
ROOT=pathlib.Path(__file__).resolve().parents[1]

def digest(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def check_sources(report):
    for path,sha in report['source_sha256'].items():
        if digest(ROOT/path)!=sha: raise SystemExit('Stale build/test: '+path+'. Rebuild and rerun tests first.')

def main():
    build=ROOT/'build/keil'; test=ROOT/'build/tests'; release=ROOT/'release'
    compilation=json.loads((build/'build-report.json').read_text())
    tests=json.loads((test/'test-report.json').read_text())
    if not tests['passed']: raise SystemExit('Tests have not passed')
    check_sources(compilation); check_sources(tests)
    for name,sha in compilation['image_sha256'].items():
        if digest(build/name)!=sha: raise SystemExit('Build image changed: '+name)
    release.mkdir(exist_ok=True)
    for name in ['plant-care.bin','plant-care.hex','build-report.json']: shutil.copyfile(build/name,release/name)
    shutil.copyfile(test/'test-report.json',release/'test-report.json')
    shutil.copyfile(test/'results.txt',release/'test-results.txt')
    paths=list((ROOT/'firmware').rglob('*'))+list((ROOT/'vendor').rglob('*'))+list((ROOT/'keil').glob('*.uvprojx'))
    paths += list((ROOT/'tools').glob('*.py'))+list((ROOT/'desktop').glob('*.py'))+list((ROOT/'tests').glob('*.c'))+list((ROOT/'tests').glob('*.py'))
    paths += [release/name for name in ['plant-care.bin','plant-care.hex','build-report.json','test-report.json','test-results.txt']]
    manifest={'version':'1.0.0','packaged_at_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
              'target':'STM32F103C8T6','hardware_tested':False,
              'sha256':{p.relative_to(ROOT).as_posix():digest(p) for p in sorted(set(paths)) if p.is_file()}}
    (release/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    (release/'SHA256SUMS.txt').write_text(''.join(manifest['sha256']['release/'+name]+'  '+name+'\n' for name in ['plant-care.bin','plant-care.hex']),encoding='utf-8')
    print('Release packaged from matching built and tested sources. Run tools/verify.py next.')

if __name__=='__main__': main()
