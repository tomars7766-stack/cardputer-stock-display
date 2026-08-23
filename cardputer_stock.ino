/*
 * Cardputer 行情展示屏（仅供学习参考）
 * 标的：8大A股指数（用户可自行增删）
 * 视图：报价 / 分时 / 日K线
 * 数据源：由用户自行配置（见下方"数据源配置"区块，本代码不含任何内置数据源地址）
 * WiFi：WiFiManager 配网（首次/换地方手机配网，长按 0 键重置）
 * 颜色：红涨绿跌（A股习惯）
 *
 * 按键（正常模式）：
 *   w/s      切换视图（报价→盘口→分时→K线→量价）
 *   a/d      切换标的（上一个/下一个）
 *   1-9      直接跳到第 N 个标的
 *   空格     锁定/恢复自动轮播
 *   - / =    轮播间隔 减/加（3/5/8/12/20/30 秒）
 *   e        手动输入个股代码查询
 *   长按 0   重置 WiFi 重新配网
 *
 * 按键（输入模式，按 e 进入）：
 *   0-9      输入代码（6 位，满 6 位自动查询）
 *   x        删除最后一位
 *   e        退出输入模式
 */

#include <M5Cardputer.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <time.h>

#define AP_NAME "Cardputer-Stock"

// ============================================================
// ===== 数据源配置（用户自行填写，本代码不含任何内置地址）=====
// ============================================================
// 本代码只提供"展示架构"，不内置任何行情数据源。
// 请将下方三个地址替换为你自己的数据源，并确保返回格式符合
// 下方各 fetch 函数注释里的约定。
//
// 1. QUOTE_URL  — 行情快照接口
//    GET 请求，返回文本。约定：多个标的用 "," 分隔拼接在 URL 末尾；
//    每个标的返回一段用 '~' 分隔的字段串，字段含义见 parseQuote()。
//
// 2. KLINE_URL  — 日K线接口
//    GET 请求，返回 JSON。约定：URL 末尾拼 "<代码>,day,,,120,qfq"，
//    返回结构见 fetchKline()。
//
// 3. MINUTE_URL — 分时接口
//    GET 请求，返回 JSON。约定：URL 末尾拼 6 位代码，
//    返回结构见 fetchMinute()。
//
// 说明：数据源选择由使用者自行决定并承担合规责任，
//       本代码仅供学习参考，不推荐、不指定任何具体数据源。
// ============================================================
const char* QUOTE_URL  = "";   // 行情快照接口地址（自行填写）
const char* KLINE_URL  = "";   // 日K线接口地址（自行填写）
const char* MINUTE_URL = "";   // 分时接口地址（自行填写）

// ===== 标的（8 个 A 股指数，可自行增删）=====
const int TICKER_COUNT = 8;
const char* NAMES[] = {
  "SH Index",
  "SZ Component",
  "ChiNext",
  "STAR 50",
  "CSI 300",
  "CSI 500",
  "CSI 1000",
  "SSE 50"
};
const char* CODES[] = {
  "sh000001",
  "sz399001",
  "sz399006",
  "sh000688",
  "sh000300",
  "sh000905",
  "sh000852",
  "sh000016"
};
const bool IS_INDEX[TICKER_COUNT] = {
  true, true, true, true, true, true, true, true
};

// ===== 行情数据结构 =====
struct Quote {
  float price = 0;
  float prevClose = 0;
  float open = 0;
  float high = 0;
  float low = 0;
  float pct = 0;
  long  volume = 0;
  float amount = 0;
  float turnover = 0;
  float volRatio = 0;
  float amplitude = 0;
  float pe = 0;
  float mcap = 0;
  float buyPrice[5];
  float buyVol[5];
  float sellPrice[5];
  float sellVol[5];
};
Quote quotes[TICKER_COUNT + 1];  // 8 固定 + 1 临时查询
String LAST_UPDATE = "--:--:--";

// ===== 临时查询标的 =====
String customCode = "";   // 如 "sh000001"
bool  customValid = false;

// ===== 日K线 =====
const int KLEN = 120;
float kOpen[KLEN], kClose[KLEN], kHigh[KLEN], kLow[KLEN];
long  kVol[KLEN];
int   kCount = 0;
int   kTicker = -1;
float macdDif[KLEN], macdDea[KLEN], macdBar[KLEN];
float kdjK[KLEN], kdjD[KLEN], kdjJ[KLEN];
int kZoomIdx = 1;
const int kZoomLevels[] = {120, 60, 30, 15};
const int KZOOM_N = 4;
int kStart = 0, kShow = 60;
int brightness = 128;
int batLevel = -1;
unsigned long lastBatRead = 0;

