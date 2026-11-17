from pathlib import Path
import json,sys
from native_mcp import call
from discover import unpack
out=Path(__file__).parent/'evidence';name=sys.argv[1];args=json.loads(sys.argv[2]) if len(sys.argv)>2 else {};r=call(name,args);label=sys.argv[3] if len(sys.argv)>3 else name.replace('.','-');(out/(label+'.json')).write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(unpack(r),indent=2))
