#include <WiFi.h>
#include <Audio.h>
#include <LovyanGFX.hpp>
#include <time.h>

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9341 _panel_instance;
  lgfx::Bus_SPI _bus_instance;
public:
  LGFX(void) {
    {
      auto cfg = _bus_instance.config();
      cfg.spi_host = HSPI_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 40000000;
      cfg.freq_read = 16000000;
      cfg.spi_3wire = false;
      cfg.use_lock = true;
      cfg.dma_channel = 1;
      cfg.pin_sclk = 14;
      cfg.pin_mosi = 13;
      cfg.pin_miso = 12;
      cfg.pin_dc = 2;
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }
    {
      auto cfg = _panel_instance.config();
      cfg.pin_cs = 15;
      cfg.pin_rst = -1;
      cfg.pin_busy = -1;
      cfg.memory_width = 240;
      cfg.memory_height = 320;
      cfg.panel_width = 240;
      cfg.panel_height = 320;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits = 1;
      cfg.readable = true;
      cfg.invert = false;
      cfg.rgb_order = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = true;
      _panel_instance.config(cfg);
    }
    setPanel(&_panel_instance);
  }
};

LGFX tft;
Audio audio(true, I2S_DAC_CHANNEL_LEFT_EN);

const char* ssid = "Evgenium";
const char* pass = "5555555555";

struct Station { const char* url; const char* name; float freq; };
Station stations[] = {
  {"http://ep128.hostingradio.ru:8030/ep128",       "EUROPA PLUS", 88.0},
  {"https://dfm.hostingradio.ru/dfm128.mp3",        "DFM",         91.2},
  {"http://nashe1.hostingradio.ru/nashe-128",       "NASHE RADIO", 94.5},
  {"http://dorognoe.hostingradio.ru:8000/dorognoe", "DOROGNOE",    97.7},
  {"http://icecast.vgtrk.cdnvideo.ru/mayakfm_mp3_192kbps", "MAYAK", 101.2},
  {"http://chanson.hostingradio.ru:8041/chanson256.mp3",   "CHANSON", 104.3},
  {"http://retro.hostingradio.ru:8014/retro320.mp3",       "RETRO FM", 107.5},
  {"https://maximum.hostingradio.ru/maximum128.mp3",       "MAXIMUM",  108.0},
};
const int STATION_COUNT = 8;
int currentStation = 0;
int volume = 16;

#define ENC_CLK 22
#define ENC_DT  27
volatile int encDelta = 0;
volatile unsigned long encLastInt = 0;
void IRAM_ATTR encISR() {
  unsigned long now = micros();
  if (now - encLastInt < 3000) return;
  encLastInt = now;
  int clk = digitalRead(ENC_CLK), dt = digitalRead(ENC_DT);
  if (clk == LOW && dt == HIGH) encDelta++;
  else if (clk == LOW && dt == LOW) encDelta--;
}

#define WA_BG       0x1082
#define WA_LCD_BG   0x0080
#define WA_LCD_TXT  0x07E0
#define WA_LCD_DIM  0x02E0
#define WA_BTN      0x4208
#define WA_BTN_HI   0x8C71
#define WA_BTN_LO   0x2104
#define WA_ORANGE   0xFD20
#define WA_WHITE    0xFFFF
#define WA_DIM      0x8410
#define WA_GREEN    0x07E0
#define WA_RED      0xF800
#define WA_BAR_BG   0x2104
#define WA_METAL    0x630C
#define WA_METAL_HI 0xBDF7
#define WA_METAL_LO 0x3186

#define TITLE_H 30
#define LCD_X 6
#define LCD_Y (TITLE_H + 6)
#define LCD_W 308
#define LCD_H 58

#define SPEC_X 6
#define SPEC_Y (LCD_Y + LCD_H + 4)
#define SPEC_W 308
#define SPEC_H 46
#define SPEC_BARS 24
float specVals[SPEC_BARS];
float specPeaks[SPEC_BARS];
int oldBarH[SPEC_BARS];
int oldPeakY[SPEC_BARS];
bool specInited = false;

#define BTN_Y (SPEC_Y + SPEC_H + 6)
#define BTN_H 30
#define BTN_COUNT 4
int btnX[BTN_COUNT] = {6, 84, 162, 240};
int btnW = 74;