// ===== 分时线 =====
const int MLEN = 250;
float mPrice[MLEN];
float mAvg[MLEN];
float mVol[MLEN];
int   mCount = 0;
int   mTicker = -1;

// ===== 视图/交互状态 =====
enum View { VIEW_QUOTE, VIEW_MINUTE, VIEW_KLINE, VIEW_COUNT };
int  curView = VIEW_QUOTE;
int  curTicker = 0;
bool autoRotate = true;
int  rotateIdx = 1;
const int rotateIntervals[] = {3, 5, 8, 12, 20, 30};
const int ROTATE_N = 6;
unsigned long lastRotate = 0;
unsigned long lastFetch = 0;

// ===== 输入模式 =====
bool   inputMode = false;
String inputBuf = "";

WiFiManager wm;
LGFX_Sprite screen(&M5Cardputer.Display);

// ===== 标的代码/名称（支持临时查询）=====
String tickerCode(int idx) {
  if (idx == TICKER_COUNT) return customCode;
  return CODES[idx];
}
String tickerName(int idx) {
  if (idx == TICKER_COUNT) return customCode;
  return NAMES[idx];
}
bool tickerIsIndex(int idx) {
  if (idx == TICKER_COUNT) return false;
  return IS_INDEX[idx];
}

// ===== 代码前缀判断（6位→市场前缀）=====
String addPrefix(const String& c6) {
  if (c6.length() < 1) return "sh" + c6;
  char c = c6.charAt(0);
  if (c == '6' || c == '9') return "sh" + c6;
  if (c == '0' || c == '3') return "sz" + c6;
  if (c == '8' || c == '4') return "bj" + c6;
  return "sh" + c6;
}

// ===== 解析行情 =====
void parseQuote(int idx, const String& seg) {
  float f[50];
  for (int i = 0; i < 50; i++) f[i] = 0;
  String ts = "";
  int from = 0, j = 0;
  while (j < 50) {
    int to = seg.indexOf('~', from);
    if (to < 0) to = seg.length();
    String field = seg.substring(from, to);
    f[j] = field.toFloat();
    if (j == 30) ts = field;
    j++;
    from = to + 1;
    if (from >= seg.length()) break;
  }
  if (ts.length() == 14) {
    LAST_UPDATE = ts.substring(8, 10) + ":" + ts.substring(10, 12) + ":" + ts.substring(12, 14);
  }
  Quote& q = quotes[idx];
  q.price = f[3];
  q.prevClose = f[4];
  q.open = f[5];
  q.volume = (long)f[6];
  q.pct = f[32];
  q.high = f[33];
  q.low = f[34];
  q.amount = f[37];
  q.turnover = f[38];
  q.pe = f[39];
  q.amplitude = f[43];
  q.mcap = f[45];
  q.volRatio = f[46];
  for (int k = 0; k < 5; k++) {
    q.buyPrice[k] = f[9 + k * 2];
    q.buyVol[k] = f[10 + k * 2];
    q.sellPrice[k] = f[19 + k * 2];
    q.sellVol[k] = f[20 + k * 2];
  }
}

// ===== 拉取单个标的行情（临时查询用）=====
bool fetchOne(int slot, const String& code) {
  String url = String(QUOTE_URL) + code;
  HTTPClient http;
  http.setTimeout(8000);
  http.begin(url);
  int httpCode = http.GET();
  bool ok = false;
  if (httpCode == 200) {
    String payload = http.getString();
    String key = "v_" + code + "=";
    int s = payload.indexOf(key);
    if (s >= 0) {
      s += key.length() + 1;
      int e = payload.indexOf('"', s);
      if (e >= 0) {
        String seg = payload.substring(s, e);
        // 校验代码存在（seg 非空且第2字段非空）
        if (seg.length() > 3 && seg.indexOf("~") >= 0) {
          parseQuote(slot, seg);
          ok = true;
        }
      }
    }
  }
  http.end();
  return ok;
}

