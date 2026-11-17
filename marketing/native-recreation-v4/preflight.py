"""Check command plans against the live native schemas before any mutation."""
from pathlib import Path
import json
import re
import sys

from native_mcp import call
from discover import unpack


def check(value, schema, path, errors):
    if isinstance(value, dict) and set(value)=={"result","field"}:
        value="00000000-0000-0000-0000-000000000000"
    kind=schema.get("type")
    valid={"object":lambda:isinstance(value,dict),"array":lambda:isinstance(value,list),
           "string":lambda:isinstance(value,str),"boolean":lambda:isinstance(value,bool),
           "number":lambda:isinstance(value,(int,float)) and not isinstance(value,bool),
           "integer":lambda:isinstance(value,int) and not isinstance(value,bool)}
    if kind in valid and not valid[kind]():errors.append(path+": expected "+kind);return
    if "enum" in schema and value not in schema["enum"]:errors.append(path+": invalid enum value")
    if kind=="object":
        properties=schema.get("properties",{})
        for name in schema.get("required",[]):
            if name not in value:errors.append(path+": missing "+name)
        for name,item in value.items():
            if name not in properties:
                if schema.get("additionalProperties") is False:errors.append(path+": unknown "+name)
            else:check(item,properties[name],path+"."+name,errors)
    elif kind=="array":
        if len(value)<schema.get("minItems",0):errors.append(path+": too few items")
        if len(value)>schema.get("maxItems",float("inf")):errors.append(path+": too many items")
        for i,item in enumerate(value):check(item,schema.get("items",{}),path+f"[{i}]",errors)
    elif kind in ("number","integer"):
        if value<schema.get("minimum",-float("inf")):errors.append(path+": below minimum "+str(schema['minimum']))
        if value>schema.get("maximum",float("inf")):errors.append(path+": above maximum "+str(schema['maximum']))
        if "exclusiveMinimum" in schema and value<=schema["exclusiveMinimum"]:errors.append(path+": below exclusive minimum")
    elif kind=="string":
        if len(value)>schema.get("maxLength",float("inf")):errors.append(path+": too long")
        if "pattern" in schema and not re.search(schema["pattern"],value):errors.append(path+": invalid pattern")


def preflight(path):
    plan=json.loads(path.read_text());schemas={};errors=[]
    for step in plan['steps']:
        command=step['commandId']
        if command not in schemas:
            schema=unpack(call('app.command_schema',{'commandId':command}))
            schemas[command]=schema
        schema=schemas[command]
        if not schema.get('available'):
            errors.append(step['id']+': unavailable '+command);continue
        check(step['values'],schema['inputSchema'],step['id'],errors)
    result={'plan':str(path),'stepCount':len(plan['steps']),'schemaCount':len(schemas),
            'valid':not errors,'errors':errors,'schemas':schemas}
    output=path.with_suffix('.preflight.json');output.write_text(json.dumps(result,indent=2)+'\n')
    print(str(path),len(errors),'schema errors')
    for error in errors[:24]:print(error)
    return not errors


if __name__=='__main__':
    sys.exit(0 if preflight(Path(sys.argv[1])) else 1)
