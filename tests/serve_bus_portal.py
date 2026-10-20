#!/usr/bin/env python3
"""Local browser harness: actual flash assets and C++ catalogue lookup; no ESP32/NVS simulation claim."""
import json
import subprocess
import sys
from pathlib import Path
from http.server import BaseHTTPRequestHandler, HTTPServer
from urllib.parse import parse_qs
ROOT=Path(__file__).resolve().parents[1]
TOKEN='browser-test-public-token'
CATALOGUE=json.loads((ROOT/'lib/BusCatalog/catalogue.json').read_text())
CURRENT={
    'frequency':60,
    'stop':'','route':'','direction':'-1','stop_label':'','line_label':'','destination':'',
    'second_stop':'','second_route':'','second_direction':'-1',
    'second_stop_label':'','second_line_label':'','second_destination':'','swapped':False,
}
class Handler(BaseHTTPRequestHandler):
    def log_message(self,*args): pass
    def reply(self,code,body,content_type='application/json; charset=utf-8',gzip=False):
        if isinstance(body,dict): body=json.dumps(body,ensure_ascii=False).encode()
        if isinstance(body,str): body=body.encode()
        self.send_response(code);self.send_header('Content-Type',content_type)
        if gzip:self.send_header('Content-Encoding','gzip')
        self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
    def do_GET(self):
        if self.path in ('/bus','/'):
            self.reply(200,(ROOT/'lib/BusPortal/portal.html').read_text().replace('__CSRF__',TOKEN),'text/html; charset=utf-8')
        elif self.path=='/api/catalogue':self.reply(200,(ROOT/'lib/BusCatalog/catalogue.json.gz').read_bytes(),gzip=True)
        elif self.path=='/api/current': self.reply(200,CURRENT)
        else:self.reply(404,{'message':'Harness: Wi-Fi scan requires real board'})
    def do_POST(self):
        length=int(self.headers.get('Content-Length','0'))
        if length>2048:self.reply(413,{'message':'Request too large'});return
        body=parse_qs(self.rfile.read(length).decode(),keep_blank_values=True)
        data={k:v[0] for k,v in body.items()}
        if data.get('csrf')!=TOKEN:self.reply(403,{'message':'CSRF rejected'});return
        first=('stop','route','direction','destination')
        second=('second_stop','second_route','second_direction','second_destination')
        if any(data.get(k,'') for k in second) and not all(data.get(k,'') for k in second):
            self.reply(400,{'message':'Incomplete second selection'});return
        args=[data.get(k,'') for k in first]+[data.get('frequency','')]
        if all(data.get(k,'') for k in second): args += [data[k] for k in second]
        process=subprocess.run([str(ROOT/'.cache/bus_catalogue_lookup')]+args,capture_output=True,text=True)
        if process.returncode:self.reply(400,{'message':'Catalogue mapping rejected'});return
        def labels(prefix):
            stop=data[prefix+'stop'];route=data[prefix+'route'];direction=data[prefix+'direction'];destination=data[prefix+'destination']
            row=next(s for s in CATALOGUE['stops'] if s['id']==stop)
            service=next(x for x in row['services'] if x['route']==route and x['direction']==direction and x['destination']==destination)
            return row['name'],service['line'],service['destination']
        first_label=labels('')
        second_label=labels('second_') if all(data.get(k,'') for k in second) else ('','','')
        CURRENT.update({
            'frequency':int(data['frequency']),
            'stop':data['stop'],'route':data['route'],'direction':data['direction'],
            'stop_label':first_label[0],'line_label':first_label[1],
            'destination':first_label[2],
            'second_stop':data.get('second_stop',''),'second_route':data.get('second_route',''),
            'second_direction':data.get('second_direction','-1'),
            'second_stop_label':second_label[0],'second_line_label':second_label[1],
            'second_destination':second_label[2],'swapped':False,
        })
        (ROOT/'.cache/browser-submit.json').write_text(json.dumps({'submitted':data,'cpp_mapping':process.stdout,'current':CURRENT},ensure_ascii=False,indent=2))
        self.reply(200,{'message':'Harness : sélection validée par le catalogue C++ (pas de NVS matériel).'})
if __name__=='__main__':HTTPServer(('127.0.0.1',int(sys.argv[1]) if len(sys.argv)>1 else 18765),Handler).serve_forever()
