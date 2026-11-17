"""Use the app's documented MCP adapter. Never log endpoint credentials."""
from pathlib import Path
import importlib.util
import json
import sys

ROOT=Path(__file__).resolve().parents[1]
REPO=ROOT.parents[3]
spec=importlib.util.spec_from_file_location('looksy_adapter',REPO/'scripts/looksy-mcp.py')
adapter=importlib.util.module_from_spec(spec);spec.loader.exec_module(adapter)
connection=adapter.DesktopConnection(adapter.default_session_file(),timeout=45)
def rpc(method,params):
    response=connection.forward({'jsonrpc':'2.0','id':1,'method':method,'params':params})
    if response and response.get('error'):raise RuntimeError(response['error'])
    return response
def initialize():
    response=rpc('initialize',{'protocolVersion':'2025-06-18','capabilities':{},'clientInfo':{'name':'Looksi campaign production','version':'2'}})
    connection.forward({'jsonrpc':'2.0','method':'notifications/initialized'})
    return response
def call(name,args):return rpc('tools/call',{'name':name,'arguments':args})
if __name__=='__main__':
    try:
        init=initialize()
        if len(sys.argv)==1:
            response=rpc('tools/list',{})
            tools=response.get('result',{}).get('tools',[])
            (ROOT/'evidence').mkdir(exist_ok=True)
            (ROOT/'evidence/live-tools.json').write_text(json.dumps(tools,indent=2))
            print(json.dumps([{'name':t['name'],'description':t.get('description','')[:130]}for t in tools],indent=2))
        else:
            name=sys.argv[1]
            args=json.loads(Path(sys.argv[2]).read_text())if len(sys.argv)>2 else{}
            result=call(name,args)
            print(json.dumps(result,indent=2))
    except adapter.ConnectionFailure as error:
        print(json.dumps({'connection_available':False,'reason':str(error)}));sys.exit(2)