// ===== 拉取全部固定标的行情 =====
void fetchQuotes() {
  String url = QUOTE_URL;
  for (int i = 0; i < TICKER_COUNT; i++) {
    if (i > 0) url += ",";
    url += CODES[i];
  }
  HTTPClient http;
  http.setTimeout(8000);
  http.begin(url);
  int code = http.GET();
  if (code == 200) {
    String payload = http.getString();
    for (int i = 0; i < TICKER_COUNT; i++) {
      String key = "v_" + String(CODES[i]) + "=";
      int s = payload.indexOf(key);
      if (s < 0) continue;
      s += key.length() + 1;
      int e = payload.indexOf('"', s);
      if (e < 0) continue;
      parseQuote(i, payload.substring(s, e));
    }
    int t = payload.lastIndexOf('~');
    if (t >= 0 && payload.length() - t >= 15) {
      String ts = payload.substring(t + 1, t + 15);
      if (ts.length() == 14) {
        LAST_UPDATE = ts.substring(8, 10) + ":" + ts.substring(10, 12) + ":" + ts.substring(12, 14);
      }
    }
  }
  http.end();
}

// ===== 拉取日K线 =====
void fetchKline(int idx) {
  String url = String(KLINE_URL) + tickerCode(idx) + ",day,,,120,qfq";
  HTTPClient http;
  http.setTimeout(9000);
  http.setUserAgent("Mozilla/5.0");
  http.begin(url);
  int code = http.GET();
  if (code == 200) {
    String payload = http.getString();
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (!err) {
      JsonVariant data = doc["data"][tickerCode(idx)];
      JsonArray arr;
      if (data["qfqday"].is<JsonArray>()) arr = data["qfqday"];
      else if (data["day"].is<JsonArray>()) arr = data["day"];
      kCount = 0;
      for (JsonVariant v : arr) {
        if (kCount >= KLEN) break;
        kOpen[kCount] = v[1].as<float>();
        kClose[kCount] = v[2].as<float>();
        kHigh[kCount] = v[3].as<float>();
        kLow[kCount] = v[4].as<float>();
        kVol[kCount] = (long)v[5].as<float>();
        kCount++;
      }
      kTicker = idx;
    }
  }
  http.end();
}

// ===== 拉取分时线 =====
void fetchMinute(int idx) {
  String url = String(MINUTE_URL) + tickerCode(idx);
  HTTPClient http;
  http.setTimeout(9000);
  http.setUserAgent("Mozilla/5.0");
  http.begin(url);
  int code = http.GET();
  if (code == 200) {
    String payload = http.getString();
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (!err) {
      JsonArray arr = doc["data"][tickerCode(idx)]["data"]["data"];
      mCount = 0;
      float prevCum = 0;
      for (JsonVariant v : arr) {
        if (mCount >= MLEN) break;
        String s = v.as<String>();
        int sp1 = s.indexOf(' ');
        int sp2 = s.indexOf(' ', sp1 + 1);
        int sp3 = s.indexOf(' ', sp2 + 1);
        if (sp1 > 0 && sp2 > 0) {
          mPrice[mCount] = s.substring(sp1 + 1, sp2).toFloat();
          float cumVol = s.substring(sp2 + 1, sp3).toFloat();
          float cumAmt = s.substring(sp3 + 1).toFloat();
          mAvg[mCount] = (cumVol > 0) ? (cumAmt / (cumVol * 100.0)) : mPrice[mCount];
          mVol[mCount] = cumVol - prevCum;
          prevCum = cumVol;
          mCount++;
        }
      }
      mTicker = idx;
    }
  }
  http.end();
}

// ===== 状态栏 =====
void drawStatusBar() {
  auto& d = screen;
  d.setTextSize(1);
  d.setTextColor(TFT_DARKGREY);
  d.setCursor(2, 126);
  String rot = autoRotate ? ("Auto " + String(rotateIntervals[rotateIdx]) + "s") : "Manual";
  d.print(rot);
  struct tm ti;
  d.setCursor(138, 126);
  if (getLocalTime(&ti)) {
    char buf[12];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", ti.tm_hour, ti.tm_min, ti.tm_sec);
    d.print(buf);
  } else {
    d.print("--:--:--");
  }
  d.setCursor(200, 126);
  if (batLevel < 0) {
    d.print("USB");
  } else {
    d.printf("%d%%", batLevel);
  }
}

