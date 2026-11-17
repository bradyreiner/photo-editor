"""Portable video VUI, still profiles and font-exact vector companions."""
from pathlib import Path
import json,subprocess,shutil,tempfile,sys
from PIL import Image,ImageCms
ROOT=Path(__file__).resolve().parents[1]
report={'canonical_builder_letter':[],'stills_profiled':[],'outlined_svgs':[]}
for src,name in ([]if'--vectors-only'in sys.argv else[('01-builder-letter.mp4','hero'),('01-hook-bring-a-real-project.mp4','hook-b'),('01-hook-what-should-this-handle.mp4','hook-c')]):
    source=ROOT/'campaign01/video'/src;dest=ROOT/f'campaign01/videos/{name}.mp4'
    subprocess.run(['ffmpeg','-v','error','-y','-i',str(source),'-map','0:v:0','-map','0:a:0','-c:v','copy','-bsf:v','h264_metadata=colour_primaries=1:transfer_characteristics=1:matrix_coefficients=1:video_full_range_flag=0','-af','loudnorm=I=-18:TP=-1.5:LRA=9','-c:a','aac','-b:a','192k','-ar','48000','-t','32','-movflags','+faststart',str(dest)],check=True)
    silent=dest.with_stem(dest.stem+'-silent');subprocess.run(['ffmpeg','-v','error','-y','-i',str(dest),'-c:v','copy','-an','-movflags','+faststart',str(silent)],check=True)
    report['canonical_builder_letter'].append(str(dest.relative_to(ROOT)))
icc=ImageCms.ImageCmsProfile(ImageCms.createProfile('sRGB')).tobytes()
for id in['01','02','03','04','05']:
    for folder in['images','storyboard','carousel','carousels']:
        for path in (ROOT/f'campaign{id}'/folder).glob('*.png'):
            with Image.open(path)as im:
                if not im.info.get('icc_profile'):
                    pixels=im.copy();pixels.save(path,icc_profile=icc);report['stills_profiled'].append(str(path.relative_to(ROOT)))
source=(ROOT/'campaign04/source/outline_svg.m').read_text()
needle='NSDictionary *files=@{'
aliases='@"sans":@"spacegrotesk/SpaceGrotesk[wght].ttf",@"serif":@"instrumentserif/InstrumentSerif.ttf",@"italic":@"instrumentserif/InstrumentSerif-Italic.ttf",@"mono":@"spacemono/SpaceMono-Regular.ttf",@"heavy":@"archivoblack/ArchivoBlack-Regular.ttf",'
source=source.replace(needle,needle+aliases)
source=source.replace('NSDictionary *attrs=@{(__bridge NSString*)kCTFontVariationAttribute:@{@2003265652:@600}};', 'NSNumber *weight=@([attr(node,@"font-weight") ?: @"400" doubleValue]);\n      NSDictionary *attrs=@{(__bridge NSString*)kCTFontVariationAttribute:@{@2003265652:weight}};')
source=source.replace('CTLineRef line=CTLineCreateWithAttributedString((__bridge CFAttributedStringRef)string);','CTLineRef line=CTLineCreateWithAttributedString((__bridge CFAttributedStringRef)string);\n      if([attr(node,@"dominant-baseline") isEqualToString:@"text-before-edge"]) { CGRect ink=CTLineGetBoundsWithOptions(line,kCTLineBoundsUseGlyphPathBounds); y+=CGRectGetMaxY(ink); }')
helper=ROOT/'tools/outline_svg.m'
if not helper.exists():helper.write_text(source)
binary=Path(tempfile.gettempdir())/'looksi-v2-outline'
subprocess.run(['clang','-fobjc-arc','-fblocks','-framework','Foundation','-framework','CoreText','-framework','CoreGraphics',str(helper),'-o',str(binary)],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
for id in['01','02','03','05']:
    for folder in['images','storyboard','carousel','carousels']:
        for path in sorted((ROOT/f'campaign{id}'/folder).glob('*.svg')):
            if '-editable-text' in path.stem:continue
            live=path.with_stem(path.stem+'-editable-text')
            # Idempotent: only copy a true live-text master, never replace it with outlines.
            if '<text ' in path.read_text():shutil.copy2(path,live)
            if live.exists():
                subprocess.run([str(binary),str(live),str(path),str(ROOT/'fonts')],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
                report['outlined_svgs'].append(str(path.relative_to(ROOT)))
(ROOT/'qa/finish-delivery.json').write_text(json.dumps(report,indent=2))
print(json.dumps({k:len(v)for k,v in report.items()}))