#define VOL_X 6
#define VOL_Y (BTN_Y + BTN_H + 6)
#define VOL_W 308
#define VOL_H 22

bool isPlaying = false;
bool isConnecting = false;
unsigned long lastClockUpdate = 0;
unsigned long lastSpecUpdate = 0;
int streamBitrate = 0;
String streamCodec = "";
String streamTitle = "";
int scrollOffset = 0;
unsigned long lastScrollUpdate = 0;
String lastShown = "";

uint16_t titleGradColor(int y) {
  float t = (float)y / TITLE_H;
  return tft.color565(30 + t * 15, 30 + t * 15, 35 + t * 15);
}

void redrawTitleBg(int x, int y, int w, int h) {
  for (int yy = 0; yy < h; yy++)
    tft.drawFastHLine(x, y + yy, w, titleGradColor(y + yy));
}

void drawMetalPanel(int x, int y, int w, int h, int r) {
  tft.fillRoundRect(x, y, w, h, r, WA_METAL);
  tft.drawRoundRect(x, y, w, h, r, WA_METAL_LO);
  tft.drawRoundRect(x + 1, y + 1, w - 2, h - 2, r - 1, WA_METAL_HI);
}

void drawInsetPanel(int x, int y, int w, int h, int r) {
  tft.fillRoundRect(x, y, w, h, r, WA_METAL_LO);
  tft.drawRoundRect(x, y, w, h, r, WA_METAL_HI);
  tft.drawRoundRect(x + 1, y + 1, w - 2, h - 2, r - 1, 0x0000);
}

void drawButton(int x, int y, int w, int h, bool pressed) {
  uint16_t bg = pressed ? WA_BTN_LO : WA_BTN;
  uint16_t hi = pressed ? 0x0000 : WA_BTN_HI;
  uint16_t lo = pressed ? WA_BTN_HI : WA_BTN_LO;
  tft.fillRoundRect(x, y, w, h, 4, bg);
  tft.drawFastHLine(x + 2, y + 1, w - 4, hi);
  tft.drawFastVLine(x + 1, y + 2, h - 4, hi);
  tft.drawFastHLine(x + 2, y + h - 2, w - 4, lo);
  tft.drawFastVLine(x + w - 2, y + 2, h - 4, lo);
  tft.drawRoundRect(x, y, w, h, 4, WA_METAL_LO);
}

void drawPrevIcon(int cx, int cy) {
  tft.fillTriangle(cx + 6, cy - 6, cx + 6, cy + 6, cx - 2, cy, WA_LCD_TXT);
  tft.fillRect(cx - 6, cy - 5, 3, 11, WA_LCD_TXT);
}
void drawNextIcon(int cx, int cy) {
  tft.fillTriangle(cx - 6, cy - 6, cx - 6, cy + 6, cx + 2, cy, WA_LCD_TXT);
  tft.fillRect(cx + 3, cy - 5, 3, 11, WA_LCD_TXT);
}
void drawPlayIcon(int cx, int cy) {
  tft.fillTriangle(cx - 4, cy - 7, cx - 4, cy + 7, cx + 6, cy, WA_LCD_TXT);
}
void drawStopIcon(int cx, int cy) {
  tft.fillRect(cx - 5, cy - 5, 10, 10, WA_LCD_TXT);
}

void drawTitleBar() {
  for (int y = 0; y < TITLE_H; y++)
    tft.drawFastHLine(0, y, 320, titleGradColor(y));
  tft.drawFastHLine(0, TITLE_H - 1, 320, WA_METAL_LO);
  tft.drawFastHLine(0, TITLE_H, 320, 0x0000);
  tft.setTextColor(WA_ORANGE, titleGradColor(8));
  tft.setTextSize(2);
  tft.setCursor(8, 8);
  tft.print("WINAMP");
  tft.setTextColor(WA_LCD_DIM, titleGradColor(8));
  tft.setTextSize(1);
  tft.setCursor(84, 9);
  tft.print("RADIO");
}

void drawClock() {
  time_t now = time(nullptr);
  struct tm* t = localtime(&now);
  bool synced = (t->tm_year > 100);
  redrawTitleBg(128, 4, 72, 22);
  tft.setTextColor(synced ? WA_LCD_TXT : WA_LCD_DIM, titleGradColor(8));
  tft.setTextSize(2);
  if (synced) {
    tft.setCursor(130, 8);
    tft.printf("%02d:%02d", t->tm_hour, t->tm_min);
  } else {
    tft.setCursor(138, 8);
    tft.print("--:--");
  }
}