// ===== 视图1：报价 =====
void drawQuote() {
  auto& d = screen;
  d.fillScreen(TFT_BLACK);
  Quote& q = quotes[curTicker];
  uint16_t c = (q.pct >= 0) ? TFT_RED : TFT_GREEN;

  d.setTextSize(1);
  d.setTextColor(TFT_WHITE);
  d.setCursor(2, 2);
  d.print(tickerName(curTicker));
  d.setTextColor(TFT_DARKGREY);
  d.setCursor(80, 2);
  d.print(tickerCode(curTicker));

  d.setTextSize(3);
  d.setTextColor(TFT_WHITE);
  d.setCursor(2, 16);
  d.printf("%.2f", q.price);

  d.setTextSize(2);
  d.setTextColor(c);
  d.setCursor(2, 46);
  d.printf("%+.2f%%", q.pct);

  d.setTextSize(1);
  d.setTextColor(TFT_LIGHTGREY);
  d.setCursor(2, 64);
  d.printf("O %.2f H %.2f", q.open, q.high);
  d.setCursor(2, 78);
  d.printf("L %.2f P %.2f", q.low, q.prevClose);

  d.setTextColor(TFT_WHITE);
  d.setCursor(2, 92);
  d.printf("Amt %.1f亿", q.amount / 10000.0);
  d.setCursor(2, 104);
  d.printf("Tr %.2f%% VR %.2f", q.turnover, q.volRatio);
  d.setCursor(2, 116);
  d.printf("Amp %.2f%%", q.amplitude);

  // 右边：买五卖五
  if (tickerIsIndex(curTicker)) {
    d.setTextColor(TFT_YELLOW);
    d.setCursor(150, 40);
    d.setTextSize(2);
    d.print("Index");
  } else {
    int y = 16;
    d.setTextSize(1);
    for (int i = 4; i >= 0; i--) {
      d.setTextColor(TFT_GREEN);
      d.setCursor(150, y);
      d.printf("%.2f %.0f", q.sellPrice[i], q.sellVol[i]);
      y += 10;
    }
    d.drawFastHLine(148, y, 90, TFT_DARKGREY);
    y += 2;
    for (int i = 0; i < 5; i++) {
      d.setTextColor(TFT_RED);
      d.setCursor(150, y);
      d.printf("%.2f %.0f", q.buyPrice[i], q.buyVol[i]);
      y += 10;
    }
  }

  drawStatusBar();
}

// ===== 视图2：买五卖五盘口 =====

// ===== 视图3：分时 =====
void drawMinute() {
  auto& d = screen;
  d.fillScreen(TFT_BLACK);
  Quote& q = quotes[curTicker];

  d.setTextSize(1);
  d.setTextColor(TFT_WHITE);
  d.setCursor(2, 2);
  d.print(tickerName(curTicker));

  if (mCount == 0 || mTicker != curTicker) {
    d.setTextColor(TFT_YELLOW);
    d.setCursor(2, 50);
    d.setTextSize(2);
    if (mTicker == curTicker) {
      d.print("Min fail");
      d.setTextSize(1);
      d.setTextColor(TFT_DARKGREY);
      d.setCursor(2, 72);
      d.print("tap a/d retry");
    } else {
      d.print("Loading...");
    }
    drawStatusBar();
    return;
  }

  float ref = q.prevClose;
  float minP = ref, maxP = ref;
  for (int i = 0; i < mCount; i++) {
    if (mPrice[i] < minP) minP = mPrice[i];
    if (mPrice[i] > maxP) maxP = mPrice[i];
    if (mAvg[i] < minP) minP = mAvg[i];
    if (mAvg[i] > maxP) maxP = mAvg[i];
  }
  float range = maxP - minP;
  if (range <= 0.0001) range = 0.01;

  int chartY = 10, chartH = 70;
  float stepX = 240.0 / max(1, mCount);

  int yRef = chartY + (maxP - ref) / range * chartH;
  d.drawFastHLine(0, yRef, 240, TFT_DARKGREY);

  for (int i = 1; i < mCount; i++) {
    int x0 = (i - 1) * stepX;
    int y0 = chartY + (maxP - mPrice[i - 1]) / range * chartH;
    int x1 = i * stepX;
    int y1 = chartY + (maxP - mPrice[i]) / range * chartH;
    d.drawLine(x0, y0, x1, y1, TFT_WHITE);
  }
  for (int i = 1; i < mCount; i++) {
    int x0 = (i - 1) * stepX;
    int y0 = chartY + (maxP - mAvg[i - 1]) / range * chartH;
    int x1 = i * stepX;
    int y1 = chartY + (maxP - mAvg[i]) / range * chartH;
    d.drawLine(x0, y0, x1, y1, TFT_YELLOW);
  }

  // 量柱
  float maxV = 0;
  for (int i = 0; i < mCount; i++) if (mVol[i] > maxV) maxV = mVol[i];
  if (maxV <= 0) maxV = 1;
  int vY = 84, vH = 26;
  for (int i = 0; i < mCount; i++) {
    int h = (int)(mVol[i] / maxV * vH);
    int x = i * stepX;
    uint16_t color = (mPrice[i] >= ref) ? TFT_RED : TFT_GREEN;
    d.fillRect(x, vY + vH - h, max(1, (int)stepX - 1), h, color);
  }

  d.setTextSize(1);
  d.setTextColor(TFT_WHITE);
  d.setCursor(2, 116);
  d.printf("%.2f", q.price);
  d.setTextColor((q.pct >= 0) ? TFT_RED : TFT_GREEN);
  d.setCursor(60, 116);
  d.printf("%+.2f%%", q.pct);

  drawStatusBar();
}

