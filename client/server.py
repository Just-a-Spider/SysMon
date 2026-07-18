import os
os.environ["PROCFS_PATH"] = "/host/proc"

from fastapi import FastAPI
from fastapi.staticfiles import StaticFiles
from fastapi.middleware.cors import CORSMiddleware
import psutil
import uvicorn
import glob

app = FastAPI()

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

def get_hwmon_data():
    cpu_fan = 0
    gpu_fan = 0
    cpu_temp = 0.0
    gpu_temp = 0.0
    
    # We mount /sys to /host/sys
    hwmon_paths = glob.glob('/host/sys/class/hwmon/hwmon*')
    for path in hwmon_paths:
        try:
            with open(os.path.join(path, 'name'), 'r') as f:
                name = f.read().strip()
                
            if name == 'amdgpu' or 'gpu' in name.lower():
                # Read GPU fan
                fan_files = glob.glob(os.path.join(path, 'fan*_input'))
                if fan_files:
                    with open(fan_files[0], 'r') as f:
                        gpu_fan = int(f.read().strip())
                # Read GPU temp
                temp_files = glob.glob(os.path.join(path, 'temp*_input'))
                if temp_files:
                    with open(temp_files[0], 'r') as f:
                        gpu_temp = float(f.read().strip()) / 1000.0
                        
            elif name == 'coretemp' or 'cpu' in name.lower() or name == 'k10temp':
                # Read CPU temp
                temp_files = glob.glob(os.path.join(path, 'temp*_input'))
                if temp_files:
                    with open(temp_files[0], 'r') as f:
                        cpu_temp = float(f.read().strip()) / 1000.0
                        
            # Some motherboards expose fans here
            if name == 'nct6775' or name == 'it87' or name == 'nct6798':
                fan_files = glob.glob(os.path.join(path, 'fan*_input'))
                if fan_files:
                    with open(fan_files[0], 'r') as f:
                        cpu_fan = int(f.read().strip())
        except Exception:
            pass

    return {"cpu_fan": cpu_fan, "gpu_fan": gpu_fan, "cpu_temp": cpu_temp, "gpu_temp": gpu_temp}

@app.get("/api/data")
def get_data():
    hw_data = get_hwmon_data()
    cpu_usage = psutil.cpu_percent(interval=None)
    
    mem = psutil.virtual_memory()
    free_ram = round(mem.available / 1000000000, 2)
    
    return {
        "cpu_fan": hw_data["cpu_fan"],
        "gpu_fan": hw_data["gpu_fan"],
        "cpu_temp": hw_data["cpu_temp"],
        "gpu_temp": hw_data["gpu_temp"],
        "free_ram": free_ram,
        "cpu_usage": cpu_usage
    }

static_dir = os.path.join(os.path.dirname(__file__), "dist/client/browser")
if os.path.isdir(static_dir):
    app.mount("/", StaticFiles(directory=static_dir, html=True), name="static")

if __name__ == "__main__":
    uvicorn.run(app, host="0.0.0.0", port=3000)
