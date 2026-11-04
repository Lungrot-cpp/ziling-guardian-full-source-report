"""Offline event preview and read-only DSH MCP tools. Standard library only."""
import argparse
import base64
import json
import struct
import sys
import time
import urllib.request
import zlib
from activity_history import DEFAULT_DB, summarize
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parent
BOARD = 'http://192.168.0.2'
SCENES = [
    dict(id='E01', title='低位姿态后持续未起身', label='需要查看',
         poses=['standing', 'bending', 'lying', 'lying'], states=[0, 1, 1, 2],
         times=[0, 2, 6, 12],
         facts=['0 秒：目标站立，状态正常。', '2 秒：目标转为低位姿态，进入预警。',
                '6 秒：仍处于低位。', '12 秒：板卡记录报警，示意人物仍在地面附近。'],
         explanation='模拟片段中，人物从站立变为低位，12 秒时仍未起身并出现报警。无法确认是否跌倒。',
         current='模拟片段末帧仍处于低位，未见起身；这不是实时画面。',
         alternative='可能，但后续仍处于低位，也可能是主动躺卧。仅凭片段不能确定。'),
    dict(id='E02', title='弯腰后恢复站立', label='已恢复站立',
         poses=['standing', 'bending', 'bending', 'standing'], states=[0, 1, 1, 0],
         times=[0, 2, 4, 6],
         facts=['0 秒：站立。', '2 秒：上身前倾，触发预警。', '4 秒：仍处于弯腰姿态。', '6 秒：恢复站立，状态正常。'],
         explanation='模拟片段中，人物弯腰后在第 6 秒恢复站立，预警解除。具体动作意图未知。',
         current='模拟片段末帧已经站立，状态正常；这不是实时画面。',
         alternative='有可能。片段显示后来恢复站立，但看不出是否捡到了物品。'),
    dict(id='E03', title='遮挡下的低位目标', label='证据不足',
         poses=['standing', 'hidden', 'hidden', 'hidden'], states=[0, 1, 1, 2],
         times=[0, 2, 6, 12],
         facts=['0 秒：人物可见。', '2 秒：人物进入家具后方。', '6 秒：身体主要部分仍被遮挡。', '12 秒：状态记录报警，但完整姿态不可见。'],
         explanation='模拟记录出现报警，但身体被家具遮挡，无法确认具体动作。需要补充视角。',
         current='无法判断。模拟末帧被遮挡，看不见起身不代表没有起身。',
         alternative='有可能，但遮挡太多，捡东西、坐下或跌倒都无法确认。'),
]

GUIDANCE = '''你是板卡事件助手。默认只答一两句，通常不超过60个汉字；先给结论，不复述问题、不汇报操作。
只有用户追问依据时才列采样时间和事实；不要每次重复整套限制。用“模拟片段”简短标明来源。
工具中的模拟素材不是现场证据。用“模拟片段”标来源；未追问时不要展开时间线。
区分板卡记录、图像可见事实、推测、未知；不能从低位姿态推断受伤或摔倒原因。
遮挡/漏检不能当作未起身，末帧不能当作当前实时画面。不同样例不能串成同一事件。
追问时引用事件号与采样时间、其他合理解释。报警解释不取消原报警。
用户询问最新情况时重新读取；无历史记录或连接失败时明确说明，不补造数据。'''


def scene_by_id(event_id):
    return next(s for s in SCENES if s['id'] == event_id)


def example_answer(event_id, question):
    s = scene_by_id(event_id)
    q = question.strip()
    if not q or len(q) > 500:
        raise ValueError('问题需为 1～500 个字符')
    if any(w in q for w in ['温度', '湿度']):
        answer = '目前是离线样例，没有实测温湿度。接通板卡后才能查询。'
    elif any(w in q for w in ['比较', '区别', '上次']):
        answer = '三个模拟片段：E01 仍处低位，E02 已恢复站立，E03 遮挡无法判断。'
    elif any(w in q for w in ['依据', '证据', '截图', '画面']):
        answer = '模拟记录：\n' + '\n'.join(s['facts'])
    elif any(w in q for w in ['现在', '起来', '起身', '恢复']):
        answer = s['current']
    elif any(w in q for w in ['捡', '可能', '会不会', '确定']):
        answer = s['alternative']
    elif any(w in q for w in ['发生', '怎么', '为什么', '解释', '分析']):
        answer = s['explanation']
    else:
        answer = '这里没有调用语言模型，暂不支持这个问题。可以问“发生了什么”或“依据是什么”。'
    return dict(answer=answer, event_id=event_id, source='模拟数据 · 预设回答 · 未调用模型')