// ===== 视图4：日K线 =====
// MACD 计算
void calcMACD() {
  float e12 = kClose[kStart], e26 = kClose[kStart], dv = 0;
  for (int i = 0; i < kShow; i++) {
    int idx = kStart + i;
    e12 = e12 * 11 / 13 + kClose[idx] * 2 / 13;
    e26 = e26 * 25 / 27 + kClose[idx] * 2 / 27;
    float di = e12 - e26;
    macdDif[i] = di;
    dv = dv * 8 / 10 + di * 2 / 10;
    macdDea[i] = dv;
    macdBar[i] = 2 * (di - dv);
  }
}
// KDJ 计算
void calcKDJ() {
  float k = 50, d = 50;
  for (int i = 0; i < kShow; i++) {
    int idx = kStart + i;
    int n = min(9, i + 1);
    float h = kHigh[idx], l = kLow[idx];
    for (int t = idx - n + 1; t <= idx; t++) {
      if (kHigh[t] > h) h = kHigh[t];
      if (kLow[t] < l) l = kLow[t];
    }
    float rsv = (h == l) ? 50 : (kClose[idx] - l) / (h - l) * 100;
    k = k * 2 / 3 + rsv / 3;
    d = d * 2 / 3 + k / 3;
    kdjK[i] = k; kdjD[i] = d; kdjJ[i] = 3 * k - 2 * d;
  }
}

