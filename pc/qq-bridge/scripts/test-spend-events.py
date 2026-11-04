import json
import pathlib
import runpy

collect = runpy.run_path(str(pathlib.Path(__file__).with_name('read-spend-events.py')))['collect']

def event(kind, **data):
    return {'type': kind, 'time': 1000, 'data': data}

rows = [event('turn/start', turn=1),
        event('assistant/attempt', turn=1, step=1, usage={'inputTokens': 5}),
        event('assistant/message', turn=1, step=1, usage={'inputTokens': 10}),
        event('tool/call', turn=1, step=1, callId='c', name='compress', arguments={'secret': 'DO_NOT_EXPORT'}),
        event('tool/result', turn=1, step=1, message={'toolCallId': 'c', 'isError': True}),
        event('llm/retry-started', turn=1, step=1, retry=1),
        event('assistant/message', turn=1, step=1, usage={'inputTokens': 20})]
result = collect(rows, 's')
assert len(result['requests']) == 2, 'attempt replacement is not billed twice; real retry is separate'
assert sum(r['usage']['inputTokens'] for r in result['requests']) == 30
assert len(result['failures']) == 1
assert result['activeTurn'] == 1
assert 'DO_NOT_EXPORT' not in json.dumps(result)
assert collect(rows + [event('turn/end', turn=1)], 's')['activeTurn'] is None
print('usage dedup, real retries, error correlation, completed turn and secret exclusion: OK')