def board_json():
    # Fixed destination: a QQ request cannot turn this tool into an arbitrary URL fetcher.
    with urllib.request.urlopen(BOARD + '/api/status', timeout=3) as response:
        payload = response.read(100001)
    if len(payload) > 100000:
        raise ValueError('板卡响应超出上限')
    value = json.loads(payload)
    if value.get('server') != 'riscv-lwip' or 'uptime_ms' not in value:
        raise ValueError('无法识别板卡状态格式')
    sensors = value.get('sensors', {})
    return dict(source='真实板卡 HTTP 快照', received_at=time.time(),
                uptime_ms=value['uptime_ms'], frame_id=value.get('frame_id'),
                alarm_latched=value.get('alarm_latched'), tracks=value.get('tracks', []),
                sensors={k: sensors.get(k) for k in ['ambient', 'humidity', 'env_valid', 'environment_age_us']},
                note='温湿度原始单位为百分之一℃/%RH；须同时核对有效标志和数据年龄。alarm_latched 是锁存报警，不等同当前姿态。')


def diagram_png(pose):
    """A schematic, not a photograph. Return a dependency-free PNG for MCP vision."""
    w, h = 480, 270
    pixels = [bytearray([244, 240, 233] * w) for _ in range(h)]
    def rect(x, y, width, height, color):
        for yy in range(max(0, y), min(h, y + height)):
            for xx in range(max(0, x), min(w, x + width)):
                pixels[yy][xx*3:xx*3+3] = bytes(color)
    def line(x1, y1, x2, y2):
        n = max(abs(x2-x1), abs(y2-y1), 1)
        for i in range(n+1):
            rect(round(x1+(x2-x1)*i/n)-4, round(y1+(y2-y1)*i/n)-4, 8, 8, (78, 92, 88))
    rect(0, 225, w, 2, (165, 158, 148))
    points = {'standing': [(220,80),(220,140),(196,220),(244,220)],
              'bending': [(266,138),(218,155),(196,220),(240,220)],
              'lying': [(166,207),(235,207),(300,215),(310,198)],
              'hidden': [(242,169),(228,195),(210,220),(250,220)]}[pose]
    head, hip, foot1, foot2 = points
    for y in range(head[1]-24, head[1]):
        for x in range(head[0]-12, head[0]+13):
            if (x-head[0])**2+(y-head[1]+12)**2 < 144:
                rect(x,y,1,1,(78,92,88))
    line(*head,*hip); line(*hip,*foot1); line(*hip,*foot2)
    line(*head,head[0]+28,head[1]+42)
    if pose == 'hidden':
        rect(185,130,130,94,(174,165,151))
    def chunk(kind, data):
        return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)
    raw = b''.join(b'\0'+bytes(row) for row in pixels)
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b'')


def call_tool(name, args, live=False, activity_db=DEFAULT_DB):
    if name == 'board_activity_summary':
        if not live:
            raise ValueError('活动统计只读取真实采集记录，不提供模拟历史')
        result = summarize(activity_db, args.get('date', 'today'))
        return {'content': [{'type':'text','text':json.dumps(result,ensure_ascii=False)}]}
    if name == 'board_current_status':
        if live:
            result = board_json()
        else:
            result = dict(source='离线模拟', real_board_connected=False,
                          note='没有实时传感器值；请读取事件样例，不能称为当前现场状态。')
        return {'content': [{'type':'text','text':json.dumps(result,ensure_ascii=False)}]}
    if name == 'board_event_context':
        if live:
            raise ValueError('真实历史采集尚未启用；没有历史片段可供分析，不使用模拟数据替代。')
        s = scene_by_id(args.get('event_id','E01'))
        evidence = {k:s[k] for k in ['id','title','times','states','facts']}
        result = [{'type':'text','text':json.dumps(dict(source='合成示意图，非相机照片', guidance=GUIDANCE, evidence=evidence),ensure_ascii=False)}]
        for t, pose in zip(s['times'],s['poses']):
            result.append({'type':'text','text':f'事件 {s["id"]}，相对时间 {t} 秒，模拟示意图'})
            result.append({'type':'image','mimeType':'image/png','data':base64.b64encode(diagram_png(pose)).decode()})
        return {'content':result}
    raise ValueError('未知工具')