void drawKline() {
  auto& d = screen;
  d.fillScreen(TFT_BLACK);
  Quote& q = quotes[curTicker];

  d.setTextSize(1);
  d.setTextColor(TFT_WHITE);
  d.setCursor(2, 2);
  d.print(tickerName(curTicker));
  d.setTextColor(TFT_WHITE);
  d.setCursor(100, 2);
  d.printf("%.2f", q.price);
  d.setTextColor((q.pct >= 0) ? TFT_RED : TFT_GREEN);
  d.setCursor(160, 2);
  d.printf("%+.1f%%", q.pct);
  d.setTextColor(TFT_DARKGREY);
  d.setCursor(205, 2);
  d.printf("Z%d", kZoomLevels[kZoomIdx]);

  if (kCount == 0 || kTicker != curTicker) {
    d.setTextColor(TFT_YELLOW);
    d.setCursor(2, 50);
    d.setTextSize(2);
    if (kTicker == curTicker) {
      d.print("K data fail");
      d.setTextSize(1);
      d.setTextColor(TFT_DARKGREY);
      d.setCursor(2, 72);
      d.print("tap a/d retry");
    } else {
      d.print("Loading...");
    }
    drawStatusBar();
    return;
  }

  kShow = kZoomLevels[kZoomIdx];
  if (kShow > kCount) kShow = kCount;
  kStart = kCount - kShow;

  float minP = kLow[kStart], maxP = kHigh[kStart];
  for (int i = 0; i < kShow; i++) {
    if (kLow[kStart + i] < minP) minP = kLow[kStart + i];
    if (kHigh[kStart + i] > maxP) maxP = kHigh[kStart + i];
  }
  float range = maxP - minP;
  if (range <= 0.0001) range = 0.01;

  int kw = 240, chartY = 12, chartH = 48;
  float cw = (float)kw / kShow;

  for (int i = 0; i < kShow; i++) {
    int cx = i * cw + cw / 2;
    int idx = kStart + i;
    int yH = chartY + (maxP - kHigh[idx]) / range * chartH;
    int yL = chartY + (maxP - kLow[idx]) / range * chartH;
    int yO = chartY + (maxP - kOpen[idx]) / range * chartH;
    int yC = chartY + (maxP - kClose[idx]) / range * chartH;
    bool up = kClose[idx] >= kOpen[idx];
    uint16_t color = up ? TFT_RED : TFT_GREEN;
    d.drawLine(cx, yH, cx, yL, color);
    int top = min(yO, yC);
    int bot = max(yO, yC);
    int bh = max(1, bot - top);
    int bw = max(1, (int)cw - 1);
    d.fillRect(cx - bw / 2, top, bw, bh, color);
  }

  // MACD
  calcMACD();
  float mdMin = macdDif[0], mdMax = macdDif[0];
  for (int i = 0; i < kShow; i++) {
    if (macdDif[i] < mdMin) mdMin = macdDif[i];
    if (macdDif[i] > mdMax) mdMax = macdDif[i];
    if (macdDea[i] < mdMin) mdMin = macdDea[i];
    if (macdDea[i] > mdMax) mdMax = macdDea[i];
    if (macdBar[i] < mdMin) mdMin = macdBar[i];
    if (macdBar[i] > mdMax) mdMax = macdBar[i];
  }
  if (mdMax - mdMin <= 0.0001) mdMax = mdMin + 0.01;
  int my = 64, mh = 18;
  int zero = my + (mdMax - 0) / (mdMax - mdMin) * mh;
  d.drawFastHLine(0, zero, kw, TFT_DARKGREY);
  for (int i = 0; i < kShow; i++) {
    int x = i * cw;
    int yBar = my + (mdMax - macdBar[i]) / (mdMax - mdMin) * mh;
    uint16_t color = (macdBar[i] >= 0) ? TFT_RED : TFT_GREEN;
    int top = min(zero, yBar);
    int h = max(1, abs(yBar - zero));
    d.fillRect(x, top, max(1, (int)cw - 1), h, color);
  }
  for (int i = 1; i < kShow; i++) {
    d.drawLine((i - 1) * cw, my + (mdMax - macdDif[i - 1]) / (mdMax - mdMin) * mh,
               i * cw, my + (mdMax - macdDif[i]) / (mdMax - mdMin) * mh, TFT_WHITE);
    d.drawLine((i - 1) * cw, my + (mdMax - macdDea[i - 1]) / (mdMax - mdMin) * mh,
               i * cw, my + (mdMax - macdDea[i]) / (mdMax - mdMin) * mh, TFT_YELLOW);
  }

  // KDJ
  calcKDJ();
  int ky = 86, kh = 14;
  for (int i = 1; i < kShow; i++) {
    d.drawLine((i - 1) * cw, ky + (100 - kdjK[i - 1]) / 100 * kh, i * cw, ky + (100 - kdjK[i]) / 100 * kh, TFT_WHITE);
    d.drawLine((i - 1) * cw, ky + (100 - kdjD[i - 1]) / 100 * kh, i * cw, ky + (100 - kdjD[i]) / 100 * kh, TFT_YELLOW);
    d.drawLine((i - 1) * cw, ky + (100 - kdjJ[i - 1]) / 100 * kh, i * cw, ky + (100 - kdjJ[i]) / 100 * kh, TFT_MAGENTA);
  }

  d.setTextColor(TFT_DARKGREY);
  d.setCursor(4, 64);
  d.print("MACD");
  d.setCursor(4, 86);
  d.print("KDJ");

  drawStatusBar();
}


// ===== 视图5：量价 =====

// ===== 统一渲染 =====
void render() {
  switch (curView) {
    case VIEW_QUOTE: drawQuote(); break;
    case VIEW_MINUTE: drawMinute(); break;
    case VIEW_KLINE: drawKline(); break;
  }
  screen.pushSprite(0, 0);
}

// ===== 进入视图时拉取对应数据 =====
void ensureViewData() {
  if (curView == VIEW_KLINE) {
    if (kCount == 0 || kTicker != curTicker) fetchKline(curTicker);
  }
  if (curView == VIEW_MINUTE) {
    if (mCount == 0 || mTicker != curTicker) fetchMinute(curTicker);
  }
}

// ===== 输入模式 UI =====
void drawInputUI() {
  auto& d = screen;
  d.fillScreen(TFT_BLACK);
  d.setTextSize(1);
  d.setTextColor(TFT_WHITE);
  d.setCursor(2, 4);
  d.print("Input stock code:");
  d.setTextSize(3);
  d.setTextColor(TFT_CYAN);
  d.setCursor(2, 40);
  d.print(inputBuf);
  d.print("_");
  d.setTextSize(1);
  d.setTextColor(TFT_DARKGREY);
  d.setCursor(2, 100);
  d.print("6 digits, auto search");
  d.setCursor(2, 114);
  d.print("x=erase  e=exit");
  screen.pushSprite(0, 0);
}