void drawWiFiIcon() {
  redrawTitleBg(238, 4, 80, 22);
  bool conn = (WiFi.status() == WL_CONNECTED);
  int rssi = conn ? WiFi.RSSI() : -100;
  int activeBars = 0;
  if (conn) {
    if (rssi > -55) activeBars = 4;
    else if (rssi > -65) activeBars = 3;
    else if (rssi > -75) activeBars = 2;
    else activeBars = 1;
  }
  tft.setTextColor(conn ? WA_LCD_DIM : WA_RED, titleGradColor(14));
  tft.setTextSize(1);
  tft.setCursor(240, 16);
  tft.print(conn ? "WiFi" : "----");
  int barsX = 282, barsY = 8, barsW = 3, gap = 2;
  for (int i = 0; i < 4; i++) {
    int h = 3 + i * 4;
    int x = barsX + i * (barsW + gap);
    int y = barsY + (15 - h);
    tft.fillRect(x, y, barsW, h, (i < activeBars) ? WA_GREEN : WA_METAL_LO);
  }
}

void drawLCDBackground() {
  drawInsetPanel(LCD_X, LCD_Y, LCD_W, LCD_H, 3);
  for (int y = 0; y < LCD_H - 4; y++) {
    float t = (float)y / (LCD_H - 4);
    uint16_t c = tft.color565(0, 10 + t * 6, 0);
    tft.drawFastHLine(LCD_X + 2, LCD_Y + 2 + y, LCD_W - 4, c);
  }
}

void drawLCDContent() {
  String line1 = streamTitle.length() > 0 ? streamTitle : stations[currentStation].name;
  line1 += "  ***  ";
  int chW = 12;
  int visibleChars = (LCD_W - 12) / chW;
  String display1 = line1 + line1;
  int startIdx = scrollOffset % line1.length();
  String shown = display1.substring(startIdx, startIdx + visibleChars);
  if ((int)shown.length() < visibleChars)
    shown += line1.substring(0, visibleChars - shown.length());
  if (shown != lastShown) {
    tft.fillRect(LCD_X + 4, LCD_Y + 4, LCD_W - 8, 22, WA_LCD_BG);
    tft.setTextColor(WA_LCD_TXT, WA_LCD_BG);
    tft.setTextSize(2);
    tft.setCursor(LCD_X + 6, LCD_Y + 6);
    tft.print(shown);
    lastShown = shown;
  }
  static int lastStation = -1;
  static int lastBitrate = -1;
  static String lastCodec = "";
  static bool lastPlaying = !isPlaying;
  static bool lastConnecting = !isConnecting;
  bool needRedraw = (currentStation != lastStation ||
                     streamBitrate != lastBitrate ||
                     streamCodec != lastCodec ||
                     isPlaying != lastPlaying ||
                     isConnecting != lastConnecting);
  if (needRedraw) {
    tft.fillRect(LCD_X + 4, LCD_Y + 26, LCD_W - 8, LCD_H - 30, WA_LCD_BG);
    tft.setTextColor(WA_LCD_DIM, WA_LCD_BG);
    tft.setTextSize(1);
    tft.setCursor(LCD_X + 6, LCD_Y + 30);
    tft.printf("%.1f MHz  FM  STEREO", stations[currentStation].freq);
    if (streamBitrate > 0) {
      tft.setTextColor(WA_ORANGE, WA_LCD_BG);
      tft.setCursor(LCD_X + LCD_W - 70, LCD_Y + 30);
      tft.printf("%dk %s", streamBitrate, streamCodec.c_str());
    } else if (isConnecting) {
      tft.setTextColor(WA_ORANGE, WA_LCD_BG);
      tft.setCursor(LCD_X + LCD_W - 70, LCD_Y + 30);
      tft.print("connecting");
    }
    if (isPlaying) {
      tft.setTextColor(WA_GREEN, WA_LCD_BG);
      tft.setCursor(LCD_X + LCD_W - 40, LCD_Y + 42);
      tft.print("ON AIR");
    } else {
      tft.setTextColor(WA_RED, WA_LCD_BG);
      tft.setCursor(LCD_X + LCD_W - 36, LCD_Y + 42);
      tft.print("STOP");
    }
    tft.setTextColor(WA_LCD_DIM, WA_LCD_BG);
    tft.setCursor(LCD_X + 6, LCD_Y + 42);
    tft.printf("[%d/%d]", currentStation + 1, STATION_COUNT);
    lastStation = currentStation;
    lastBitrate = streamBitrate;
    lastCodec = streamCodec;
    lastPlaying = isPlaying;
    lastConnecting = isConnecting;
  }
}

