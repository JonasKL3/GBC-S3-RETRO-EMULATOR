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
name=f'{system}-S3.zip';target=out/name
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
        # Rename the existing ZIP without recompressing or replacing its contents.
        repo=os.environ['GH_REPO']
        release=json.loads(subprocess.check_output(['gh','api',f'repos/{repo}/releases/tags/{tag}'],cwd=root,text=True))
        legacy=f'{system}-S3-DEV-{version}-SEM-TELEMETRIA.zip'
        matches=[asset for asset in release['assets'] if asset['name'] in (name,legacy)]
        if len(matches)!=1:raise SystemExit('Expected one existing release ZIP; refusing a name collision')
        asset=matches[0]
        downloaded=out/'published';downloaded.mkdir(exist_ok=True)
        subprocess.run(['gh','release','download',tag,'--pattern',asset['name'],'--dir',str(downloaded),'--clobber'],cwd=root,check=True)
        published=downloaded/asset['name']
        published_digest=hashlib.sha256(published.read_bytes()).hexdigest()
        if asset.get('digest') and asset['digest']!='sha256:'+published_digest:raise SystemExit('Published ZIP hash mismatch')
        with zipfile.ZipFile(published) as archive:
            if archive.testzip() is not None:raise SystemExit('Invalid published ZIP')
        if asset['name']!=name:
            subprocess.run(['gh','api','--method','PATCH',f"repos/{repo}/releases/assets/{asset['id']}",'-f','name='+name],cwd=root,check=True,stdout=subprocess.DEVNULL)
        checksum=downloaded/'SHA256SUMS.txt'
        checksum.write_text(published_digest+'  '+name+'\n',encoding='ascii')
        subprocess.run(['gh','release','upload',tag,str(checksum),'--clobber'],cwd=root,check=True)
        subprocess.run(['gh','release','edit',tag,'--prerelease=false','--latest','--title',f'{system}-S3 {version} — sem telemetria','--notes-file',str(root/'release/RELEASE_NOTES.md')],cwd=root,check=True)
        print(f'Stable release {tag}: {name}, original ZIP SHA256 {published_digest}')
    else:
        error=existing.stderr.lower()
        if 'release not found' not in error and '404' not in error:raise SystemExit(existing.stderr)
        sha=os.environ.get('GITHUB_SHA') or subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip()
        subprocess.run(['gh','release','create',tag,str(target),str(out/'SHA256SUMS.txt'),'--target',sha,'--title',f'{system}-S3 {version} — sem telemetria','--notes-file',str(root/'release/RELEASE_NOTES.md'),'--latest'],cwd=root,check=True)
        print('Published '+tag)