// ===== 查询自定义代码 =====
void queryCustom(const String& c6) {
  String code = addPrefix(c6);
  if (fetchOne(TICKER_COUNT, code)) {
    customCode = code;
    customValid = true;
    curTicker = TICKER_COUNT;
    inputMode = false;
    ensureViewData();
    render();
  } else {
    // 查询失败
    auto& d = screen;
    d.fillScreen(TFT_BLACK);
    d.setTextColor(TFT_RED);
    d.setTextSize(2);
    d.setCursor(2, 40);
    d.print("Not found:");
    d.setTextColor(TFT_WHITE);
    d.setCursor(2, 70);
    d.print(code);
    delay(1500);
    inputBuf = "";
    drawInputUI();
  }
}

// ===== AP 配网提示 =====
void showPortal() {
  auto& d = screen;
  d.fillScreen(TFT_BLACK);
  d.setTextSize(1);
  d.setTextColor(TFT_YELLOW); d.setCursor(4, 4);
  d.println("WiFi Setup Mode");
  d.setTextColor(TFT_WHITE); d.setCursor(4, 20);
  d.println("1.Phone connect WiFi:");
  d.setTextColor(TFT_CYAN); d.setCursor(4, 34);
  d.println(AP_NAME);
  d.setTextColor(TFT_WHITE); d.setCursor(4, 48);
  d.println("2.Open browser:");
  d.setTextColor(TFT_CYAN); d.setCursor(4, 62);
  d.println("http://192.168.4.1");
  d.setTextColor(TFT_WHITE); d.setCursor(4, 76);
  d.println("3.Enter WiFi, save");
  screen.pushSprite(0, 0);
}

// ===== 按键处理 =====
bool keyWasPressed[128] = {false};
bool handleKey(char c) {
  bool pressed = M5Cardputer.Keyboard.isKeyPressed(c);
  bool was = keyWasPressed[(uint8_t)c];
  keyWasPressed[(uint8_t)c] = pressed;
  return pressed && !was;
}

void processInputKeys() {
  // 数字 0-9
  for (int i = 0; i <= 9; i++) {
    if (handleKey('0' + i)) {
      if (inputBuf.length() < 6) {
        inputBuf += (char)('0' + i);
        if (inputBuf.length() == 6) {
          queryCustom(inputBuf);
          return;
        }
        drawInputUI();
      }
    }
  }
  // x 删除
  if (handleKey('x')) {
    if (inputBuf.length() > 0) inputBuf.remove(inputBuf.length() - 1);
    drawInputUI();
  }
  // e 退出
  if (handleKey('e')) {
    inputMode = false;
    render();
  }
}

void processKeys() {
  if (inputMode) {
    processInputKeys();
    return;
  }
  if (handleKey('e')) {
    inputMode = true;
    inputBuf = "";
    drawInputUI();
    return;
  }
  if (handleKey('w')) {
    curView = (curView + VIEW_COUNT - 1) % VIEW_COUNT;
    ensureViewData();
    render();
  }
  if (handleKey('s')) {
    curView = (curView + 1) % VIEW_COUNT;
    ensureViewData();
    render();
  }
  if (handleKey('a')) {
    curTicker = (curTicker + TICKER_COUNT + 1 - 1) % (TICKER_COUNT + 1);
    if (curTicker == TICKER_COUNT && !customValid) curTicker = TICKER_COUNT - 1;
    lastRotate = millis();
    ensureViewData();
    render();
  }
  if (handleKey('d')) {
    curTicker = (curTicker + 1) % (TICKER_COUNT + 1);
    if (curTicker == TICKER_COUNT && !customValid) curTicker = 0;
    lastRotate = millis();
    ensureViewData();
    render();
  }
  for (int i = 1; i <= 9; i++) {
    if (handleKey('0' + i)) {
      curTicker = i - 1;
      lastRotate = millis();
      ensureViewData();
      render();
    }
  }
  if (handleKey(' ')) {
    autoRotate = !autoRotate;
    lastRotate = millis();
    render();
  }
  if (handleKey('-')) {
    rotateIdx = (rotateIdx + ROTATE_N - 1) % ROTATE_N;
    lastRotate = millis();
    render();
  }
  if (handleKey('=')) {
    rotateIdx = (rotateIdx + 1) % ROTATE_N;
    lastRotate = millis();
    render();
  }
  // K线缩放（[缩小看更多根，]放大看更少根）
  if (handleKey('[')) {
    kZoomIdx = (kZoomIdx + KZOOM_N - 1) % KZOOM_N;
    render();
  }
  if (handleKey(']')) {
    kZoomIdx = (kZoomIdx + 1) % KZOOM_N;
    render();
  }
  // 亮度（,调暗 .调亮）
  if (handleKey(',')) {
    brightness -= 24;
    if (brightness < 20) brightness = 20;
    M5Cardputer.Display.setBrightness(brightness);
  }
  if (handleKey('.')) {
    brightness += 24;
    if (brightness > 255) brightness = 255;
    M5Cardputer.Display.setBrightness(brightness);
  }
}