void updateSpectrum() {
  if (millis() - lastSpecUpdate < 80) return;
  lastSpecUpdate = millis();
  int gap = 1;
  int barW = (SPEC_W - 4 - (SPEC_BARS - 1) * gap) / SPEC_BARS;
  int totalW = SPEC_BARS * barW + (SPEC_BARS - 1) * gap;
  int xOff = ((SPEC_W - 4) - totalW) / 2;
  int maxH = SPEC_H - 4;
  if (!specInited) {
    for (int i = 0; i < SPEC_BARS; i++) {
      oldBarH[i] = 0;
      oldPeakY[i] = 0;
    }
    specInited = true;
  }
  for (int i = 0; i < SPEC_BARS; i++) {
    float target;
    if (isPlaying) {
      float base = sin(millis() * 0.003 + i * 0.5) * 0.5 + 0.5;
      float noise = (random(100) / 100.0) * 0.4;
      float freqWeight = 1.0 - (float)i / SPEC_BARS * 0.5;
      target = (base * 0.6 + noise) * freqWeight * maxH;
    } else {
      target = 0;
    }
    specVals[i] += (target - specVals[i]) * 0.3;
    if (specVals[i] > maxH) specVals[i] = maxH;
    if (specVals[i] < 0) specVals[i] = 0;
    if (specVals[i] > specPeaks[i]) {
      specPeaks[i] = specVals[i];
    } else {
      specPeaks[i] -= 0.8;
      if (specPeaks[i] < 0) specPeaks[i] = 0;
    }
    if (specPeaks[i] > maxH) specPeaks[i] = maxH;
    int x = SPEC_X + 2 + xOff + i * (barW + gap);
    int newH = (int)specVals[i];
    if (newH > maxH) newH = maxH;
    int newPeak = (int)specPeaks[i];
    if (newPeak > maxH) newPeak = maxH;
    if (newH == oldBarH[i] && newPeak == oldPeakY[i]) continue;
    if (oldPeakY[i] > 0 && oldPeakY[i] > oldBarH[i]) {
      int oldPy = SPEC_Y + 2 + maxH - oldPeakY[i] - 1;
      tft.drawFastHLine(x, oldPy, barW, WA_BAR_BG);
      if (oldPy + 1 < SPEC_Y + 2 + maxH)
        tft.drawFastHLine(x, oldPy + 1, barW, WA_BAR_BG);
    }
    if (newH < oldBarH[i]) {
      int eraseFrom = SPEC_Y + 2 + maxH - oldBarH[i];
      int eraseH = oldBarH[i] - newH;
      tft.fillRect(x, eraseFrom, barW, eraseH, WA_BAR_BG);
    }
    if (newH > oldBarH[i]) {
      int drawStart = SPEC_Y + 2 + maxH - newH;
      int drawEnd = SPEC_Y + 2 + maxH - oldBarH[i];
      for (int y = drawStart; y < drawEnd; y++) {
        float t = 1.0 - (float)(y - SPEC_Y - 2) / maxH;
        uint16_t c;
        if (t < 0.4) c = WA_GREEN;
        else if (t < 0.65) c = 0xFFE0;
        else if (t < 0.85) c = WA_ORANGE;
        else c = WA_RED;
        tft.drawFastHLine(x, y, barW, c);
      }
    }
    if (newPeak > 0 && newPeak > newH) {
      int newPy = SPEC_Y + 2 + maxH - newPeak - 1;
      if (newPy >= SPEC_Y + 2) {
        tft.drawFastHLine(x, newPy, barW, WA_WHITE);
        if (newPy + 1 < SPEC_Y + 2 + maxH)
          tft.drawFastHLine(x, newPy + 1, barW, WA_WHITE);
      }
    }
    oldBarH[i] = newH;
    oldPeakY[i] = newPeak;
  }
}

