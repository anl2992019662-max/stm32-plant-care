import pathlib, subprocess, threading

ROOT=pathlib.Path(__file__).resolve().parents[1]
class Simulator:
    demo=True
    def __init__(self):
        filename='plant_sim.exe' if __import__('os').name=='nt' else 'plant_sim'
        binary=ROOT/'build'/'tests'/filename
        if not binary.exists(): raise RuntimeError('请先运行 tools/test.py，生成本机软件演示程序。')
        options={}
        if __import__('os').name=='nt': options['creationflags']=subprocess.CREATE_NO_WINDOW
        self.process=subprocess.Popen([str(binary)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,**options)
        self.lock=threading.Lock()
    def write(self,data):
        with self.lock:
            self.process.stdin.write(data); self.process.stdin.flush()
    def readline(self): return self.process.stdout.readline(1025)
    def close(self):
        if self.process.poll() is None:
            self.process.terminate()
            try: self.process.wait(timeout=2)
            except subprocess.TimeoutExpired: self.process.kill(); self.process.wait()
        self.process.stdin.close(); self.process.stdout.close()

class Serial:
    demo=False
    def __init__(self,port):
        try: import serial
        except ImportError as exc: raise RuntimeError('实物连接需先安装 requirements.txt 中的 pyserial。') from exc
        self.port=serial.Serial(port,9600,timeout=0.2,write_timeout=0.5)
    def write(self,data): self.port.write(data)
    def readline(self): return self.port.readline(1025)
    def close(self): self.port.close()

def ports():
    try:
        from serial.tools import list_ports
        return [p.device for p in list_ports.comports()]
    except ImportError: return []
