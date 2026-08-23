#!/usr/bin/env python3
# Cardputer 行情屏预览代理服务（仅供学习参考）
# 本地代理，解决 CORS + 编码转换
#
# ⚠️ 数据源需自行填写，本脚本不内置任何数据源地址。
#    下方三个 URL 模板请替换为你自己的数据源，
#    数据源选择与合规责任由使用者自行承担。
import http.server
import urllib.request
import urllib.parse
import json
import os

PORT = 8600
BASE = os.path.dirname(os.path.abspath(__file__))

# ===== 数据源配置（用户自行填写，勿内置任何地址）=====
QUOTE_URL  = ""   # 行情快照：末尾拼 "," 分隔的代码列表，返回 GBK 文本
KLINE_URL  = ""   # 日K线：末尾拼 "<代码>,day,,,120,qfq"，返回 JSON
MINUTE_URL = ""   # 分时：末尾拼 6 位代码，返回 JSON
QUOTE_HEADERS = {}   # 行情请求可能需要的请求头（如 Referer）
KLINE_HEADERS = {'User-Agent': 'Mozilla/5.0'}
MINUTE_HEADERS = {'User-Agent': 'Mozilla/5.0'}


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        try:
            parsed = urllib.parse.urlparse(self.path)
            path = parsed.path
            qs = urllib.parse.parse_qs(parsed.query)
            if path == '/':
                self._serve_file('cardputer_preview.html', 'text/html')
            elif path == '/quote':
                codes = qs.get('codes', [''])[0]
                url = QUOTE_URL + codes
                req = urllib.request.Request(url, headers=QUOTE_HEADERS)
                data = urllib.request.urlopen(req, timeout=10).read()
                text = data.decode('gbk', errors='replace')
                self._json({'ok': True, 'text': text})
            elif path == '/kline':
                code = qs.get('code', [''])[0]
                url = KLINE_URL + ('%s,day,,,120,qfq' % code)
                req = urllib.request.Request(url, headers=KLINE_HEADERS)
                data = urllib.request.urlopen(req, timeout=10).read()
                self._json({'ok': True, 'raw': data.decode('utf-8', errors='replace')})
            elif path == '/minute':
                code = qs.get('code', [''])[0]
                url = MINUTE_URL + code
                req = urllib.request.Request(url, headers=MINUTE_HEADERS)
                data = urllib.request.urlopen(req, timeout=10).read()
                self._json({'ok': True, 'raw': data.decode('utf-8', errors='replace')})
            else:
                self._json({'ok': False, 'error': 'not found'}, 404)
        except Exception as e:
            self._json({'ok': False, 'error': str(e)}, 500)

    def _serve_file(self, name, ctype):
        p = os.path.join(BASE, name)
        if os.path.exists(p):
            with open(p, 'rb') as f:
                body = f.read()
            self.send_response(200)
            self.send_header('Content-Type', ctype + '; charset=utf-8')
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        else:
            self._json({'ok': False, 'error': 'file not found: ' + name}, 404)

    def _json(self, obj, code=200):
        body = json.dumps(obj, ensure_ascii=False).encode('utf-8')
        self.send_response(code)
        self.send_header('Content-Type', 'application/json; charset=utf-8')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *args):
        pass


if __name__ == '__main__':
    print('Cardputer preview at http://127.0.0.1:%d' % PORT)
    http.server.ThreadingHTTPServer(('127.0.0.1', PORT), Handler).serve_forever()
