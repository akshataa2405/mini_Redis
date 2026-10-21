import socket
import os
from fastapi import FastAPI, HTTPException
from fastapi.staticfiles import StaticFiles
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel
from typing import Optional, List, Any
import redis

app = FastAPI(title="MiniRedis Dashboard API")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

REDIS_HOST = "127.0.0.1"
REDIS_PORT = 6380

def get_redis_client():
    return redis.Redis(host=REDIS_HOST, port=REDIS_PORT, decode_responses=True, socket_timeout=2)

def execute_raw_command(cmd_str: str) -> str:
    """Send a raw command over TCP to MiniRedis server and return reply."""
    parts = cmd_str.strip().split()
    if not parts:
        return ""
    
    # Format as RESP array of bulk strings
    payload = f"*{len(parts)}\r\n"
    for part in parts:
        payload += f"${len(part.encode('utf-8'))}\r\n{part}\r\n"
    
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.settimeout(2.0)
            s.connect((REDIS_HOST, REDIS_PORT))
            s.sendall(payload.encode("utf-8"))
            
            # Read response
            data = s.recv(16384).decode("utf-8", errors="replace")
            return data
    except Exception as e:
        return f"-ERR Failed to communicate with MiniRedis: {str(e)}\r\n"

class CommandRequest(BaseModel):
    command: str

class SetRequest(BaseModel):
    key: str
    value: str
    ttl: Optional[int] = None # in seconds

@app.get("/api/health")
def health():
    try:
        r = get_redis_client()
        pong = r.ping()
        return {"status": "online", "ping": pong}
    except Exception as e:
        return {"status": "offline", "error": str(e)}

@app.post("/api/exec")
def exec_command(req: CommandRequest):
    raw_resp = execute_raw_command(req.command)
    return {"raw": raw_resp}

@app.get("/api/keys")
def get_keys(pattern: str = "*"):
    try:
        r = get_redis_client()
        keys = r.keys(pattern)
        items = []
        for k in keys:
            ttl = r.ttl(k)
            # Try to determine type
            val = r.get(k)
            if val is not None:
                items.append({
                    "key": k,
                    "type": "string",
                    "value": val,
                    "ttl": ttl
                })
            else:
                # Check list
                length = r.llen(k)
                if length and length > 0:
                    elements = r.lrange(k, 0, -1)
                    items.append({
                        "key": k,
                        "type": "list",
                        "value": elements,
                        "ttl": ttl
                    })
                else:
                    items.append({
                        "key": k,
                        "type": "unknown",
                        "value": None,
                        "ttl": ttl
                    })
        return {"keys": items}
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.post("/api/set")
def set_key(req: SetRequest):
    try:
        r = get_redis_client()
        if req.ttl and req.ttl > 0:
            r.set(req.key, req.value, ex=req.ttl)
        else:
            r.set(req.key, req.value)
        return {"status": "ok"}
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.get("/api/get")
def get_key(key: str):
    try:
        r = get_redis_client()
        val = r.get(key)
        ttl = r.ttl(key)
        return {"key": key, "value": val, "ttl": ttl}
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.delete("/api/del")
def delete_key(key: str):
    try:
        r = get_redis_client()
        deleted = r.delete(key)
        return {"deleted": deleted}
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.get("/api/stats")
def get_stats():
    try:
        r = get_redis_client()
        all_keys = r.keys("*")
        return {
            "connected": True,
            "port": REDIS_PORT,
            "total_keys": len(all_keys),
            "version": "1.0.0"
        }
    except Exception as e:
        return {
            "connected": False,
            "error": str(e),
            "port": REDIS_PORT,
            "total_keys": 0
        }

# Mount static web frontend
web_dir = os.path.join(os.path.dirname(__file__), "..", "web")
if os.path.exists(web_dir):
    app.mount("/", StaticFiles(directory=web_dir, html=True), name="web")
