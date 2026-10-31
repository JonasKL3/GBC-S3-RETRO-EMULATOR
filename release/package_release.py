from pathlib import Path
import zipfile,subprocess,hashlib,json,re,os,sys
root=Path(__file__).resolve().parents[1]
system=root.name.split('-')[0]
# In a GitHub checkout, the folder name can differ from the repository name.
if not (root/'firmware'/(system+'_S3')).exists():
    candidates=[p.name[:-3] for p in (root/'firmware').iterdir() if p.is_dir() and p.name.endswith('_S3')]
    if len(candidates)!=1:raise SystemExit('Expected exactly one emulator sketch')
    system=candidates[0]
version=(root/'VERSION.txt').read_text().strip().removesuffix('-dev')
if not re.fullmatch(r'\d+\.\d+\.\d+',version):raise SystemExit('Invalid version')
manifest=json.loads((root/'SHA256SUMS.json').read_text())
for relative,expected in manifest.items():
    path=root/relative
    if hashlib.sha256(path.read_bytes()).hexdigest()!=expected:raise SystemExit('Hash mismatch: '+relative)
files=subprocess.check_output(['git','ls-files','-z'],cwd=root).decode().split('\0')
files=[name for name in files if name]
out=root/'dist';out.mkdir(exist_ok=True)
name=f'{system}-S3-DEV-{version}-SEM-TELEMETRIA.zip';target=out/name
with zipfile.ZipFile(target,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as archive:
    for relative in sorted(files):
        path=root/relative
        # Fixed ZIP timestamps make the same source snapshot reproducible.
        entry=zipfile.ZipInfo(relative,date_time=(2026,10,31,0,0,0));entry.compress_type=zipfile.ZIP_DEFLATED;entry.external_attr=0o100644<<16
        archive.writestr(entry,path.read_bytes())
with zipfile.ZipFile(target) as archive:
    if archive.testzip() is not None:raise SystemExit('Invalid ZIP')
digest=hashlib.sha256(target.read_bytes()).hexdigest()
(out/'SHA256SUMS.txt').write_text(digest+'  '+name+'\n',encoding='ascii')
print(f'Package verified: {name}, {len(files)} files, SHA256 {digest}')
if '--publish' in sys.argv:
    tag='v'+version
    existing=subprocess.run(['gh','release','view',tag,'--json','tagName'],cwd=root,text=True,capture_output=True)
    if existing.returncode==0:
        # Promote the tested version without replacing its existing ZIP or checksum.
        subprocess.run(['gh','release','edit',tag,'--prerelease=false','--latest','--title',f'{system}-S3 {version} — sem telemetria','--notes-file',str(root/'release/RELEASE_NOTES.md')],cwd=root,check=True)
        print('Promoted stable release; original assets preserved: '+tag)
    else:
        error=existing.stderr.lower()
        if 'release not found' not in error and '404' not in error:raise SystemExit(existing.stderr)
        sha=os.environ.get('GITHUB_SHA') or subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip()
        subprocess.run(['gh','release','create',tag,str(target),str(out/'SHA256SUMS.txt'),'--target',sha,'--title',f'{system}-S3 {version} — sem telemetria','--notes-file',str(root/'release/RELEASE_NOTES.md'),'--latest'],cwd=root,check=True)
        print('Published '+tag)
