from pathlib import Path
import subprocess,shlex
root=Path(__file__).resolve().parents[1];repo=root.parents[1];build=repo/'build/dev-macos-debug'
for name,target in [('color','color_pipeline_test'),('scene','gaussian_scene_render_test')]:
 line=subprocess.check_output(['ninja','-C',str(build),'-t','commands',target],text=True).splitlines()[-1]
 args=[x for x in shlex.split(line) if x not in [':','&&']]
 ix=next(i for i,x in enumerate(args) if x.endswith(target+'.cpp.o'));args[ix]=str(root/f'tools/studio-{name}.cpp');args[args.index('-o')+1]=str(root/f'out/studio/native-{name}')
 args[1:1]=['-std=c++20','-O2','-I'+str(repo/'engine/include'),'-I'+str(repo/'apps/desktop/src'),'-I/opt/homebrew/include','-F/opt/homebrew/lib','-I/opt/homebrew/lib/QtCore.framework/Headers','-I/opt/homebrew/lib/QtGui.framework/Headers']
 if name=='color':args+=['-framework','QtGui','-framework','QtCore','-L/opt/homebrew/opt/libraw/lib','-lraw_r','-Wl,-rpath,/opt/homebrew/lib']
 result=subprocess.run(args,cwd=build,capture_output=True,text=True);print(name,result.returncode,result.stderr[-5000:]);result.check_returncode()
