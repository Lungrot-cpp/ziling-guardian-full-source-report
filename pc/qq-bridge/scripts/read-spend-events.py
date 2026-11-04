"""Read QQ session usage and tool names only; never export chat text or arguments."""
import io
import json
import pathlib
import sys
import zstandard


def collect(rows, sid):
    requests, calls, failures, ended = {}, {}, [], set()
    model, provider, retry = None, None, {}
    last_turn, last_time = None, 0
    for event in rows:
        data = event.get('data', {})
        kind, turn, step = event.get('type'), data.get('turn'), data.get('step')
        stamp = event.get('time', 0)
        last_time = max(last_time, stamp)
        if kind == 'model/selection':
            model, provider = data.get('model'), data.get('provider')
        if kind == 'turn/start':
            last_turn = turn
        if kind == 'turn/end':
            ended.add(turn)
        if kind == 'llm/retry-started':
            retry[turn, step] = data.get('retry', 1)
        if kind == 'tool/call':
            calls[data['callId']] = (turn, step, data.get('name', 'unknown'))
        if kind == 'tool/result':
            msg = data.get('message', {})
            call = calls.get(msg.get('toolCallId') or msg.get('source', {}).get('callId'))
            if call and msg.get('isError'):
                failures.append({'sessionId': sid, 'turn': call[0], 'tool': call[2], 'time': stamp})
        if kind not in ('assistant/message', 'assistant/attempt'):
            continue
        usage = data.get('usage')
        if usage is None:
            usage = next((c['usage'] for c in reversed(data.get('stream', [])) if 'usage' in c), None)
        if usage is None:
            continue
        source = data.get('message', {}).get('source', {})
        key = (turn, step, retry.get((turn, step), 0))
        requests[key] = {'sessionId': sid, 'turn': turn, 'step': step, 'retry': key[2],
                         'time': stamp, 'model': source.get('model') or model,
                         'provider': source.get('provider') or provider, 'usage': usage}
    for request in requests.values():
        request['tools'] = sorted({name for t, s, name in calls.values()
                                   if (t, s) == (request['turn'], request['step'])})
    return {'requests': list(requests.values()), 'failures': failures,
            'activeTurn': last_turn if last_turn not in ended else None, 'lastTime': last_time}


if __name__ == '__main__':
    result = {'sessions': [], 'incomplete': []}
    for filename in pathlib.Path(sys.argv[1]).glob('*/session*.jsonl.zstd'):
        rows = []
        try:
            with filename.open('rb') as source:
                with zstandard.ZstdDecompressor().stream_reader(source, read_across_frames=True) as stream:
                    for line in io.TextIOWrapper(stream, encoding='utf8'):
                        if line.strip():
                            rows.append(json.loads(line))
        except (zstandard.ZstdError, json.JSONDecodeError, OSError):
            result['incomplete'].append(filename.parent.name)
        if rows:
            result['sessions'].append(collect(rows, filename.parent.name))
    print(json.dumps(result, ensure_ascii=True))