void setup() {
  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);
  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.setBrightness(brightness);
  screen.setColorDepth(16);
  screen.createSprite(240, 135);
  M5Cardputer.Display.fillScreen(TFT_BLACK);
  M5Cardputer.Display.setTextSize(1);
  M5Cardputer.Display.setTextColor(TFT_WHITE);
  M5Cardputer.Display.setCursor(4, 4);
  M5Cardputer.Display.println("Connecting WiFi...");

  wm.setAPCallback([](WiFiManager* m) { showPortal(); });
  wm.setConfigPortalTimeout(180);
  wm.setWiFiAutoReconnect(true);
  bool ok = wm.autoConnect(AP_NAME);

  if (!ok) {
    M5Cardputer.Display.fillScreen(TFT_BLACK);
    M5Cardputer.Display.setTextColor(TFT_WHITE);
    M5Cardputer.Display.setCursor(4, 4);
    M5Cardputer.Display.println("Timeout, restart...");
    delay(2000);
    ESP.restart();
  }

  M5Cardputer.Display.fillScreen(TFT_BLACK);
  M5Cardputer.Display.setTextColor(TFT_GREEN);
  M5Cardputer.Display.setCursor(4, 4);
  M5Cardputer.Display.println("WiFi OK");
  M5Cardputer.Display.setTextColor(TFT_WHITE);
  M5Cardputer.Display.setCursor(4, 18);
  M5Cardputer.Display.println(WiFi.localIP().toString());
  delay(800);

  configTime(8 * 3600, 0, "ntp.aliyun.com", "pool.ntp.org");
  fetchQuotes();
  fetchKline(0);
  render();
}

void loop() {
  M5Cardputer.update();
  processKeys();

  // 长按 0 键 2 秒 → 重置 WiFi
  static unsigned long keyStart = 0;
  static bool keyDown = false;
  if (M5Cardputer.Keyboard.isKeyPressed('0')) {
    if (!keyDown) { keyDown = true; keyStart = millis(); }
    else if (millis() - keyStart > 2000) {
      M5Cardputer.Display.fillScreen(TFT_BLACK);
      M5Cardputer.Display.setTextColor(TFT_YELLOW);
      M5Cardputer.Display.setCursor(4, 4);
      M5Cardputer.Display.println("Reset WiFi...");
      wm.resetSettings();
      delay(1000);
      ESP.restart();
    }
  } else {
    keyDown = false;
  }

  if (inputMode) {
    delay(10);
    return;
  }

  unsigned long now = millis();

  // 每秒刷新状态栏时间
  static unsigned long lastSb = 0;
  if (now - lastSb >= 1000) {
    screen.fillRect(0, 126, 240, 9, TFT_BLACK);
    drawStatusBar();
    screen.pushSprite(0, 0);
    lastSb = now;
  }

  // 每 30 秒读一次电量（平滑，避免跳动）
  if (now - lastBatRead >= 30000) {
    int b = M5Cardputer.Power.getBatteryLevel();
    if (b >= 0) {
      batLevel = (batLevel < 0) ? b : (batLevel + b) / 2;
    }
    lastBatRead = now;
  }

  if (WiFi.status() != WL_CONNECTED) WiFi.reconnect();

  if (now - lastFetch >= 1000) {
    fetchQuotes();
    if (customValid) fetchOne(TICKER_COUNT, customCode);
    lastFetch = now;
    render();
  }

  // 每 5 秒刷新分时（开盘实时；K线日线不自动刷新）
  static unsigned long lastChart = 0;
  if (now - lastChart >= 1000) {
    if (curView == VIEW_MINUTE) fetchMinute(curTicker);
    lastChart = now;
    render();
  }

  if (autoRotate && now - lastRotate >= (unsigned long)rotateIntervals[rotateIdx] * 1000UL) {
    curTicker = (curTicker + 1) % (TICKER_COUNT + 1);
    if (curTicker == TICKER_COUNT && !customValid) curTicker = 0;
    lastRotate = now;
    ensureViewData();
    render();
  }

  delay(10);
}
