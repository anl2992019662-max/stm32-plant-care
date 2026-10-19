"""Strict decoding for untrusted serial lines. Values use the firmware's units."""
import json

def decode_telemetry(line):
    if len(line)>1024 or not line.startswith('{'):
        return None
    obj=json.loads(line)
    required={'v','ms','epoch','mode','t_dC','soil','light','co2','raw','valid','cal','out','fault','water','pump_ms','io_errors'}
    if not isinstance(obj,dict) or not required.issubset(obj): raise ValueError('missing telemetry field')
    for name,maximum in [('ms',2**32-1),('epoch',2**32-1),('mode',2),('valid',15),('cal',3),('out',15),
                         ('fault',255),('water',1),('pump_ms',15000),('io_errors',2**32-1)]:
        if type(obj[name]) is not int or not 0<=obj[name]<=maximum: raise ValueError('invalid '+name)
    if type(obj['v']) is not int or obj['v']!=1: raise ValueError('unsupported protocol')
    for name,low,high in [('t_dC',-550,1250),('soil',0,100),('light',0,100),('co2',400,10000)]:
        if obj[name] is not None and (type(obj[name]) is not int or not low<=obj[name]<=high): raise ValueError('invalid '+name)
    if not isinstance(obj['raw'],list) or len(obj['raw'])!=2 or any(type(n) is not int or not 0<=n<=4095 for n in obj['raw']):
        raise ValueError('invalid raw ADC')
    return obj

FAULT_NAMES={1:'温度无效',2:'土壤采样无效',4:'光照采样无效',8:'CO₂预热/无效',16:'缺水/浮球未接',
             32:'远程控制超时',64:'浇水预算用尽',128:'未完成标定'}
def faults_text(mask):
    return '、'.join(name for bit,name in FAULT_NAMES.items() if mask&bit) or '无'

def encode_command(text):
    if not text or len(text)>95 or any(ord(c)<32 or ord(c)>126 for c in text):
        raise ValueError('指令必须是 1–95 个可打印 ASCII 字符')
    return (text+'\n').encode('ascii')
