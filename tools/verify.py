"""Offline integrity checks. Does not establish electrical correctness or absence of all secrets."""
import csv, hashlib, json, pathlib, re, struct, sys, xml.etree.ElementTree as ET
ROOT=pathlib.Path(__file__).resolve().parents[1]

def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()

def verify():
    manifest=json.loads((ROOT/'vendor/manifest.json').read_text())
    for item in manifest:
        assert sha(ROOT/item['path'])==item['sha256'], 'vendor changed: '+item['path']
    pins=list(csv.DictReader((ROOT/'hardware/pinout.csv').open(encoding='utf-8',newline='')))
    assert len({row['pin'] for row in pins})==len(pins),'pin conflict'
    project=ROOT/'keil/PlantCare.uvprojx'; xml=ET.parse(project)
    expected={p.resolve() for p in (ROOT/'firmware/src').glob('*.c')}
    actual={(project.parent/element.text.replace('\\','/')).resolve() for element in xml.findall('.//FilePath')}
    assert actual==expected,'Keil project source list mismatch'
    scatter=xml.find('.//LDads/ScatterFile').text.replace('\\','/')
    assert (project.parent/scatter).resolve()==(ROOT/'firmware/plant.sct').resolve()
    release=ROOT/'release'
    build=json.loads((release/'manifest.json').read_text())
    for relative,digest in build['sha256'].items():
        assert sha(ROOT/relative)==digest,'source/release mismatch: '+relative
    binary=(release/'plant-care.bin').read_bytes()
    assert 256<=len(binary)<=0xf800
    sp,reset=struct.unpack_from('<II',binary)
    assert sp==0x20005000 and reset&1 and 0x08000000<=reset<0x08000000+len(binary),'bad vectors'
    high=0; memory={}; eof=False
    for line in (release/'plant-care.hex').read_text().splitlines():
        assert line.startswith(':')
        record=bytes.fromhex(line[1:]); assert len(record)==record[0]+5 and sum(record)%256==0,'bad HEX checksum'
        length,kind=record[0],record[3]; address=int.from_bytes(record[1:3],'big')
        data=record[4:-1]
        if kind==4: assert length==2; high=int.from_bytes(data,'big')<<16
        elif kind==0:
            for offset,byte in enumerate(data):
                location=high+address+offset
                assert 0x08000000<=location<0x0800f800,'HEX overlaps config or wrong target'
                assert location not in memory,'overlapping HEX record'; memory[location]=byte
        elif kind==1: eof=True
        elif kind!=5: raise AssertionError('unexpected HEX record type')
    assert eof and memory
    assert bytes(memory.get(0x08000000+i,0) for i in range(len(binary)))==binary,'HEX/BIN mismatch'
    # A targeted hygiene scan, not a guarantee that every possible secret is detectable.
    patterns=[r'gh[pousr]_[A-Za-z0-9]{20,}',r'github_pat_[A-Za-z0-9_]{30,}',r'sk-[A-Za-z0-9]{20,}',
              r'AKIA[0-9A-Z]{16}',r'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----']
    scanned=0
    for path in ROOT.rglob('*'):
        if not path.is_file() or any(part in {'.git','build','__pycache__'} for part in path.relative_to(ROOT).parts): continue
        assert path.suffix.lower() not in {'.docx','.pdf','.ppt','.pptx','.xlsx','.pem','.key','.p12'},'unexpected private document: '+path.name
        if path.name.startswith('.env'): raise AssertionError('unexpected environment file')
        if path.suffix in {'.bin','.exe','.png','.zip'}: continue
        text=path.read_text(encoding='utf-8-sig',errors='replace')
        for pattern in patterns: assert not re.search(pattern,text), 'possible secret: '+str(path.relative_to(ROOT))
        scanned+=1
    print(f'PASS: {len(manifest)} pinned vendor files; unique pin assignments; Keil source list; vectors; HEX/BIN; {scanned} text files scanned.')
    print('This verifies software artifacts only. Hardware acceptance remains pending.')

if __name__=='__main__':
    try: verify()
    except (AssertionError,OSError,ValueError,KeyError) as exc:
        print('FAIL:',exc,file=sys.stderr); raise SystemExit(1)
