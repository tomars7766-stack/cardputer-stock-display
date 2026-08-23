# Cardputer 行情展示屏 / Cardputer Market Display

**仅供学习参考 / For learning and reference only**

一个跑在 M5Stack Cardputer 上的 A 股指数行情展示框架。放工位上，低调得像个计算器，实时看大盘涨跌。

A stock-market display framework for the M5Stack Cardputer. Looks like a calculator on your desk, shows live index quotes quietly.

---

## ⚠️ 免责声明 / Disclaimer

- 本项目**仅供学习参考**，不构成任何投资建议。
- 本项目**不内置、不推荐、不指定**任何行情数据源。数据源的接入与合规责任由使用者自行承担。
- This project is **for learning and reference only** and does **not** constitute investment advice.
- This project does **not** bundle, recommend, or designate any market data source. You are solely responsible for the data source you connect and its compliance.

---

## ✨ 功能 / Features

- 报价：现价 + 涨跌幅 + 买五卖五（指数显示 Index）
- 分时：价格线 + 均价线 + 量柱
- 日 K 线：K 线 + MACD + KDJ，可缩放（120/60/30/15 根）
- 红涨绿跌（A 股习惯）
- 8 个 A 股指数自动轮播 / 手动切换
- 手动输入 6 位代码查询任意标的（自动判断沪深北交所）
- WiFiManager 配网（首次/换地方手机配网，长按 0 键重置）

- Quote: price + change% + Level-5 order book
- Minute chart: price line + average line + volume bars
- Daily K-line: candles + MACD + KDJ, zoomable (120/60/30/15 bars)
- Red-up / green-down (A-share convention)
- 8 A-share indices with auto-rotate / manual switching
- Manual 6-digit code lookup (auto-detect SH/SZ/BJ)
- WiFiManager provisioning (long-press 0 to reset)

---

## 🧰 硬件 / Hardware

只需一样东西 / Only one thing:

- **M5Stack Cardputer-Adv**（ESP32-S3，自带屏幕 + 键盘 + WiFi + 耳机孔 + 电池，约 200 元）
- **M5Stack Cardputer-Adv** (ESP32-S3, built-in screen + keyboard + WiFi + headphone jack + battery)

---

## 📦 构建 / Build

依赖库 / Libraries:
- M5Cardputer, M5GFX, M5Unified
- WiFiManager
- ArduinoJson

```bash
arduino-cli compile \
  --fqbn "m5stack:esp32:m5stack_cardputer:FlashSize=8M,PartitionScheme=default_8MB" \
  cardputer_stock.ino
```

板卡包 URL / Board manager URL:
`https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/arduino/package_m5stack_index.json`

---

## 🔌 数据源接入 / Connect Your Own Data Source

本项目是"展示架构"，代码里**没有内置任何行情数据源**。你需要打开 `cardputer_stock.ino`，在文件开头的"数据源配置"区块填写三个地址：

This project is a display framework with **no built-in data source**. Open `cardputer_stock.ino` and fill in three URLs in the "Data Source Config" section near the top:

```cpp
const char* QUOTE_URL  = "";   // 行情快照接口 / quote snapshot endpoint
const char* KLINE_URL  = "";   // 日K线接口 / daily K-line endpoint
const char* MINUTE_URL = "";   // 分时接口 / minute chart endpoint
```

### 三个接口的返回格式约定 / Expected response formats

**1. `QUOTE_URL` — 行情快照 / Quote snapshot**

- HTTP GET。多个标的用 `,` 拼接在 URL 末尾。
- HTTP GET. Multiple symbols are joined with `,` at the end of the URL.
- 每个标的返回一段用 `~` 分隔的字段串，关键字段位置如下（0 起）：
- Each symbol returns a `~`-separated field string. Key field positions (0-based):

| 位置 Pos | 含义 Meaning |
|----------|--------------|
| 3 | 现价 price |
| 4 | 昨收 prev close |
| 5 | 今开 open |
| 6 | 成交量 volume |
| 9+2k, 10+2k | 买价/买量 buy price/vol（k=0..4）|
| 19+2k, 20+2k | 卖价/卖量 sell price/vol（k=0..4）|
| 30 | 时间戳 timestamp（14 位）|
| 32 | 涨跌幅 change% |
| 33 | 最高 high |
| 34 | 最低 low |
| 37 | 成交额 amount |
| 38 | 换手率 turnover |
| 39 | 市盈率 PE |
| 43 | 振幅 amplitude |
| 45 | 市值 market cap |
| 46 | 量比 volume ratio |

**2. `KLINE_URL` — 日 K 线 / Daily K-line**

- HTTP GET。URL 末尾拼接 `<代码>,day,,,120,qfq`。
- HTTP GET. Append `<code>,day,,,120,qfq` to the URL.
- 返回 JSON，结构：`data[<code>]["qfqday"]`（或 `["day"]`），每根为 `[日期, 开, 收, 高, 低, 量]`。
- Returns JSON: `data[<code>]["qfqday"]` (or `["day"]`), each bar is `[date, open, close, high, low, volume]`.

**3. `MINUTE_URL` — 分时 / Minute chart**

- HTTP GET。URL 末尾拼接 6 位代码。
- HTTP GET. Append the 6-digit code to the URL.
- 返回 JSON，结构：`data[<code>]["data"]["data"]` 数组，每项为字符串 `"时间 价格 累计量 累计额"`。
- Returns JSON: `data[<code>]["data"]["data"]` array, each item is a string `"time price cumVolume cumAmount"`.

> 如果你的数据源返回格式不同，请相应调整 `parseQuote()`、`fetchKline()`、`fetchMinute()` 里的解析逻辑。
> If your data source returns a different format, adjust the parsing in `parseQuote()`, `fetchKline()`, and `fetchMinute()` accordingly.

---

## 🔑 按键 / Keymap

| 按键 Key | 功能 Function |
|----------|---------------|
| w / s | 切视图 / switch view（报价→分时→K线）|
| a / d | 切标的 / switch symbol |
| 1–9 | 跳到第 N 个标的 / jump to symbol N |
| 空格 Space | 锁定/恢复自动轮播 / lock/unlock auto-rotate |
| - / = | 轮播间隔减/增 / rotate interval |
| [ / ] | K 线缩放 / K-line zoom |
| , / . | 亮度 / brightness |
| e | 手动输入代码 / manual code input |
| 长按 0 Hold 0 | 重置 WiFi / reset WiFi |

---

## 📄 License

[MIT](./LICENSE)