TOOLS = [dict(name='board_activity_summary',description='读取指定日期的真实房间活动采样汇总；未记录到不等于没去过，不能判断食欲或疾病。',
              inputSchema={'type':'object','properties':{'date':{'type':'string','description':'today 或 YYYY-MM-DD'}},'additionalProperties':False}),
         dict(name='board_current_status',description='读取当前板卡状态；离线时明确返回无实时数据。',inputSchema={'type':'object','properties':{},'additionalProperties':False}),
         dict(name='board_event_context',description='读取事件证据供模型解释和追问。离线提供 E01/E02/E03 合成示例，真实模式没有历史时明确拒绝。',
              inputSchema={'type':'object','properties':{'event_id':{'type':'string','enum':['E01','E02','E03']}},'required':['event_id'],'additionalProperties':False})]


def mcp(live, activity_db=DEFAULT_DB):
    for line in sys.stdin:
        try:
            request=json.loads(line)
            if 'id' not in request:
                continue
            method=request.get('method')
            if method=='initialize':
                result=dict(protocolVersion=request.get('params',{}).get('protocolVersion','2024-11-05'),capabilities={'tools':{}},serverInfo={'name':'board-events','version':'0.1.0'},instructions=GUIDANCE)
            elif method=='tools/list': result={'tools':TOOLS}
            elif method=='tools/call':
                params=request.get('params',{})
                try: result=call_tool(params['name'],params.get('arguments',{}),live,activity_db)
                except Exception as exc: result={'isError':True,'content':[{'type':'text','text':f'读取失败：{type(exc).__name__}。没有可用证据，请勿推测。'}]}
            elif method=='ping': result={}
            else:
                print(json.dumps({'jsonrpc':'2.0','id':request['id'],'error':{'code':-32601,'message':'Method not found'}}),flush=True); continue
            print(json.dumps({'jsonrpc':'2.0','id':request['id'],'result':result},ensure_ascii=False),flush=True)
        except (ValueError,KeyError,TypeError):
            print(json.dumps({'jsonrpc':'2.0','id':None,'error':{'code':-32700,'message':'Invalid request'}}),flush=True)


class Handler(BaseHTTPRequestHandler):
    def reply(self, data, mime='application/json; charset=utf-8', status=200):
        if not isinstance(data,bytes): data=json.dumps(data,ensure_ascii=False).encode()
        self.send_response(status); self.send_header('Content-Type',mime)
        self.send_header('Content-Length',str(len(data))); self.send_header('Cache-Control','no-store')
        self.send_header('X-Content-Type-Options','nosniff'); self.end_headers(); self.wfile.write(data)

    def do_GET(self):
        path=self.path.split('?')[0]
        if path=='/api/scenes': self.reply(SCENES)
        elif path in ['/', '/index.html','/app.js','/style.css']:
            file=ROOT/('index.html' if path=='/' else path[1:])
            mime={'.html':'text/html; charset=utf-8','.js':'text/javascript; charset=utf-8','.css':'text/css; charset=utf-8'}[file.suffix]
            self.reply(file.read_bytes(),mime)
        elif path.startswith('/diagram/') and path.endswith('.png'):
            pose=path[9:-4]
            if pose in ['standing','bending','lying','hidden']: self.reply(diagram_png(pose),'image/png')
            else: self.reply({'error':'Not found'},status=404)
        else: self.reply({'error':'Not found'},status=404)

    def do_POST(self):
        if self.path!='/api/ask': return self.reply({'error':'Not found'},status=404)
        # The offline preview has no external actions or board access.
        if self.headers.get('Origin') not in [None,f'http://127.0.0.1:{self.server.server_port}',f'http://localhost:{self.server.server_port}']:
            return self.reply({'error':'Origin rejected'},status=403)
        try:
            size=int(self.headers.get('Content-Length','0'))
            if size<1 or size>4096: raise ValueError('请求过大或为空')
            args=json.loads(self.rfile.read(size))
            self.reply(example_answer(args['event_id'],args['question']))
        except (ValueError,KeyError,StopIteration,TypeError,AttributeError): self.reply({'error':'问题或事件编号无效'},status=400)


if __name__=='__main__':
    sys.stdin.reconfigure(encoding='utf-8')
    sys.stdout.reconfigure(encoding='utf-8')
    parser=argparse.ArgumentParser()
    parser.add_argument('--mcp',action='store_true')
    parser.add_argument('--live',action='store_true')
    parser.add_argument('--activity-db',type=Path,default=DEFAULT_DB)
    parser.add_argument('--port',type=int,default=8098)
    args=parser.parse_args()
    if args.mcp: mcp(args.live, args.activity_db)
    else:
        if args.live: parser.error('--live 目前只用于 MCP 实时状态工具')
        print(f'离线演示：http://127.0.0.1:{args.port}（模拟数据 / 预设回答）',flush=True)
        ThreadingHTTPServer(('127.0.0.1',args.port),Handler).serve_forever()