void drawSpectrumFrame() {
  drawInsetPanel(SPEC_X, SPEC_Y, SPEC_W, SPEC_H, 3);
  tft.fillRect(SPEC_X + 2, SPEC_Y + 2, SPEC_W - 4, SPEC_H - 4, WA_BAR_BG);
  for (int y = 8; y < SPEC_H - 4; y += 8) {
    tft.drawFastHLine(SPEC_X + 3, SPEC_Y + 2 + y, SPEC_W - 6, 0x3186);
  }
  specInited = false;
}

void drawButtons() {
  drawButton(btnX[0], BTN_Y, btnW, BTN_H, false);
  drawPrevIcon(btnX[0] + btnW / 2, BTN_Y + BTN_H / 2);
  drawButton(btnX[1], BTN_Y, btnW, BTN_H, false);
  if (isPlaying) drawStopIcon(btnX[1] + btnW / 2, BTN_Y + BTN_H / 2);
  else drawPlayIcon(btnX[1] + btnW / 2, BTN_Y + BTN_H / 2);
  drawButton(btnX[2], BTN_Y, btnW, BTN_H, false);
  drawNextIcon(btnX[2] + btnW / 2, BTN_Y + BTN_H / 2);
  drawButton(btnX[3], BTN_Y, btnW, BTN_H, false);
  tft.setTextColor(WA_ORANGE, WA_BTN);
  tft.setTextSize(1);
  tft.setCursor(btnX[3] + 12, BTN_Y + 8);
  tft.print("STATION");
  tft.setTextColor(WA_LCD_TXT, WA_BTN);
  tft.setCursor(btnX[3] + 16, BTN_Y + 18);
  tft.printf("#%d", currentStation + 1);
}

void drawVolumeBar() {
  drawMetalPanel(VOL_X, VOL_Y, VOL_W, VOL_H, 3);
  tft.setTextColor(WA_LCD_DIM, WA_METAL);
  tft.setTextSize(1);
  tft.setCursor(VOL_X + 6, VOL_Y + 7);
  tft.print("VOL");
  int slX = VOL_X + 34;
  int slW = VOL_W - 70;
  int slY = VOL_Y + VOL_H / 2;
  tft.fillRoundRect(slX, slY - 3, slW, 6, 2, WA_METAL_LO);
  tft.drawFastHLine(slX, slY - 2, slW, 0x0000);
  int fillW = map(volume, 0, 21, 0, slW);
  if (fillW > 0) {
    for (int x = 0; x < fillW; x++) {
      float r = (float)x / slW;
      uint16_t c = r < 0.6 ? WA_GREEN : (r < 0.85 ? 0xFFE0 : WA_RED);
      tft.drawFastVLine(slX + x, slY - 2, 4, c);
    }
  }
  int thumbX = slX + fillW;
  tft.fillRect(thumbX - 2, slY - 7, 5, 14, WA_METAL_HI);
  tft.drawRect(thumbX - 2, slY - 7, 5, 14, WA_METAL_LO);
  tft.drawFastVLine(thumbX, slY - 6, 12, WA_METAL_LO);
  tft.setTextColor(WA_LCD_TXT, WA_METAL);
  tft.setTextSize(1);
  tft.setCursor(VOL_X + VOL_W - 28, VOL_Y + 7);
  tft.printf("%02d", volume);
  for (int i = 0; i <= 4; i++) {
    int tx = slX + map(i, 0, 4, 0, slW);
    tft.drawFastVLine(tx, slY + 4, 2, WA_METAL_LO);
  }
}

void drawBootScreen() {
  tft.fillScreen(WA_BG);
  drawMetalPanel(60, 70, 200, 100, 8);
  tft.setTextColor(WA_ORANGE, WA_METAL);
  tft.setTextSize(3);
  tft.setCursor(88, 85);
  tft.print("WINAMP");
  tft.setTextColor(WA_LCD_TXT, WA_METAL);
  tft.setTextSize(1);
  tft.setCursor(100, 112);
  tft.print("RADIO PLAYER");
  tft.setTextColor(WA_LCD_DIM, WA_METAL);
  tft.setCursor(85, 130);
  tft.print("Connecting to WiFi");
  for (int frame = 0; frame < 30; frame++) {
    int cx = 160, cy = 150;
    for (int i = 0; i < 5; i++) {
      int phase = (frame + i) % 5;
      uint16_t c = (phase == 0) ? WA_ORANGE : WA_METAL_LO;
      tft.fillCircle(cx - 20 + i * 10, cy, 2, c);
    }
    delay(100);
  }
}

