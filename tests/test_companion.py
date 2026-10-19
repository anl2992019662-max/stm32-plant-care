import json, pathlib, subprocess, sys, unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'desktop'))
from protocol import decode_telemetry,encode_command,faults_text

class ProtocolTests(unittest.TestCase):
    def sample(self):
        return dict(v=1,ms=100,epoch=0,mode=0,t_dC=251,soil=55,light=35,co2=None,raw=[2200,2450],
                    valid=7,cal=3,out=0,fault=8,water=1,pump_ms=0,io_errors=0)
    def test_units_and_null(self):
        data=self.sample(); self.assertEqual(decode_telemetry(json.dumps(data)),data)
        self.assertIsNone(decode_telemetry('OK PONG'))
    def test_invalid_frame(self):
        for key,value in [('mode',3),('out',16),('t_dC',9999),('co2',0),('raw',[5000,10]),('soil',True),('v',2)]:
            data=self.sample(); data[key]=value
            with self.assertRaises(ValueError): decode_telemetry(json.dumps(data))
        with self.assertRaises(ValueError): decode_telemetry('{}')
        with self.assertRaises(ValueError): decode_telemetry('{bad json}')
    def test_no_command_injection(self):
        self.assertEqual(encode_command('SET FAN 1'),b'SET FAN 1\n')
        for text in ['STOP\nSET PUMP 1','', 'A'*96,'模式 AUTO','STOP\0']:
            with self.assertRaises(ValueError): encode_command(text)
    def test_fault_descriptions(self):
        self.assertIn('缺水',faults_text(16)); self.assertIn('未完成标定',faults_text(128)); self.assertEqual(faults_text(0),'无')

class SameCoreSimulationTests(unittest.TestCase):
    def test_actual_c_core_roundtrip(self):
        executable=ROOT/'build'/'tests'/('plant_sim.exe' if sys.platform=='win32' else 'plant_sim')
        self.assertTrue(executable.exists(),'tools/test.py must build the simulator first')
        script='SENS 9999999999999999999999 20 20 700 1\nMODE AUTO\nSENS 330 20 20 700 1\nTICK 3000\nSENS 330 20 20 700 0\nMODE REMOTE\nSET FAN 1\nTICK 10001\nGET\n'
        result=subprocess.run([str(executable)],input=script,text=True,capture_output=True,check=True)
        self.assertIn('ERR SENS',result.stdout)
        frames=[decode_telemetry(line) for line in result.stdout.splitlines() if line.startswith('{')]
        self.assertTrue(frames[0]['out']&2,'dry soil starts a bounded watering pulse')
        self.assertFalse(frames[1]['out']&2,'pump stops after its limit')
        self.assertEqual(frames[-1]['mode'],0)
        self.assertEqual(frames[-1]['out'],0)
        self.assertTrue(frames[-1]['fault']&32,'remote timeout recorded')

if __name__=='__main__': unittest.main()
