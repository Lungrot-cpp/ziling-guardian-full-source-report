import json
import subprocess
import sys
import unittest
from unittest.mock import patch
import assistant as a


class AssistantTests(unittest.TestCase):
    def test_offline_never_invents_temperature(self):
        self.assertIn('没有实测温湿度',a.example_answer('E01','温度多少')['answer'])
        self.assertIn('无法判断',a.example_answer('E03','现在起来了吗')['answer'])
        self.assertIn('已经站立',a.example_answer('E02','现在起来了吗')['answer'])

    def test_unknown_and_input(self):
        self.assertIn('没有调用语言模型',a.example_answer('E01','帮我唱歌')['answer'])
        for q in ['', 'a'*501]:
            with self.assertRaises(ValueError): a.example_answer('E01',q)
        with self.assertRaises(StopIteration): a.scene_by_id('../../config')

    def test_default_answers_are_short(self):
        for event in ['E01','E02','E03']:
            for question in ['发生了什么','现在起来了吗','会不会捡东西','温度多少']:
                self.assertLessEqual(len(a.example_answer(event,question)['answer']),60)

    def test_evidence_has_images_and_no_scripted_answer(self):
        result=a.call_tool('board_event_context',{'event_id':'E03'})
        self.assertEqual(sum(c['type']=='image' for c in result['content']),4)
        text=result['content'][0]['text']
        self.assertIn('合成',text)
        self.assertNotIn('explanation',text)
        for pose in ['standing','lying','hidden','bending']:
            self.assertTrue(a.diagram_png(pose).startswith(b'\x89PNG'))

    def test_live_does_not_fallback(self):
        with patch.object(a,'board_json',side_effect=TimeoutError):
            with self.assertRaises(TimeoutError): a.call_tool('board_current_status',{},True)
        with self.assertRaises(ValueError): a.call_tool('board_event_context',{'event_id':'E01'},True)

    def test_mcp_wire(self):
        requests=[{'jsonrpc':'2.0','id':1,'method':'initialize','params':{'protocolVersion':'2024-11-05'}},
                  {'jsonrpc':'2.0','method':'notifications/initialized'},
                  {'jsonrpc':'2.0','id':2,'method':'tools/list'},
                  {'jsonrpc':'2.0','id':3,'method':'tools/call','params':{'name':'board_current_status'}},
                  {'jsonrpc':'2.0','id':4,'method':'tools/call','params':{'name':'board_event_context','arguments':{'event_id':'missing'}}}]
        p=subprocess.run([sys.executable,str(a.ROOT/'assistant.py'),'--mcp'],input='\n'.join(map(json.dumps,requests))+'\n',encoding='utf-8',capture_output=True,timeout=10,check=True)
        rows=[json.loads(line) for line in p.stdout.splitlines()]
        self.assertEqual([r['id'] for r in rows],[1,2,3,4])
        self.assertEqual(len(rows[1]['result']['tools']),3)
        self.assertIn('离线模拟',rows[2]['result']['content'][0]['text'])
        self.assertTrue(rows[3]['result']['isError'])


if __name__=='__main__': unittest.main()