void drawMainScreen() {
  tft.fillScreen(WA_BG);
  drawTitleBar();
  drawClock();
  drawWiFiIcon();
  drawLCDBackground();
  drawLCDContent();
  drawSpectrumFrame();
  drawButtons();
  drawVolumeBar();
}

void nextStation() {
  currentStation = (currentStation + 1) % STATION_COUNT;
  audio.connecttohost(stations[currentStation].url);
  isPlaying = true;
  isConnecting = true;
  streamBitrate = 0;
  streamCodec = "";
  streamTitle = "";
  scrollOffset = 0;
  lastShown = "";
  drawLCDContent();
  drawButtons();
}

void prevStation() {
  currentStation = (currentStation - 1 + STATION_COUNT) % STATION_COUNT;
  audio.connecttohost(stations[currentStation].url);
  isPlaying = true;
  isConnecting = true;
  streamBitrate = 0;
  streamCodec = "";
  streamTitle = "";
  scrollOffset = 0;
  lastShown = "";
  drawLCDContent();
  drawButtons();
}

void togglePlayStop() {
  if (isPlaying) {
    audio.stopSong();
    isPlaying = false;
  } else {
    audio.connecttohost(stations[currentStation].url);
    isPlaying = true;
    isConnecting = true;
  }
  drawLCDContent();
  drawButtons();
}

void changeVolume(int delta) {
  volume = constrain(volume + delta, 0, 21);
  audio.setVolume(volume);
  drawVolumeBar();
}

void processEncoder() {
  int delta = encDelta;
  encDelta = 0;
  if (delta == 0) return;
  if (delta > 0) {
    for (int i = 0; i < delta; i++) {
      if (volume < 21) changeVolume(1);
      else { nextStation(); break; }
    }
  } else {
    for (int i = 0; i < -delta; i++) {
      if (volume > 0) changeVolume(-1);
      else { prevStation(); break; }
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  tft.init();
  tft.setRotation(1);
  pinMode(21, OUTPUT);
  digitalWrite(21, HIGH);
  pinMode(ENC_CLK, INPUT_PULLUP);
  pinMode(ENC_DT, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(ENC_CLK), encISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_DT), encISR, CHANGE);
  drawBootScreen();
  WiFi.setSleep(false);
  WiFi.begin(ssid, pass);
  int dots = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (++dots > 20) ESP.restart();
  }
  Serial.println("\nWiFi connected! IP: " + WiFi.localIP().toString());
  configTzTime("YEKT-5", "pool.ntp.org", "time.nist.gov");
  audio.setConnectionTimeout(50000, 50000);
  audio.setVolume(volume);
  audio.forceMono(true);
  audio.connecttohost(stations[currentStation].url);
  isPlaying = true;
  isConnecting = true;
  drawMainScreen();
  Serial.println("Winamp Radio ready!");
}

void loop() {
  audio.loop();
  processEncoder();
  updateSpectrum();
  if (millis() - lastScrollUpdate > 500) {
    lastScrollUpdate = millis();
    scrollOffset++;
    if (scrollOffset > 200) scrollOffset = 0;
    drawLCDContent();
  }
  if (millis() - lastClockUpdate > 1000) {
    lastClockUpdate = millis();
    drawClock();
    drawWiFiIcon();
  }
  if (Serial.available()) {
    char c = Serial.read();
    if (c == '1') nextStation();
    else if (c == '2') prevStation();
    else if (c == ' ') togglePlayStop();
    else if (c == '+') changeVolume(1);
    else if (c == '-') changeVolume(-1);
  }
}

void audio_info(const char *info) {
  String s = info;
  if (s.startsWith("Bitrate: ")) {
    streamBitrate = s.substring(9).toInt() / 1000;
    isConnecting = false;
    drawLCDContent();
  }
  if (s.startsWith("Codec: ")) {
    streamCodec = s.substring(7);
    drawLCDContent();
  }
  if (s.startsWith("Title: ")) {
    streamTitle = s.substring(7);
    scrollOffset = 0;
    lastShown = "";
    drawLCDContent();
  }
}

void audio_eof_stream(const char *info) {
  isPlaying = false;
  drawLCDContent();
  drawButtons();
}
