"""Local Tk GUI. Demo runs the same compiled C core; serial mode uses HC-05 SPP."""
import argparse, csv, datetime, pathlib, queue, sys, threading, time
import tkinter as tk
from tkinter import ttk, filedialog, messagebox

sys.path.insert(0,str(pathlib.Path(__file__).resolve().parent))
from protocol import decode_telemetry,encode_command,faults_text
from transport import Simulator,Serial,ports

class App:
    def __init__(self,root,demo=False):
        self.root=root; self.demo=demo; self.link=None; self.generation=0; self.remote_owned=False
        self.events=queue.Queue(maxsize=500); self.logfile=None; self.writer=None
        self.last_received=0.; self.last_tick=time.monotonic(); self.history=[]
        root.title('绿植养护控制台 — '+('软件演示（无硬件）' if demo else '蓝牙串口'))
        root.geometry('960x780'); root.minsize(820,700)
        root.protocol('WM_DELETE_WINDOW',self.close)
        style=ttk.Style(root); style.configure('Value.TLabel',font=('Microsoft YaHei UI',18))
        frame=ttk.Frame(root,padding=16); frame.pack(fill='both',expand=True)
        ttk.Label(frame,text='STM32 多参数绿植养护',font=('Microsoft YaHei UI',20,'bold')).pack(anchor='w')
        ttk.Label(frame,text='软件演示数据 · 未连接传感器' if demo else '9600 / 8N1 · 配对后的 HC-05 串口',foreground='#9b5c00' if demo else '#38556a').pack(anchor='w',pady=(4,12))
        connection=ttk.Frame(frame); connection.pack(fill='x')
        self.port=tk.StringVar(); self.port_box=ttk.Combobox(connection,textvariable=self.port,values=ports(),width=18)
        self.port_box.pack(side='left'); self.port_box.configure(state='disabled' if demo else 'normal')
        ttk.Button(connection,text='刷新串口',command=lambda:self.port_box.configure(values=ports())).pack(side='left',padx=4)
        self.connect_btn=ttk.Button(connection,text='启动演示' if demo else '连接',command=self.connect); self.connect_btn.pack(side='left',padx=4)
        ttk.Button(connection,text='断开',command=self.disconnect).pack(side='left')
        self.status=tk.StringVar(value='未连接'); ttk.Label(connection,textvariable=self.status).pack(side='right')
        cards=ttk.Frame(frame); cards.pack(fill='x',pady=16)
        self.values={}
        for col,(key,title) in enumerate([('t_dC','温度'),('soil','土壤相对湿度'),('light','相对光照'),('co2','CO₂')]):
            card=ttk.LabelFrame(cards,text=title,padding=12); card.grid(row=0,column=col,padx=4,sticky='nsew'); cards.columnconfigure(col,weight=1)
            value=tk.StringVar(value='—'); self.values[key]=value; ttk.Label(card,textvariable=value,style='Value.TLabel').pack()
        modes=ttk.Frame(frame); modes.pack(fill='x')
        for label,mode in [('手动 / 输出归零','MANUAL'),('自动养护','AUTO'),('蓝牙控制','REMOTE')]:
            ttk.Button(modes,text=label,command=lambda m=mode:self.set_mode(m)).pack(side='left',padx=4)
        ttk.Button(modes,text='停止全部输出',command=lambda:self.set_mode('STOP')).pack(side='right')
        self.mode=tk.StringVar(value='模式：—'); ttk.Label(frame,textvariable=self.mode).pack(anchor='w',pady=8)
        controls=ttk.Frame(frame); controls.pack(fill='x')
        self.outputs={}
        for col,(label,key,bit) in enumerate([('风扇','FAN',1),('水泵','PUMP',2),('补光灯','LAMP',4),('遮阳','SHADE',8)]):
            group=ttk.LabelFrame(controls,text=label,padding=8); group.grid(row=0,column=col,padx=4,sticky='ew'); controls.columnconfigure(col,weight=1)
            state=tk.StringVar(value='实际：—'); self.outputs[bit]=state; ttk.Label(group,textvariable=state).pack()
            ttk.Button(group,text='请求开启',command=lambda k=key:self.send('SET '+k+' 1')).pack(side='left')
            ttk.Button(group,text='关闭',command=lambda k=key:self.send('SET '+k+' 0')).pack(side='left')
        self.faults=tk.StringVar(value='故障与标定：—'); ttk.Label(frame,textvariable=self.faults,wraplength=880,foreground='#8b3b25').pack(anchor='w',pady=8)
        self.raw=tk.StringVar(value='原始采样：—'); ttk.Label(frame,textvariable=self.raw).pack(anchor='w')
        if demo:
            scenarios=ttk.Frame(frame); scenarios.pack(fill='x',pady=8)
            ttk.Label(scenarios,text='演示场景：').pack(side='left')
            for label,command in [('正常','SENS 250 60 50 700 1'),('高温干燥','SENS 330 20 20 800 1'),('强光高 CO₂','SENS 260 55 95 1800 1'),('水箱缺水','SENS 330 20 20 800 0')]:
                ttk.Button(scenarios,text=label,command=lambda c=command:self.send(c)).pack(side='left',padx=3)
        ttk.Label(frame,text='最近 120 点：蓝色温度（℃） / 绿色土壤相对湿度（%），纵轴 0–100').pack(anchor='w',pady=(8,0))
        self.chart=tk.Canvas(frame,height=110,background='#f4f7f8',highlightthickness=0); self.chart.pack(fill='x',pady=4)
        commandbar=ttk.Frame(frame); commandbar.pack(fill='x',pady=8)
        self.command=tk.StringVar(); entry=ttk.Entry(commandbar,textvariable=self.command); entry.pack(side='left',fill='x',expand=True)
        entry.bind('<Return>',lambda event:self.send_entry())
        ttk.Button(commandbar,text='发送指令',command=self.send_entry).pack(side='left',padx=4)
        ttk.Button(commandbar,text='记录 CSV',command=self.toggle_log).pack(side='left')
        self.console=tk.Text(frame,height=6,font=('Consolas',10),state='disabled',wrap='word'); self.console.pack(fill='both',expand=True)
        ttk.Label(frame,text='OK REQUEST 表示请求已接收；以“实际”输出为准。标定 / 阈值修改后需 SAVE。').pack(anchor='w',pady=(8,0))
        root.after(100,self.poll)
    def note(self,text):
        self.console.configure(state='normal'); self.console.insert('end',text+'\n')
        if int(self.console.index('end-1c').split('.')[0])>200: self.console.delete('1.0','50.0')
        self.console.see('end'); self.console.configure(state='disabled')
    def connect(self):
        if self.link: return
        try: self.link=Simulator() if self.demo else Serial(self.port.get().strip())
        except Exception as exc: messagebox.showerror('连接失败',str(exc)); return
        self.generation+=1; generation=self.generation; link=self.link; self.last_received=time.monotonic()
        def reader():
            pending=b''; discarding=False
            try:
                while self.generation==generation:
                    data=link.readline()
                    if data:
                        pending+=data
                        while b'\n' in pending:
                            complete,pending=pending.split(b'\n',1)
                            if not discarding and len(complete)<=1024:
                                try: self.events.put_nowait((generation,complete.decode('ascii',errors='strict').strip()))
                                except UnicodeError: self.events.put_nowait((generation,'ERR 非 ASCII 数据'))
                            discarding=False
                        if len(pending)>1024: pending=b''; discarding=True
                    elif getattr(link,'demo',False): break
            except Exception as exc:
                try: self.events.put_nowait((generation,'ERR 连接读取失败：'+str(exc)))
                except queue.Full: pass
        threading.Thread(target=reader,daemon=True).start()
        self.connect_btn.configure(state='disabled'); self.status.set('已连接'); self.last_tick=time.monotonic()
        self.send('GET'); self.send('CFG')
    def disconnect(self):
        if self.link:
            if self.remote_owned:
                try: self.link.write(encode_command('STOP'))
                except Exception: pass
            self.generation+=1
            try: self.link.close()
            except Exception: pass
        self.link=None; self.remote_owned=False; self.status.set('已断开'); self.connect_btn.configure(state='normal')
    def send(self,text,quiet=False):
        if not self.link:
            if not quiet: self.note('请先连接或启动演示。')
            return False
        try: self.link.write(encode_command(text))
        except Exception as exc: self.note('发送失败：'+str(exc)); self.disconnect(); return False
        if not quiet: self.note('> '+text)
        return True
    def set_mode(self,mode):
        if self.send('STOP' if mode=='STOP' else 'MODE '+mode): self.remote_owned=mode=='REMOTE'
    def send_entry(self):
        text=self.command.get().strip()
        if text=='MODE REMOTE': self.set_mode('REMOTE')
        else:
            if text in ('STOP','MODE MANUAL','MODE AUTO'): self.remote_owned=False
            self.send(text)
        self.command.set('')
    def show(self,data):
        self.last_received=time.monotonic()
        for key,unit in [('t_dC',' ℃'),('soil',' %'),('light',' %'),('co2',' ppm')]:
            value=data[key]
            if value is None: shown='—'
            elif key=='t_dC': shown=f'{value/10:.1f}'+unit
            else: shown=str(value)+unit
            self.values[key].set(shown)
        mode=['手动','自动','蓝牙控制'][data['mode']]
        self.mode.set('模式：'+mode+f" · 标定标志 {data['cal']} · 浇水累计 {data['pump_ms']/1000:.1f}s")
        if data['mode']!=2: self.remote_owned=False
        for bit,value in self.outputs.items(): value.set('实际：'+('开' if data['out']&bit else '关'))
        self.faults.set('故障与标定：'+faults_text(data['fault']))
        self.raw.set(f"原始 ADC：土壤 {data['raw'][0]} / 光照 {data['raw'][1]} · 水箱 {'有水' if data['water'] else '缺水/未接'} · I/O 错误 {data['io_errors']}")
        self.history.append((None if data['t_dC'] is None else data['t_dC']/10,data['soil'])); self.history=self.history[-120:]; self.draw()
        if self.writer:
            self.writer.writerow([datetime.datetime.now(datetime.timezone.utc).isoformat(),self.demo,data['ms'],data['t_dC'],data['soil'],data['light'],data['co2'],data['mode'],data['out'],data['fault'],data['water'],data['pump_ms']]); self.logfile.flush()
    def draw(self):
        canvas=self.chart; canvas.delete('all'); width=max(canvas.winfo_width(),100); height=105
        for y,label in [(5,'100'),(55,'50'),(105,'0')]:
            canvas.create_line(28,y,width,y,fill='#d6e0e5'); canvas.create_text(14,y,text=label,fill='#637786')
        for col,color in [(0,'#3270ad'),(1,'#29845f')]:
            previous=None
            for i,point in enumerate(self.history):
                value=point[col]
                if value is None: previous=None; continue
                coordinate=(30+i*(width-35)/119,105-max(0,min(100,value)))
                if previous: canvas.create_line(*previous,*coordinate,fill=color,width=2)
                previous=coordinate
    def toggle_log(self):
        if self.logfile: self.logfile.close(); self.logfile=None; self.writer=None; self.note('CSV 记录已停止。'); return
        filename=filedialog.asksaveasfilename(defaultextension='.csv',initialfile='plant-demo.csv' if self.demo else 'plant-live.csv',filetypes=[('CSV','*.csv')])
        if not filename: return
        try:
            self.logfile=open(filename,'w',newline='',encoding='utf-8-sig'); self.writer=csv.writer(self.logfile)
            self.writer.writerow(['utc','software_demo','ms','temperature_dC','soil_relative_pct','light_relative_pct','co2_ppm','mode','outputs','faults','water_ok','pump_used_ms'])
            self.note('开始记录 CSV。')
        except OSError as exc: messagebox.showerror('记录失败',str(exc))
    def poll(self):
        for _ in range(100):
            try: generation,line=self.events.get_nowait()
            except queue.Empty: break
            if generation!=self.generation: continue
            try: data=decode_telemetry(line)
            except (ValueError,TypeError) as exc: self.note('数据无效：'+str(exc)); continue
            if data is not None: self.show(data)
            elif line!='OK PONG': self.note(line)
        now=time.monotonic()
        if self.link and now-self.last_tick>=1:
            dt=min(int((now-self.last_tick)*1000),600000); self.last_tick=now
            if self.demo: self.send('TICK '+str(dt),quiet=True)
            if self.remote_owned and now-self.last_received>6:
                self.send('STOP',quiet=True); self.remote_owned=False; self.note('遥测超时，已发送停止指令。')
            if self.remote_owned: self.send('PING',quiet=True)
        if self.link: self.status.set('数据超时，请检查连接' if now-self.last_received>6 else ('软件演示中' if self.demo else '数据接收中'))
        self.root.after(100,self.poll)
    def close(self):
        self.disconnect()
        if self.logfile: self.logfile.close()
        self.root.destroy()

def main():
    parser=argparse.ArgumentParser(); parser.add_argument('--demo',action='store_true'); parser.add_argument('--self-check',action='store_true')
    args=parser.parse_args(); root=tk.Tk()
    if args.self_check: root.withdraw()
    app=App(root,args.demo)
    if args.self_check: root.update_idletasks(); app.close(); print('GUI widgets created successfully; no hardware connection.')
    else: root.mainloop()

if __name__=='__main__': main()
