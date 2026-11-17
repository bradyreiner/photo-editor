"""Small localhost-only client for Looksy's authenticated native MCP bridge."""
import argparse
import json
from pathlib import Path
import urllib.parse
import urllib.request
import urllib.error
import time


class Looksy:
    def __init__(self, session):
        self.session=json.loads(Path(session).read_text())
        self.endpoint=self.session["mcpEndpoint"]
        parsed=urllib.parse.urlparse(self.endpoint)
        if parsed.scheme!="http" or parsed.hostname not in ("127.0.0.1","localhost","::1"):
            raise ValueError("Only the app's localhost MCP endpoint is supported")
        self.index=0
        self.last_request=0.0

    def rpc(self,method,params):
        self.index+=1
        request=urllib.request.Request(self.endpoint,
            data=json.dumps(dict(jsonrpc="2.0",id=self.index,method=method,params=params)).encode(),
            headers={"Authorization":"Bearer "+self.session["token"],
                     "Content-Type":"application/json","Accept":"application/json, text/event-stream"})
        for attempt in range(31):
            time.sleep(max(0,.65-(time.monotonic()-self.last_request)))
            self.last_request=time.monotonic()
            try:
                with urllib.request.urlopen(request,timeout=60) as reply:
                    result=json.load(reply)
                break
            except urllib.error.HTTPError as error:
                # The native bridge rejects 429 before command admission. It is
                # safe to retry that rejection; never retry unknown transport
                # failures after a potentially accepted mutation.
                if error.code!=429 or attempt==30:raise
                time.sleep(2)
        if "error" in result:raise RuntimeError(result["error"])
        return result["result"]

    def call(self,name,arguments):
        result=self.rpc("tools/call",dict(name=name,arguments=arguments))
        if "structuredContent" in result:return result["structuredContent"]
        for item in result.get("content",[]):
            if item.get("type")=="text":
                try:return json.loads(item["text"])
                except json.JSONDecodeError:pass
        return result


if __name__=="__main__":
    p=argparse.ArgumentParser()
    p.add_argument("session")
    p.add_argument("--list",action="store_true")
    p.add_argument("--schema")
    p.add_argument("--call")
    p.add_argument("--arguments",default="{}")
    p.add_argument("--output")
    args=p.parse_args()
    client=Looksy(args.session)
    if args.call:
        result=client.call(args.call,json.loads(args.arguments))
        if args.output:Path(args.output).write_text(json.dumps(result,indent=2))
        else:print(json.dumps(result,indent=2))
        raise SystemExit(0)
    tools=client.rpc("tools/list",{})["tools"]
    if args.schema:
        print(json.dumps(next(t for t in tools if t["name"]==args.schema),indent=2))
    else:
        print(json.dumps([dict(name=t["name"],description=t.get("description","")[:120]) for t in tools],indent=2))
