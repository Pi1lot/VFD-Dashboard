// =============================================================
//  VFD Display Project (ESP8266 / ESP-12F)
//
//  Modes :
//    - scroll : texte horizontal défilant
//    - json   : dashboard JSON adaptatif 1 ou 2 colonnes
//    - matrix : caractères japonais façon Matrix
//
//  Fonctions :
//    - WiFiManager / portail captif
//    - Interface web de configuration
//    - Configuration persistante LittleFS
//    - Dashboard JSON
//    - Pagination configurable
//    - Affichage automatique 1 ou 2 colonnes
//    - Mode Matrix japonais vertical
//
//  Cablage : IDENTIQUE au projet d'origine
//  U8G2_GP1287AI_256X50_F_4W_HW_SPI
//  CS = 15
//  DC = 16
//
//  Bibliotheques nécessaires :
//    - U8g2
//    - ArduinoJson v6
//    - WiFiManager by tzapu
//    - LittleFS
//
//  Carte : Generic ESP8266 Module
// =============================================================

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <WiFiManager.h>
#include <U8g2lib.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <vector>
#include <math.h>
#include <string.h>

// =============================================================
//  ECRAN
// =============================================================

U8G2_GP1287AI_256X50_F_4W_HW_SPI
  u8g2(U8G2_R2, 15, U8X8_PIN_NONE, 16);

// =============================================================
//  SERVEUR WEB
// =============================================================

ESP8266WebServer server(80);

// =============================================================
//  POLICES
// =============================================================

struct FontOption {
  const char* name;
  const uint8_t* font;
};

const FontOption fontList[] = {

  // Spleen
  { "Spleen 6x12",         u8g2_font_spleen6x12_mr },
  { "Spleen 8x16",         u8g2_font_spleen8x16_mr },
  { "Spleen 12x24",        u8g2_font_spleen12x24_mr },
  { "Spleen 16x32",        u8g2_font_spleen16x32_mr },
  { "Spleen 32x64",        u8g2_font_spleen32x64_mr },

  // Petites polices
  { "6x12 (accents)",      u8g2_font_6x12_mf },
  { "6x10 (accents)",      u8g2_font_6x10_mf },
  { "7x13",                u8g2_font_7x13_mr },
  { "7x14",                u8g2_font_7x14_mr },
  { "9x15",                u8g2_font_9x15_mr },
  { "10x20",               u8g2_font_10x20_mr },

  // Helvetica
  { "Helvetica 8 Regular", u8g2_font_helvR08_tr },
  { "Helvetica 12 Regular",u8g2_font_helvR12_tr },
  { "Helvetica 18 Regular",u8g2_font_helvR18_tr },

  { "Helvetica 8 Gras",    u8g2_font_helvB08_tr },
  { "Helvetica 12 Gras",   u8g2_font_helvB12_tr },
  { "Helvetica 18 Gras",   u8g2_font_helvB18_tr },

  // New Century
  { "NCenB 8",             u8g2_font_ncenB08_tr },
  { "NCenB 14",            u8g2_font_ncenB14_tr },

  // Logisoso
  { "Logisoso 16",         u8g2_font_logisoso16_tr },
  { "Logisoso 22",         u8g2_font_logisoso22_tr },
  { "Logisoso 28",         u8g2_font_logisoso28_tr },
  { "Logisoso 46",         u8g2_font_logisoso46_tr },

  // Futura
  { "Futura Gras 11",      u8g2_font_fub11_tr },
  { "Futura Gras 17",      u8g2_font_fub17_tr },
  { "Futura Gras 25",      u8g2_font_fub25_tr },

  // Courier
  { "Courier Gras 8",      u8g2_font_courB08_tf },
  { "Courier Gras 18",     u8g2_font_courB18_tf },

  // Inconsolata
  { "Inconsolata Gras 16", u8g2_font_inb16_mf },
  { "Inconsolata Gras 27", u8g2_font_inb27_mf }
};

const int fontCount =
  sizeof(fontList) / sizeof(fontList[0]);

// =============================================================
//  CONFIGURATION
// =============================================================

struct Config {

  String mode = "scroll";

  // -----------------------------------------------------------
  // Texte défilant
  // -----------------------------------------------------------

  String scrollText = "Hello World !";
  int fontIndex = 1;
  int scrollSpeed = 40;
  int scrollBaseline = 3;

  // -----------------------------------------------------------
  // Dashboard
  // -----------------------------------------------------------

  int jsonInterval = 10;
  int pageFetchCount = 5;

  // -----------------------------------------------------------
  // Matrix
  // -----------------------------------------------------------

  int matrixSpeed = 50;

  // -----------------------------------------------------------
  // Animations autonomes
  // -----------------------------------------------------------

  String animation = "snake";
  int animationSpeed = 50;
  int lifeInitialCells = 90;
  int lifeMaxGenerations = 900;
  // -----------------------------------------------------------
  // Oscilloscope
  // -----------------------------------------------------------

  float scopeFrequency = 0.35f;
  float scopeFrequencyVariation = 0.12f;
  float scopeFrequencySpeed = 0.0005f;
};

Config config;

// =============================================================
//  SOURCES JSON
// =============================================================

struct Field {
  String label;
  String key;
  String type;   // string / int / float / scrolltext
};

struct Source {
  String title;
  String url;
  std::vector<Field> fields;
};

std::vector<Source> sources;

const char* CONFIG_PATH = "/config.json";

// Limites volontairement modestes pour préserver la RAM de l'ESP8266.
const uint8_t MAX_SOURCES = 6;
const uint8_t MAX_FIELDS_PER_SOURCE = 10;

// =============================================================
//  DONNEES JSON AFFICHEES
// =============================================================

struct DisplayValue {
  String text;
  bool scrollText;
};

struct DisplaySource {
  String title;
  std::vector<DisplayValue> values;
};

std::vector<DisplaySource> displaySources;

unsigned long lastJsonFetch = 0;
unsigned long lastPageSwitch = 0;

int currentSource = 0;
int currentPage = 0;
uint16_t pagePollCount = 0;

// =============================================================
//  ETAT TEXTE DEFILANT
// =============================================================

int scrollX = 0;
unsigned long lastScrollStep = 0;

// =============================================================
//  ETAT MATRIX
// =============================================================

// 256 px / 8 px = 32 colonnes
const int MATRIX_COLS = 32;

// Nombre de caractères dans la traînée
const int MATRIX_TRAIL = 5;

int matrixHeadY[MATRIX_COLS];

uint8_t matrixChar[MATRIX_COLS][MATRIX_TRAIL];

unsigned long lastMatrixStep = 0;

// =============================================================
//  CARACTERES JAPONAIS MATRIX
// =============================================================
//
// Katakana principalement.
// La police U8g2 japanese1 permet leur affichage UTF-8.
//
// Si ton éditeur Arduino affiche correctement ces caractères,
// ils seront directement compilés comme UTF-8.
// =============================================================

// =============================================================
// Matrix
// =============================================================

#define MATRIX_COLS 32
#define MATRIX_MAX_TRAIL 9

const char* matrixChars[] = {
  "ア", "イ", "ウ", "エ", "オ",
  "カ", "キ", "ク", "ケ", "コ",
  "サ", "シ", "ス", "セ", "ソ",
  "タ", "チ", "ツ", "テ", "ト",
  "ナ", "ニ", "ヌ", "ネ", "ノ",
  "ハ", "ヒ", "フ", "ヘ", "ホ",
  "マ", "ミ", "ム", "メ", "モ",
  "ヤ", "ユ", "ヨ",
  "ラ", "リ", "ル", "レ", "ロ",
  "ワ", "ヲ", "ン"
};

const int matrixCharCount =
  sizeof(matrixChars) / sizeof(matrixChars[0]);

struct MatrixColumn {

  // Position verticale de la tête
  float y;

  // Vitesse propre à cette colonne
  float speed;

  // Nombre de caractères dans la traînée
  uint8_t trailLength;

  // Espacement vertical entre les caractères
  uint8_t spacing;

  // Temps avant le prochain changement de caractères
  unsigned long charTimer;

  // Intervalle de changement
  unsigned long charInterval;

  // Caractères de la traînée
  uint8_t chars[MATRIX_MAX_TRAIL];

  // Indique si la colonne est actuellement active
  bool active;
};

MatrixColumn matrixColumns[MATRIX_COLS];

bool matrixInitialized = false;
unsigned long lastMatrixFrame = 0;

// =============================================================
//  ANIMATIONS AUTONOMES - ETAT MINIMAL
// =============================================================

bool animationInitialized = false;
unsigned long lastAnimationFrame = 0;

// Snake : grille 32x6, 192 cellules max. Un segment = 1 octet X + 1 octet Y.
const uint8_t SNAKE_W = 32;
const uint8_t SNAKE_H = 6;
const uint8_t SNAKE_MAX = 50;
uint8_t snakeX[SNAKE_MAX];
uint8_t snakeY[SNAKE_MAX];
uint8_t snakeLen = 8;
int8_t snakeDX = 1, snakeDY = 0;
uint8_t foodX = 20, foodY = 3;
unsigned long snakeNextTurn = 0;


// Game of Life : grille compacte de 64x12 bits = 96 octets par buffer.
uint8_t lifeA[12][8];
uint8_t lifeB[12][8];
uint16_t lifeGeneration = 0;

// Oscilloscope : historique compact de 128 points, un octet par échantillon.
uint8_t scopeY[128];
float scopePhase = 0.0f;
int16_t scopeValue = 0;
float scopeCurrentFrequency = 0.0f;

// Réseau de neurones (animation) : purement visuel, aucun calcul réel.
// Réseau de neurones (animation) : purement visuel, aucun calcul réel.
const uint8_t NN_LAYER_COUNT = 4;
const uint8_t nnLayerSize[NN_LAYER_COUNT]   = {5, 6, 5, 10};
const uint8_t nnLayerOffset[NN_LAYER_COUNT] = {0, 5, 11, 16};
const uint8_t NN_NODE_COUNT = 26; // 5+6+5+10

bool nnActive[NN_NODE_COUNT];
uint8_t nnOrder[10];        // ordre aléatoire d'activation de la couche en cours
uint8_t nnPhase = 0;        // 0=pause,1..3=couches,4=scan sortie,5=résultat
uint8_t nnActiveCount = 0;
uint8_t nnTargetDigit = 0;
uint8_t nnScanIndex = 0;
uint8_t nnScanStart = 0;
unsigned long nnPhaseTimer = 0;


int nnNodeX(uint8_t layer) {
  const int xs[NN_LAYER_COUNT] = {50, 96, 142, 188};
  return xs[layer];
}

int nnNodeY(uint8_t layer, uint8_t idx) {
  uint8_t count = nnLayerSize[layer];
  if (count <= 1) return 24;
  const int top = 5, bottom = 43;
  return top + (int)((long)(bottom - top) * idx / (count - 1));
}

void nnShuffleOrder(uint8_t count) {
  for (uint8_t i = 0; i < count; i++) nnOrder[i] = i;
  for (uint8_t i = count - 1; i > 0; i--) {
    uint8_t j = random(i + 1);
    uint8_t tmp = nnOrder[i]; nnOrder[i] = nnOrder[j]; nnOrder[j] = tmp;
  }
}

void nnResetNetwork() {
  memset(nnActive, 0, sizeof(nnActive));
  nnTargetDigit = random(10);
  nnPhase = 0;
  nnActiveCount = 0;
  nnPhaseTimer = millis() + 400;
}

void updateNeuralNet() {
  unsigned long now = millis();

  switch (nnPhase) {

    case 0: // pause initiale, réseau vide
      if (now >= nnPhaseTimer) {
        nnPhase = 1;
        nnShuffleOrder(nnLayerSize[0]);
        nnActiveCount = 0;
        nnPhaseTimer = now;
      }
      break;

    case 1: case 2: case 3: { // activation des couches 0, 1, 2
      uint8_t layer = nnPhase - 1;
      uint8_t count = nnLayerSize[layer];

      if (nnActiveCount < count) {
        if (now - nnPhaseTimer >= 160) {
          nnActive[nnLayerOffset[layer] + nnOrder[nnActiveCount]] = true;
          nnActiveCount++;
          nnPhaseTimer = now;
        }
      } else if (now - nnPhaseTimer >= 400) {
        if (layer < 2) {
          nnPhase++;
          nnShuffleOrder(nnLayerSize[layer + 1]);
          nnActiveCount = 0;
          nnPhaseTimer = now;
        } else {
          nnPhase = 4; // lancement du "scan" de la couche de sortie
          nnScanIndex = 0;
          const uint8_t stepsTotal = 24;
          nnScanStart = ((int)nnTargetDigit - (int)(stepsTotal - 1) + 100) % 10;
          nnPhaseTimer = now;
        }
      }
      break;
    }

    case 4: { // "machine à sous" sur la couche de sortie (0-9)
      const uint8_t stepsTotal = 24;
      uint16_t stepDelay = 30 + (nnScanIndex * nnScanIndex) / 4; // décélération

      if (now - nnPhaseTimer >= stepDelay) {

        for (uint8_t i = 0; i < nnLayerSize[3]; i++)
          nnActive[nnLayerOffset[3] + i] = false;

        uint8_t node = (nnScanStart + nnScanIndex) % 10;
        nnActive[nnLayerOffset[3] + node] = true;

        nnPhaseTimer = now;
        nnScanIndex++;

        if (nnScanIndex >= stepsTotal) {
          nnPhase = 5;
          nnPhaseTimer = now + 3500; // durée d'affichage du résultat
        }
      }
      break;
    }

    case 5: // résultat affiché, puis on relance
      if (now >= nnPhaseTimer) {
        nnResetNetwork();
      }
      break;
  }
}

// Petite police 5x7 "pixel art" pour représenter le chiffre injecté
// dans le réseau. Un octet par ligne (5 bits utiles, MSB à gauche).
const uint8_t nnDigitFont[10][7] = {
  {0b01110,0b10001,0b10011,0b10101,0b11001,0b10001,0b01110}, // 0
  {0b00100,0b01100,0b00100,0b00100,0b00100,0b00100,0b01110}, // 1
  {0b01110,0b10001,0b00001,0b00010,0b00100,0b01000,0b11111}, // 2
  {0b11111,0b00010,0b00100,0b00010,0b00001,0b10001,0b01110}, // 3
  {0b00010,0b00110,0b01010,0b10010,0b11111,0b00010,0b00010}, // 4
  {0b11111,0b10000,0b11110,0b00001,0b00001,0b10001,0b01110}, // 5
  {0b00110,0b01000,0b10000,0b11110,0b10001,0b10001,0b01110}, // 6
  {0b11111,0b00001,0b00010,0b00100,0b01000,0b01000,0b01000}, // 7
  {0b01110,0b10001,0b10001,0b01110,0b10001,0b10001,0b01110}, // 8
  {0b01110,0b10001,0b10001,0b01111,0b00001,0b00010,0b01100}, // 9
};

void nnDrawInputDigit(uint8_t digit, int x, int y, uint8_t cell) {
  if (digit > 9) return;
  for (uint8_t row = 0; row < 7; row++) {
    uint8_t bits = nnDigitFont[digit][row];
    for (uint8_t col = 0; col < 5; col++) {
      if (bits & (1 << (4 - col))) {
        u8g2.drawBox(x + col * cell, y + row * cell, cell - 1, cell - 1);
      }
    }
  }
}

void drawNeuralNet() {
  clearAnimation();

  // -----------------------------------------------------------
  // Entrée : chiffre pixel-art représentant la donnée injectée
  // -----------------------------------------------------------
  const uint8_t cell = 4;
  const int inX = 6, inY = 10;
  u8g2.drawFrame(inX - 4, inY - 4, 5 * cell + 7, 7 * cell + 7);
  nnDrawInputDigit(nnTargetDigit, inX, inY, cell);

  // -----------------------------------------------------------
  // Connexions éparses : chaque nœud ne relie que ses homologues
  // proches en position relative dans la couche suivante.
  // -----------------------------------------------------------
  for (uint8_t l = 0; l < NN_LAYER_COUNT - 1; l++) {
    uint8_t countA = nnLayerSize[l];
    uint8_t countB = nnLayerSize[l + 1];
    float threshold = 4.0f / (float)max(countA, countB);

    for (uint8_t a = 0; a < countA; a++) {
      float posA = (countA <= 1) ? 0.5f : (float)a / (countA - 1);
      int x1 = nnNodeX(l), y1 = nnNodeY(l, a);
      bool srcOn = nnActive[nnLayerOffset[l] + a];

      for (uint8_t b = 0; b < countB; b++) {
        float posB = (countB <= 1) ? 0.5f : (float)b / (countB - 1);
        if (fabsf(posA - posB) > threshold) continue;

        int x2 = nnNodeX(l + 1), y2 = nnNodeY(l + 1, b);
        bool dstOn = nnActive[nnLayerOffset[l + 1] + b];

        u8g2.drawLine(x1, y1, x2, y2);
        if (srcOn && dstOn) u8g2.drawLine(x1, y1 + 1, x2, y2 + 1);
      }
    }
  }

  // Noeuds
  for (uint8_t l = 0; l < NN_LAYER_COUNT; l++) {
    for (uint8_t i = 0; i < nnLayerSize[l]; i++) {
      int x = nnNodeX(l), y = nnNodeY(l, i);
      bool on = nnActive[nnLayerOffset[l] + i];

      if (on) {
        u8g2.drawDisc(x, y, 2);
        if (l == 3) u8g2.drawCircle(x, y, 3);
      } else {
        u8g2.drawCircle(x, y, 1);
      }
    }
  }

  // -----------------------------------------------------------
  // Sortie : chiffre verdict, centré dynamiquement dans son cadre
  // -----------------------------------------------------------
  if (nnPhase == 5) {

    const int boxX = 198, boxY = 2, boxW = 56, boxH = 44;

    u8g2.setFont(u8g2_font_logisoso28_tr);

    char buf[2] = { (char)('0' + nnTargetDigit), 0 };

    int w = u8g2.getStrWidth(buf);
    int ascent = u8g2.getAscent();
    int descent = u8g2.getDescent();
    int fontHeight = ascent - descent;

    int textX = boxX + (boxW - w) / 2;
    int textY = boxY + (boxH - fontHeight) / 2 + ascent;

    u8g2.drawStr(textX, textY, buf);
    u8g2.drawFrame(boxX, boxY, boxW, boxH);
  }

  u8g2.sendBuffer();
}

// =============================================================
//  PAGE HTML
// =============================================================

static const char INDEX_HTML[] PROGMEM =

"<!DOCTYPE html>\n"
"<html lang=\"fr\">\n"

"<head>\n"

"<meta charset=\"UTF-8\">\n"

"<meta name=\"viewport\" "
"content=\"width=device-width, initial-scale=1\">\n"

"<title>VFD Display - Configuration</title>\n"

"<style>\n"

"body { "
"font-family:-apple-system,BlinkMacSystemFont,Arial,sans-serif;"
"background:#111;"
"color:#eee;"
"margin:0;"
"padding:16px;"
"}\n"

"h1 { font-size:1.2em; }\n"

"h2 { font-size:1.05em; margin-top:0; }\n"

".card { "
"background:#1c1c1c;"
"border-radius:10px;"
"padding:16px;"
"margin-bottom:16px;"
"}\n"

"label { "
"display:block;"
"margin:10px 0 4px;"
"font-size:.9em;"
"color:#aaa;"
"}\n"

"input[type=text],"
"input[type=number],"
"input[type=url],"
"select,"
"textarea { "
"width:100%;"
"box-sizing:border-box;"
"padding:8px;"
"border-radius:6px;"
"border:1px solid #444;"
"background:#222;"
"color:#eee;"
"font-size:1em;"
"}\n"

"textarea { min-height:60px; }\n"

"button { "
"background:#3a7bd5;"
"color:#fff;"
"border:none;"
"padding:10px 16px;"
"border-radius:6px;"
"cursor:pointer;"
"font-size:1em;"
"margin-top:6px;"
"}\n"

"button.secondary { background:#444; }\n"

".field-row { "
"display:flex;"
"gap:6px;"
"margin-bottom:6px;"
"align-items:center;"
"}\n"

".field-row input,"
".field-row select { "
"flex:1;"
"min-width:0;"
"}\n"

".field-row button { "
"background:#a83232;"
"padding:8px 10px;"
"margin:0;"
"}\n"
"\n"
".source-card { border:1px solid #333; }\n"

".radio-group label { "
"display:inline-block;"
"margin-right:16px;"
"color:#eee;"
"}\n"

"#status { "
"margin-top:10px;"
"font-size:.9em;"
"color:#8f8;"
"min-height:1.2em;"
"}\n"

".hint { "
"font-size:.85em;"
"color:#888;"
"line-height:1.4;"
"}\n"

"</style>\n"

"</head>\n"

"<body>\n"

"<h1>VFD Display - Configuration</h1>\n"


// =============================================================
// MODE
// =============================================================

"<div class=\"card\">\n"

"<label>Mode d'affichage</label>\n"

"<div class=\"radio-group\">\n"

"<label>"
"<input type=\"radio\" name=\"mode\" value=\"scroll\"> "
"Texte defilant"
"</label>\n"

"<label>"
"<input type=\"radio\" name=\"mode\" value=\"json\"> "
"Dashboard JSON"
"</label>\n"

"<label>"
"<input type=\"radio\" name=\"mode\" value=\"matrix\"> "
"Matrix"
"</label>\n"

"<label>"
"<input type=\"radio\" name=\"mode\" value=\"animation\"> "
"Animations"
"</label>\n"

"</div>\n"

"</div>\n"


// =============================================================
// SCROLL
// =============================================================

"<div class=\"card\" id=\"scrollCard\">\n"

"<h2>Texte defilant</h2>\n"

"<label>Texte a afficher</label>\n"

"<textarea id=\"scrollText\"></textarea>\n"

"<label>Police</label>\n"

"<select id=\"fontIndex\"></select>\n"

"<label>"
"Vitesse de defilement "
"(ms par pas, plus petit = plus rapide)"
"</label>\n"

"<input type=\"number\" "
"id=\"scrollSpeed\" "
"min=\"5\" "
"max=\"500\">\n"

"<label>Position verticale du texte (baseline, pixels)</label>\n"

"<input type=\"number\" "
"id=\"scrollBaseline\" "
"min=\"-20\" "
"max=\"20\">\n"

"<p class=\"hint\">"
"0 = centrage mathematique. Positif = descend, negatif = remonte."
"</p>\n"

"</div>\n"


// =============================================================
// JSON
// =============================================================

"<div class=\"card\" id=\"jsonCard\">\n"

"<h2>Dashboard JSON</h2>\n"

"<label>"
"Intervalle de rafraichissement "
"(secondes)"
"</label>\n"

"<input type=\"number\" "
"id=\"jsonInterval\" "
"min=\"1\" "
"max=\"3600\">\n"

"<label>"
"Nombre de polls avant la page suivante"
"</label>\n"

"<input type=\"number\" "
"id=\"pageFetchCount\" "
"min=\"1\" "
"max=\"100\">\n"

"<p class=\"hint\">"
"Chaque source possède un titre, une URL et ses propres champs. "
"Le titre est affiché dans le bandeau en haut de l'écran. "
"Le type Scroll texte occupe une page à lui seul et revient à la ligne."
"</p>\n"

"<div id=\"sourcesContainer\"></div>\n"

"<button type=\"button\" "
"class=\"secondary\" "
"onclick=\"addSource()\">"
"+ Ajouter une source"
"</button>\n"

"</div>\n"

// =============================================================
// MATRIX
// =============================================================

"<div class=\"card\" id=\"matrixCard\">\n"

"<h2>Mode Matrix</h2>\n"

"<p class=\"hint\">"
"Caracteres japonais defilant verticalement "
"du bas vers le haut."
"</p>\n"

"<label>"
"Vitesse "
"(ms par déplacement, plus petit = plus rapide)"
"</label>\n"

"<input type=\"number\" "
"id=\"matrixSpeed\" "
"min=\"10\" "
"max=\"1000\">\n"

"</div>\n"


// =============================================================
// ANIMATIONS
// =============================================================

"<div class=\"card\" id=\"animationCard\">\n"

"<h2>Animations autonomes</h2>\n"

"<label>Animation</label>\n"

"<select id=\"animation\">\n"
"<option value=\"snake\">Snake autonome</option>\n"
""
"<option value=\"life\">Game of Life</option>\n<option value=\"oscilloscope\">Oscilloscope</option>\n<option value=\"neural\">Réseau de neurones</option>\n"
"</select>\n"

"<label>Vitesse (ms, plus petit = plus rapide)</label>\n"

"<input type=\"number\" id=\"animationSpeed\" min=\"10\" max=\"300\">\n"

"<label>Game of Life — cellules initiales</label>\n"
"<input type=\"number\" id=\"lifeInitialCells\" min=\"10\" max=\"300\">\n"

"<label>Game of Life — nombre maximal de générations</label>\n"
"<input type=\"number\" id=\"lifeMaxGenerations\" min=\"10\" max=\"10000\">\n"

"<div id=\"oscilloscopeSettings\">\n"

"<h2>Oscilloscope</h2>\n"

"<label>Fréquence de base</label>\n"
"<input type=\"number\" "
"id=\"scopeFrequency\" "
"min=\"0.01\" "
"max=\"2.00\" "
"step=\"0.01\">\n"

"<label>Amplitude de fluctuation de la fréquence</label>\n"
"<input type=\"number\" "
"id=\"scopeFrequencyVariation\" "
"min=\"0\" "
"max=\"1.00\" "
"step=\"0.01\">\n"

"<label>Vitesse de fluctuation</label>\n"
"<input type=\"number\" "
"id=\"scopeFrequencySpeed\" "
"min=\"0.0001\" "
"max=\"0.01\" "
"step=\"0.0001\">\n"

"<p class=\"hint\">"
"La fréquence oscille progressivement autour de la fréquence de base."
"</p>\n"

"</div>\n"

"<p class=\"hint\">"
"Les animations sont autonomes et conçues pour limiter l'utilisation de RAM."
"</p>\n"

"</div>\n"


// =============================================================
// SAVE
// =============================================================

"<button onclick=\"saveConfig()\">"
"Enregistrer"
"</button>\n"

"<div id=\"status\"></div>\n"


// =============================================================
// JAVASCRIPT
// =============================================================

"<script>\n"

"let currentConfig = null;\n"


// -------------------------------------------------------------
// Sources / champs
"// -------------------------------------------------------------\n"
"\n"
"function escapeHtml(s) {\n"
"  return String(s || \"\")\n"
"    .replace(/&/g, \"&amp;\")\n"
"    .replace(/</g, \"&lt;\")\n"
"    .replace(/>/g, \"&gt;\")\n"
"    .replace(/\\\"/g, \"&quot;\");\n"
"}\n"
"\n"
"function fieldRowHtml(f) {\n"
"  f = f || {label:\"\", key:\"\", type:\"string\"};\n"
"\n"
"  return `<div class=\"field-row\">\n"
"    <input type=\"text\"\n"
"      placeholder=\"Legende (ex: Temperature)\"\n"
"      value=\"${escapeHtml(f.label)}\"\n"
"      class=\"f-label\">\n"
"\n"
"    <input type=\"text\"\n"
"      placeholder=\"Cle JSON (ex: temperature ou sensors.temp)\"\n"
"      value=\"${escapeHtml(f.key)}\"\n"
"      class=\"f-key\">\n"
"\n"
"    <select class=\"f-type\">\n"
"      <option value=\"string\" ${f.type===\"string\"?\"selected\":\"\"}>Texte</option>\n"
"      <option value=\"int\" ${f.type===\"int\"?\"selected\":\"\"}>Entier</option>\n"
"      <option value=\"float\" ${f.type===\"float\"?\"selected\":\"\"}>Decimal</option>\n"
"      <option value=\"scrolltext\" ${f.type===\"scrolltext\"?\"selected\":\"\"}>Scroll texte</option>\n"
"    </select>\n"
"\n"
"    <button type=\"button\"\n"
"      onclick=\"this.parentElement.remove()\">X</button>\n"
"  </div>`;\n"
"}\n"
"\n"
"function addFieldRow(container, f) {\n"
"  container.insertAdjacentHTML(\"beforeend\", fieldRowHtml(f));\n"
"}\n"
"\n"
"function sourceHtml(src, index) {\n"
"  src = src || {title:\"\", url:\"\", fields:[]};\n"
"\n"
"  return `<div class=\"card source-card\">\n"
"    <h2>Source ${index + 1}</h2>\n"
"\n"
"    <label>Titre affiché sur l'écran</label>\n"
"    <input type=\"text\"\n"
"      class=\"s-title\"\n"
"      placeholder=\"Homelab\"\n"
"      value=\"${escapeHtml(src.title)}\">\n"
"\n"
"    <label>URL du JSON</label>\n"
"    <input type=\"url\"\n"
"      class=\"s-url\"\n"
"      placeholder=\"http://192.168.1.50:5000/data\"\n"
"      value=\"${escapeHtml(src.url)}\">\n"
"\n"
"    <label>Champs</label>\n"
"    <div class=\"fields-container\"></div>\n"
"\n"
"    <button type=\"button\"\n"
"      class=\"secondary\"\n"
"      onclick=\"addFieldRow(this.parentElement.querySelector('.fields-container'))\">\n"
"      + Ajouter un champ\n"
"    </button>\n"
"\n"
"    <button type=\"button\"\n"
"      onclick=\"this.parentElement.remove()\">\n"
"      Supprimer la source\n"
"    </button>\n"
"  </div>`;\n"
"}\n"
"\n"
"function addSource(src) {\n"
"  const container = document.getElementById(\"sourcesContainer\");\n"
"  const index = container.querySelectorAll(\".source-card\").length;\n"
"\n"
"  container.insertAdjacentHTML(\"beforeend\", sourceHtml(src, index));\n"
"\n"
"  const card = container.lastElementChild;\n"
"  const fieldsContainer = card.querySelector(\".fields-container\");\n"
"\n"
"  (src && src.fields ? src.fields : []).forEach(\n"
"    f => addFieldRow(fieldsContainer, f)\n"
"  );\n"
"}\n"
"\n"
"function collectSources() {\n"
"  return [...document.querySelectorAll(\".source-card\")].map(card => ({\n"
"    title: card.querySelector(\".s-title\").value,\n"
"    url: card.querySelector(\".s-url\").value,\n"
"    fields: [...card.querySelectorAll(\".field-row\")].map(row => ({\n"
"      label: row.querySelector(\".f-label\").value,\n"
"      key: row.querySelector(\".f-key\").value,\n"
"      type: row.querySelector(\".f-type\").value\n"
"    }))\n"
"  }));\n"
"}\n"
"\n"

// -------------------------------------------------------------
// Mode
// -------------------------------------------------------------

"function updateModeVisibility() {\n"

"  const mode = "
"document.querySelector("
"'input[name=\"mode\"]:checked'"
").value;\n"

"  document.getElementById(\"scrollCard\")"
".style.display = "
"mode === \"scroll\" ? \"block\" : \"none\";\n"

"  document.getElementById(\"jsonCard\")"
".style.display = "
"mode === \"json\" ? \"block\" : \"none\";\n"

"  document.getElementById(\"matrixCard\")"
".style.display = "
"mode === \"matrix\" ? \"block\" : \"none\";\n"

"  document.getElementById(\"animationCard\")"
".style.display = "
"mode === \"animation\" ? \"block\" : \"none\";\n"

"}\n"


// -------------------------------------------------------------
// Load
// -------------------------------------------------------------

"async function loadConfig() {\n"

"  const res = await fetch(\"/api/config\");\n"

"  const cfg = await res.json();\n"

"  currentConfig = cfg;\n"

"  \n"

"  document.querySelector("
"`input[name=\"mode\"][value=\"${cfg.mode}\"]`"
").checked = true;\n"

"  \n"

"  document.getElementById(\"scrollText\")"
".value = cfg.scrollText;\n"

"  \n"

"  document.getElementById(\"scrollSpeed\")"
".value = cfg.scrollSpeed;\n"

"  document.getElementById(\"scrollBaseline\")"
".value = cfg.scrollBaseline;\n"

"  \n"

"  document.getElementById(\"jsonInterval\")"
".value = cfg.jsonInterval;\n"

"  \n"

"  document.getElementById(\"pageFetchCount\")"
".value = cfg.pageFetchCount || 5;\n"

"  \n"

"  document.getElementById(\"matrixSpeed\")"
".value = cfg.matrixSpeed;\n"

"  document.getElementById(\"animation\")"
".value = cfg.animation || \"snake\";\n"

"  document.getElementById(\"animationSpeed\")"
".value = cfg.animationSpeed || 50;\n"

"  document.getElementById(\"lifeInitialCells\")"
".value = cfg.lifeInitialCells || 90;\n"

"  document.getElementById(\"lifeMaxGenerations\")"
".value = cfg.lifeMaxGenerations || 900;\n"

"  document.getElementById(\"scopeFrequency\")"
".value = cfg.scopeFrequency ?? 0.35;\n"

"  document.getElementById(\"scopeFrequencyVariation\")"
".value = cfg.scopeFrequencyVariation ?? 0.12;\n"

"  document.getElementById(\"scopeFrequencySpeed\")"
".value = cfg.scopeFrequencySpeed ?? 0.0005;\n"


// -------------------------------------------------------------
// Fonts
// -------------------------------------------------------------

"  const fontSelect = "
"document.getElementById(\"fontIndex\");\n"

"  fontSelect.innerHTML = \"\";\n"

"  cfg.fonts.forEach((name, i) => {\n"

"    const opt = document.createElement(\"option\");\n"

"    opt.value = i;\n"

"    opt.textContent = name;\n"

"    if (i === cfg.fontIndex) "
"opt.selected = true;\n"

"    fontSelect.appendChild(opt);\n"

"  });\n"


// -------------------------------------------------------------
// Sources
// -------------------------------------------------------------

"  document.getElementById(\"sourcesContainer\").innerHTML = \"\";\n"

"  (cfg.sources || []).forEach("
"src => addSource(src)"
");\n"

"  updateModeVisibility();\n"

"}\n"

// -------------------------------------------------------------
// Save
// -------------------------------------------------------------

"async function saveConfig() {\n"

"  const payload = {\n"
"    mode: document.querySelector("
"'input[name=\"mode\"]:checked'"
").value,\n"
"    scrollText: document.getElementById("
"\"scrollText\""
").value,\n"
"    fontIndex: parseInt("
"document.getElementById(\"fontIndex\").value,"
"10"
"),\n"
"    scrollSpeed: parseInt("
"document.getElementById(\"scrollSpeed\").value,"
"10"
"),\n"
"    scrollBaseline: parseInt("
"document.getElementById(\"scrollBaseline\").value,"
"10"
"),\n"
"    jsonInterval: parseInt("
"document.getElementById(\"jsonInterval\").value,"
"10"
"),\n"
"    pageFetchCount: parseInt("
"document.getElementById(\"pageFetchCount\").value,"
"10"
"),\n"
"    matrixSpeed: parseInt("
"document.getElementById(\"matrixSpeed\").value,"
"10"
"),\n"
"    animation: document.getElementById(\"animation\").value,\n"
"    animationSpeed: parseInt("
"document.getElementById(\"animationSpeed\").value,"
"10"
"),\n"
"    scopeFrequency: parseFloat("
"document.getElementById(\"scopeFrequency\").value"
"),\n"

"    scopeFrequencyVariation: parseFloat("
"document.getElementById(\"scopeFrequencyVariation\").value"
"),\n"

"    scopeFrequencySpeed: parseFloat("
"document.getElementById(\"scopeFrequencySpeed\").value"
"),\n"
"    sources: collectSources()\n"
"  };\n"

"  const statusEl = document.getElementById(\"status\");\n"
"  statusEl.textContent = \"Enregistrement...\";\n"

"  const res = await fetch(\"/api/config\", {\n"
"    method: \"POST\",\n"
"    headers: {\"Content-Type\":\"application/json\"},\n"
"    body: JSON.stringify(payload)\n"
"  });\n"

"  statusEl.textContent = res.ok "
"    ? \"Enregistre ! L'ecran est mis a jour.\" "
"    : \"Erreur lors de l'enregistrement.\";\n"
"}\n"

// -------------------------------------------------------------
// Events
// -------------------------------------------------------------

"document.querySelectorAll("
"'input[name=\"mode\"]'"
").forEach("
"r => r.addEventListener("
"\"change\","
"updateModeVisibility"
")"
");\n"

"loadConfig();\n"

"</script>\n"

"</body>\n"

"</html>\n";

// =============================================================
//  PERSISTANCE
// =============================================================

void saveConfig() {

  DynamicJsonDocument doc(12288);

  doc["mode"] = config.mode;
  doc["scrollText"] = config.scrollText;
  doc["fontIndex"] = config.fontIndex;
  doc["scrollSpeed"] = config.scrollSpeed;
  doc["scrollBaseline"] = config.scrollBaseline;

  doc["jsonInterval"] = config.jsonInterval;
  doc["pageFetchCount"] = config.pageFetchCount;
  doc["matrixSpeed"] = config.matrixSpeed;
  doc["animation"] = config.animation;
  doc["animationSpeed"] = config.animationSpeed;
  doc["lifeInitialCells"] = config.lifeInitialCells;
  doc["lifeMaxGenerations"] = config.lifeMaxGenerations;
  doc["scopeFrequency"] = config.scopeFrequency;
  doc["scopeFrequencyVariation"] = config.scopeFrequencyVariation;
  doc["scopeFrequencySpeed"] = config.scopeFrequencySpeed;
  
  JsonArray srcArr = doc.createNestedArray("sources");

  uint8_t sourceCount = 0;

  for (auto &src : sources) {

    if (sourceCount++ >= MAX_SOURCES) break;

    JsonObject so = srcArr.createNestedObject();
    so["title"] = src.title;
    so["url"] = src.url;

    JsonArray fieldArr = so.createNestedArray("fields");

    uint8_t fieldCount = 0;

    for (auto &f : src.fields) {

      if (fieldCount++ >= MAX_FIELDS_PER_SOURCE) break;

      JsonObject fo = fieldArr.createNestedObject();
      fo["label"] = f.label;
      fo["key"] = f.key;
      fo["type"] = f.type;
    }
  }

  File file = LittleFS.open(CONFIG_PATH, "w");

  if (file) {
    serializeJson(doc, file);
    file.close();
  } else {
    Serial.println("Erreur ecriture config.json");
  }
}

// =============================================================

void loadConfig() {

  if (!LittleFS.exists(CONFIG_PATH)) {

    Serial.println(
      "Aucune config existante, valeurs par defaut utilisees."
    );

    saveConfig();
    return;
  }

  File file = LittleFS.open(CONFIG_PATH, "r");

  if (!file) {
    Serial.println("Impossible d'ouvrir config.json");
    return;
  }

  DynamicJsonDocument doc(12288);

  DeserializationError err = deserializeJson(doc, file);

  file.close();

  if (err) {
    Serial.println(
      "Config corrompue, valeurs par defaut utilisees."
    );
    return;
  }

  config.mode = doc["mode"] | config.mode;

  if (config.mode != "scroll" &&
      config.mode != "json" &&
      config.mode != "matrix" &&
      config.mode != "neural" &&
      config.mode != "animation") {
    config.mode = "scroll";
  }

  config.scrollText = doc["scrollText"] | config.scrollText;
  config.fontIndex = doc["fontIndex"] | config.fontIndex;
  config.scrollSpeed = doc["scrollSpeed"] | config.scrollSpeed;
  config.scrollBaseline = doc["scrollBaseline"] | config.scrollBaseline;

  config.jsonInterval = doc["jsonInterval"] | config.jsonInterval;
  config.pageFetchCount = doc["pageFetchCount"] | (doc["pageInterval"] | config.pageFetchCount);
  config.matrixSpeed = doc["matrixSpeed"] | config.matrixSpeed;
  config.animation = doc["animation"] | config.animation;
  config.animationSpeed = doc["animationSpeed"] | config.animationSpeed;
  config.lifeInitialCells = doc["lifeInitialCells"] | config.lifeInitialCells;
  config.lifeMaxGenerations = doc["lifeMaxGenerations"] | config.lifeMaxGenerations;

  config.scopeFrequency =
    doc["scopeFrequency"] | config.scopeFrequency;

  config.scopeFrequencyVariation =
    doc["scopeFrequencyVariation"] | config.scopeFrequencyVariation;

  config.scopeFrequencySpeed =
    doc["scopeFrequencySpeed"] | config.scopeFrequencySpeed;
    
  if (config.fontIndex < 0 || config.fontIndex >= fontCount)
    config.fontIndex = 0;

  if (config.scrollSpeed < 5)
    config.scrollSpeed = 5;

  if (config.scrollBaseline < -20)
    config.scrollBaseline = -20;
  if (config.scrollBaseline > 20)
    config.scrollBaseline = 20;

  if (config.jsonInterval < 1)
    config.jsonInterval = 1;

  if (config.pageFetchCount < 1) config.pageFetchCount = 1;
  if (config.pageFetchCount > 100) config.pageFetchCount = 100;

  if (config.matrixSpeed < 10)
    config.matrixSpeed = 10;

  if (config.animation != "snake" &&
      config.animation != "life" &&
      config.animation != "neural" &&
      config.animation != "oscilloscope") {
    config.animation = "snake";
  }

  config.animationSpeed = constrain(config.animationSpeed, 10, 300);
  config.lifeInitialCells = constrain(config.lifeInitialCells, 10, 180);
  config.lifeMaxGenerations = constrain(config.lifeMaxGenerations, 10, 10000);
  config.scopeFrequency =
    constrain(config.scopeFrequency, 0.01f, 2.0f);

  config.scopeFrequencyVariation =
    constrain(config.scopeFrequencyVariation, 0.0f, 1.0f);

  config.scopeFrequencySpeed =
    constrain(config.scopeFrequencySpeed, 0.0001f, 0.01f);
    
  sources.clear();

  // Compatibilité avec l'ancien format :
  // une seule jsonUrl + une liste fields.
  if (!doc.containsKey("sources") &&
      doc.containsKey("jsonUrl")) {

    Source legacy;
    legacy.title = "Dashboard";
    legacy.url = doc["jsonUrl"] | "";

    if (doc.containsKey("fields")) {

      uint8_t fieldCount = 0;

      for (JsonObject fo :
           doc["fields"].as<JsonArray>()) {

        if (fieldCount++ >= MAX_FIELDS_PER_SOURCE)
          break;

        Field f;
        f.label = fo["label"] | "";
        f.key = fo["key"] | "";
        f.type = fo["type"] | "string";

        if (f.type != "string" &&
            f.type != "int" &&
            f.type != "float" &&
            f.type != "scrolltext") {
          f.type = "string";
        }

        legacy.fields.push_back(f);
      }
    }

    sources.push_back(legacy);
  }

  if (doc.containsKey("sources")) {

    uint8_t sourceCount = 0;

    for (JsonObject so : doc["sources"].as<JsonArray>()) {

      if (sourceCount++ >= MAX_SOURCES) break;

      Source src;
      src.title = so["title"] | "";
      src.url = so["url"] | "";

      if (so.containsKey("fields")) {

        uint8_t fieldCount = 0;

        for (JsonObject fo :
             so["fields"].as<JsonArray>()) {

          if (fieldCount++ >= MAX_FIELDS_PER_SOURCE) break;

          Field f;
          f.label = fo["label"] | "";
          f.key = fo["key"] | "";
          f.type = fo["type"] | "string";

          if (f.type != "string" &&
              f.type != "int" &&
              f.type != "float" &&
              f.type != "scrolltext") {
            f.type = "string";
          }

          src.fields.push_back(f);
        }
      }

      sources.push_back(src);
    }
  }
}

// =============================================================
//  HANDLER RACINE
// =============================================================

void handleRoot() {

  server.sendHeader(
    "Cache-Control",
    "no-store, no-cache, must-revalidate"
  );

  server.sendHeader(
    "Pragma",
    "no-cache"
  );

  server.send_P(
    200,
    "text/html",
    INDEX_HTML
  );
}

// =============================================================
//  GET CONFIG
// =============================================================

void handleGetConfig() {

  DynamicJsonDocument doc(12288);

  JsonArray fontsArr = doc.createNestedArray("fonts");

  for (int i = 0; i < fontCount; i++)
    fontsArr.add(fontList[i].name);

  doc["mode"] = config.mode;
  doc["scrollText"] = config.scrollText;
  doc["fontIndex"] = config.fontIndex;
  doc["scrollSpeed"] = config.scrollSpeed;
  doc["scrollBaseline"] = config.scrollBaseline;

  doc["jsonInterval"] = config.jsonInterval;
  doc["pageFetchCount"] = config.pageFetchCount;
  doc["matrixSpeed"] = config.matrixSpeed;
  doc["animation"] = config.animation;
  doc["animationSpeed"] = config.animationSpeed;
  doc["lifeInitialCells"] = config.lifeInitialCells;
  doc["lifeMaxGenerations"] = config.lifeMaxGenerations;
  doc["scopeFrequency"] = config.scopeFrequency;
  doc["scopeFrequencyVariation"] = config.scopeFrequencyVariation;
  doc["scopeFrequencySpeed"] = config.scopeFrequencySpeed;

  JsonArray srcArr = doc.createNestedArray("sources");

  for (uint8_t si = 0;
       si < sources.size() && si < MAX_SOURCES;
       si++) {

    Source &src = sources[si];

    JsonObject so = srcArr.createNestedObject();
    so["title"] = src.title;
    so["url"] = src.url;

    JsonArray fieldArr = so.createNestedArray("fields");

    for (uint8_t fi = 0;
         fi < src.fields.size() && fi < MAX_FIELDS_PER_SOURCE;
         fi++) {

      JsonObject fo = fieldArr.createNestedObject();
      fo["label"] = src.fields[fi].label;
      fo["key"] = src.fields[fi].key;
      fo["type"] = src.fields[fi].type;
    }
  }

  String out;
  serializeJson(doc, out);

  server.send(
    200,
    "application/json",
    out
  );
}

// =============================================================
//  POST CONFIG
// =============================================================

void handleSetConfig() {

  if (!server.hasArg("plain")) {
    server.send(400, "text/plain", "Corps de requete manquant");
    return;
  }

  DynamicJsonDocument doc(12288);

  DeserializationError err =
    deserializeJson(doc, server.arg("plain"));

  if (err) {
    server.send(400, "text/plain", "JSON invalide");
    return;
  }

  config.mode = doc["mode"] | config.mode;

  if (config.mode != "scroll" &&
      config.mode != "json" &&
      config.mode != "matrix" &&
      config.mode != "neural" &&
      config.mode != "animation") {
    config.mode = "scroll";
  }

  config.scrollText = doc["scrollText"] | config.scrollText;
  config.fontIndex = doc["fontIndex"] | config.fontIndex;
  config.scrollSpeed = doc["scrollSpeed"] | config.scrollSpeed;
  config.scrollBaseline = doc["scrollBaseline"] | config.scrollBaseline;

  if (config.fontIndex < 0 || config.fontIndex >= fontCount)
    config.fontIndex = 0;

  if (config.scrollSpeed < 5)
    config.scrollSpeed = 5;

  if (config.scrollBaseline < -20)
    config.scrollBaseline = -20;
  if (config.scrollBaseline > 20)
    config.scrollBaseline = 20;

  config.jsonInterval = doc["jsonInterval"] | config.jsonInterval;
  config.pageFetchCount = doc["pageFetchCount"] | (doc["pageInterval"] | config.pageFetchCount);

  if (config.jsonInterval < 1)
    config.jsonInterval = 1;

  if (config.pageFetchCount < 1) config.pageFetchCount = 1;
  if (config.pageFetchCount > 100) config.pageFetchCount = 100;

  config.matrixSpeed = doc["matrixSpeed"] | config.matrixSpeed;
  config.animation = doc["animation"] | config.animation;
  config.animationSpeed = doc["animationSpeed"] | config.animationSpeed;
  config.lifeInitialCells = doc["lifeInitialCells"] | config.lifeInitialCells;
  config.lifeMaxGenerations = doc["lifeMaxGenerations"] | config.lifeMaxGenerations;
  config.scopeFrequency =
    doc["scopeFrequency"] | config.scopeFrequency;

  config.scopeFrequencyVariation =
    doc["scopeFrequencyVariation"] | config.scopeFrequencyVariation;

  config.scopeFrequencySpeed =
    doc["scopeFrequencySpeed"] | config.scopeFrequencySpeed;
    
  if (config.matrixSpeed < 10)
    config.matrixSpeed = 10;

  if (config.animation != "snake" &&
      config.animation != "life" &&
      config.animation != "neural" &&
      config.animation != "oscilloscope") {
    config.animation = "snake";
  }

  config.animationSpeed = constrain(config.animationSpeed, 10, 300);
  config.lifeInitialCells = constrain(config.lifeInitialCells, 10, 180);
  config.lifeMaxGenerations = constrain(config.lifeMaxGenerations, 10, 10000);
    config.scopeFrequency =
    constrain(config.scopeFrequency, 0.01f, 2.0f);

  config.scopeFrequencyVariation =
    constrain(config.scopeFrequencyVariation, 0.0f, 1.0f);

  config.scopeFrequencySpeed =
    constrain(config.scopeFrequencySpeed, 0.0001f, 0.01f);

  // -----------------------------------------------------------
  // Sources
  // -----------------------------------------------------------

  sources.clear();

  if (doc.containsKey("sources")) {

    uint8_t sourceCount = 0;

    for (JsonObject so :
         doc["sources"].as<JsonArray>()) {

      if (sourceCount++ >= MAX_SOURCES) break;

      Source src;

      src.title = so["title"] | "";
      src.url = so["url"] | "";

      if (so.containsKey("fields")) {

        uint8_t fieldCount = 0;

        for (JsonObject fo :
             so["fields"].as<JsonArray>()) {

          if (fieldCount++ >= MAX_FIELDS_PER_SOURCE) break;

          Field f;

          f.label = fo["label"] | "";
          f.key = fo["key"] | "";
          f.type = fo["type"] | "string";

          if (f.type != "string" &&
              f.type != "int" &&
              f.type != "float" &&
              f.type != "scrolltext") {
            f.type = "string";
          }

          src.fields.push_back(f);
        }
      }

      sources.push_back(src);
    }
  }

  saveConfig();

  server.send(
    200,
    "application/json",
    "{\"ok\":true}"
  );

  displaySources.clear();

  currentSource = 0;
  currentPage = 0;

  lastJsonFetch = 0;
  lastPageSwitch = millis();

  scrollX = u8g2.getWidth();

  matrixInitialized = false;
  lastMatrixStep = 0;
  animationInitialized = false;
  lastAnimationFrame = 0;
}

// =============================================================
//  MODE TEXTE DEFILANT
// =============================================================


void updateScrollDisplay() {

  if (millis() - lastScrollStep <
      (unsigned long)config.scrollSpeed) {
    return;
  }

  lastScrollStep = millis();

  int fIdx = config.fontIndex;

  if (fIdx < 0 || fIdx >= fontCount) {
    fIdx = 0;
  }

  u8g2.clearBuffer();

  // Police sélectionnée
  u8g2.setFont(fontList[fIdx].font);

  // -----------------------------------------------------------
  // ÉCRAN : 256 x 48
  // -----------------------------------------------------------

  const int DISPLAY_HEIGHT = 50;

  // -----------------------------------------------------------
  // Hauteur de la police
  // -----------------------------------------------------------

  const int ascent  = u8g2.getAscent();
  const int descent = u8g2.getDescent();

  const int fontHeight =
    ascent - descent;

  // -----------------------------------------------------------
  // Centrage vertical
  // -----------------------------------------------------------

  int baseline =
    (DISPLAY_HEIGHT - fontHeight) / 2
    + ascent;

  // Ajustement vertical configurable depuis l'interface web.
  // Positif = descend, négatif = remonte.
  baseline += config.scrollBaseline;

  // -----------------------------------------------------------
  // Largeur du texte
  // -----------------------------------------------------------

  const int textWidth =
    u8g2.getStrWidth(
      config.scrollText.c_str()
    );

  // -----------------------------------------------------------
  // Affichage
  // -----------------------------------------------------------

  u8g2.drawStr(
    scrollX,
    baseline,
    config.scrollText.c_str()
  );

  // -----------------------------------------------------------
  // Défilement
  // -----------------------------------------------------------

  scrollX--;

  if (scrollX < -textWidth) {
    scrollX = u8g2.getWidth();
  }

  u8g2.sendBuffer();
}



// =============================================================
//  NAVIGATION JSON
// =============================================================

JsonVariant navigate(
  JsonVariant root,
  const String &key
) {

  JsonVariant cur = root;

  int start = 0;

  while (true) {

    int dot = key.indexOf('.', start);

    String part =
      (dot == -1)
      ? key.substring(start)
      : key.substring(start, dot);

    cur = cur[part];

    if (dot == -1)
      break;

    start = dot + 1;
  }

  return cur;
}

// =============================================================
//  OUTILS AFFICHAGE
// =============================================================

// Dessine le titre et son ruban sur la première ligne.
// Le titre est tronqué s'il est trop long pour rester lisible.
void drawSourceHeader(const String &title) {

  u8g2.setFont(u8g2_font_spleen6x12_mr);

  const int width = u8g2.getWidth();
  const int y = 10;
  const int gap = 4;

  String shown = title;

  while (shown.length() > 0 &&
         u8g2.getStrWidth(shown.c_str()) > width - 20) {
    shown.remove(shown.length() - 1);
  }

  if (shown.length() == 0)
    shown = "...";

  int titleWidth =
    u8g2.getStrWidth(shown.c_str());

  u8g2.drawStr(2, y, shown.c_str());

  int lineX = 2 + titleWidth + gap;

  if (lineX < width - 2) {
    u8g2.drawHLine(
      lineX,
      y - 4,
      width - lineX - 2
    );
  }
}

// Retourne le nombre de lignes occupées par une valeur.
// Un scrolltext utilise toute la page.
int valueLines(
  const DisplayValue &v,
  int maxWidth
) {

  if (v.scrollText)
    return 3;

  // Les textes normaux restent sur une seule ligne.
  // S'ils dépassent, ils forceront une nouvelle page.
  if (u8g2.getStrWidth(v.text.c_str()) > maxWidth)
    return 1000;

  return 1;
}

// -------------------------------------------------------------
// Dessin d'un Scroll texte avec retour à la ligne.
// -------------------------------------------------------------

void drawWrappedText(
  const String &text,
  int x,
  int firstBaseline,
  int maxWidth
) {

  String line;
  int y = firstBaseline;

  int pos = 0;

  while (pos < (int)text.length() && y <= 50) {

    int nextSpace = text.indexOf(' ', pos);

    if (nextSpace < 0)
      nextSpace = text.length();

    String word =
      text.substring(pos, nextSpace);

    String candidate = line;

    if (candidate.length() > 0)
      candidate += " ";

    candidate += word;

    if (u8g2.getStrWidth(candidate.c_str()) <= maxWidth) {

      line = candidate;

    } else {

      if (line.length() > 0) {

        u8g2.drawStr(
          x,
          y,
          line.c_str()
        );

        y += 12;
        line = word;

      } else {

        // Mot plus long que la largeur :
        // découpe caractère par caractère.
        String chunk;

        for (int i = 0; i < (int)word.length(); i++) {

          String c =
            word.substring(i, i + 1);

          String test = chunk + c;

          if (u8g2.getStrWidth(test.c_str()) <= maxWidth) {
            chunk = test;
          } else {

            if (chunk.length() > 0) {

              u8g2.drawStr(
                x,
                y,
                chunk.c_str()
              );

              y += 12;
            }

            chunk = c;
          }
        }

        line = chunk;
      }
    }

    pos = nextSpace;

    while (
      pos < (int)text.length() &&
      text.charAt(pos) == ' '
    ) {
      pos++;
    }
  }

  if (line.length() > 0 && y <= 50) {
    u8g2.drawStr(
      x,
      y,
      line.c_str()
    );
  }
}

// =============================================================
//  RECUPERATION JSON
// =============================================================

bool fetchOneSource(
  Source &src,
  DisplaySource &dst
) {

  if (
    WiFi.status() != WL_CONNECTED ||
    src.url.length() == 0
  ) {
    return false;
  }

  HTTPClient http;

  bool secure =
    src.url.startsWith("https://");

  bool began;

  std::unique_ptr<BearSSL::WiFiClientSecure> secureClient;
  WiFiClient plainClient;

  if (secure) {

    secureClient.reset(
      new BearSSL::WiFiClientSecure()
    );

    secureClient->setInsecure();

    began =
      http.begin(
        *secureClient,
        src.url
      );

  } else {

    began =
      http.begin(
        plainClient,
        src.url
      );
  }

  if (!began) {
    Serial.println("Impossible de contacter une source JSON");
    return false;
  }

  int code = http.GET();

  if (code != HTTP_CODE_OK) {

    Serial.printf(
      "Erreur HTTP JSON %s : %d\n",
      src.url.c_str(),
      code
    );

    http.end();
    return false;
  }

  // Document volontairement limité :
  // il ne contient qu'une source à la fois.
  DynamicJsonDocument doc(4096);

  DeserializationError err =
    deserializeJson(
      doc,
      http.getStream()
    );

  if (err) {

    Serial.printf(
      "Erreur parsing JSON : %s\n",
      src.url.c_str()
    );

    http.end();
    return false;
  }

  dst.title = src.title;
  dst.values.clear();

  for (
    uint8_t i = 0;
    i < src.fields.size() &&
    i < MAX_FIELDS_PER_SOURCE;
    i++
  ) {

    Field &f = src.fields[i];

    JsonVariant v =
      navigate(
        doc.as<JsonVariant>(),
        f.key
      );

    DisplayValue dv;
    dv.scrollText = (f.type == "scrolltext");

    String valStr;

    if (f.type == "int") {

      valStr =
        String(
          (long)v.as<long>()
        );

    } else if (f.type == "float") {

      valStr =
        String(
          v.as<float>(),
          2
        );

    } else {

      valStr =
        v.as<String>();
    }

    if (dv.scrollText) {

      // Le scrolltext est une valeur pleine page.
      dv.text =
        f.label.length()
        ? f.label + ": " + valStr
        : valStr;

    } else {

      dv.text =
        f.label.length()
        ? f.label + ": " + valStr
        : valStr;
    }

    dst.values.push_back(dv);
  }

  http.end();

  return true;
}

bool fetchJsonData() {

  displaySources.clear();

  if (sources.empty())
    return false;

  bool anySuccess = false;

  for (
    uint8_t i = 0;
    i < sources.size() &&
    i < MAX_SOURCES;
    i++
  ) {

    DisplaySource dst;

    if (fetchOneSource(sources[i], dst)) {
      displaySources.push_back(dst);
      anySuccess = true;
    }
  }

  // Ne PAS toucher à currentSource/currentPage ici :
  // c'est updateJsonDisplay() qui gère la navigation,
  // sinon chaque rafraîchissement JSON coupe court
  // à l'affichage des sources suivantes.

  if (!anySuccess) Serial.println("Aucune source JSON disponible.");
  return anySuccess;
}

// =============================================================
//  CALCUL DU NOMBRE DE PAGES
// =============================================================

// Une page normale peut contenir 6 lignes sur l'écran,
// avec 3 lignes par colonne.
//
// Une valeur Scroll texte est toujours seule sur sa page.
// Une valeur trop large est également isolée sur sa page,
// ce qui évite de la couper arbitrairement.

int pageCountForSource(
  const DisplaySource &src
) {

  if (src.values.empty())
    return 1;

  const int normalWidth = 124;

  int pages = 0;
  int normalItems = 0;

  for (
    int i = 0;
    i < (int)src.values.size();
    i++
  ) {

    const DisplayValue &v = src.values[i];

    if (v.scrollText) {

      // Chaque Scroll texte occupe obligatoirement sa propre page.
      // Il ne peut jamais être regroupé avec des champs normaux.
      if (normalItems > 0) {
        pages++;
        normalItems = 0;
      }

      pages++;
      continue;
    }

    if (
      u8g2.getStrWidth(v.text.c_str()) >
      normalWidth
    ) {

      if (normalItems > 0) {
        pages++;
        normalItems = 0;
      }

      // Valeur trop longue : page dédiée.
      pages++;
      continue;
    }

    normalItems++;

    if (normalItems >= 6) {
      pages++;
      normalItems = 0;
    }
  }

  if (normalItems > 0)
    pages++;

  return max(1, pages);
}

// Trouve le bloc de valeurs correspondant à une page.
// Retourne l'index de départ et le nombre de valeurs.
// -1 signifie qu'il s'agit d'une page Scroll texte.
bool getPageRange(
  const DisplaySource &src,
  int wantedPage,
  int &start,
  int &count
) {

  start = -1;
  count = 0;

  int page = 0;
  int normalStart = -1;
  int normalCount = 0;

  const int normalWidth = 124;

  for (
    int i = 0;
    i < (int)src.values.size();
    i++
  ) {

    const DisplayValue &v = src.values[i];

    bool dedicated =
      v.scrollText ||
      u8g2.getStrWidth(v.text.c_str()) > normalWidth;

    if (dedicated) {

      if (normalCount > 0) {

        if (page == wantedPage) {
          start = normalStart;
          count = normalCount;
          return true;
        }

        page++;
        normalStart = -1;
        normalCount = 0;
      }

      // Un Scroll texte (ou une valeur trop large) est une page entière.
      // count=1 garantit qu'aucun autre champ ne sera affiché avec lui.
      if (page == wantedPage) {
        start = i;
        count = 1;
        return true;
      }

      page++;
      continue;
    }

    if (normalCount == 0)
      normalStart = i;

    normalCount++;

    if (normalCount >= 6) {

      if (page == wantedPage) {
        start = normalStart;
        count = normalCount;
        return true;
      }

      page++;
      normalStart = -1;
      normalCount = 0;
    }
  }

  if (normalCount > 0 && page == wantedPage) {

    start = normalStart;
    count = normalCount;
    return true;
  }

  return false;
}

// =============================================================
//  DASHBOARD JSON
// =============================================================

void updateJsonDisplay() {

  unsigned long intervalMs =
    (unsigned long)config.jsonInterval * 1000UL;

  if (
    lastJsonFetch == 0 ||
    millis() - lastJsonFetch >= intervalMs
  ) {

    lastJsonFetch = millis();

    if (fetchJsonData()) pagePollCount++;
  }

  u8g2.clearBuffer();

  u8g2.setFont(u8g2_font_spleen6x12_mr);

  if (displaySources.empty()) {

    u8g2.setFont(u8g2_font_spleen8x16_mr);
    u8g2.drawStr(2, 21, "Source");
    u8g2.drawStr(2, 40, "indisponible");

    u8g2.sendBuffer();
    return;
  }

  // -----------------------------------------------------------
  // Source / page courantes
  // -----------------------------------------------------------

  if (
    currentSource < 0 ||
    currentSource >= (int)displaySources.size()
  ) {
    currentSource = 0;
    currentPage = 0;
  }

  int totalPages =
    pageCountForSource(
      displaySources[currentSource]
    );

  if (currentPage >= totalPages)
    currentPage = 0;

  // -----------------------------------------------------------
  // Passage à la page suivante
  // -----------------------------------------------------------

  if (pagePollCount >= (uint16_t)config.pageFetchCount) {

    pagePollCount = 0;
    lastPageSwitch = millis();

    currentPage++;

    if (currentPage >= totalPages) {

      currentPage = 0;
      currentSource++;

      if (currentSource >=
          (int)displaySources.size()) {
        currentSource = 0;
      }
    }
  }

  // Recalcul après changement de source.
  totalPages =
    pageCountForSource(
      displaySources[currentSource]
    );

  if (currentPage >= totalPages)
    currentPage = 0;

  DisplaySource &src =
    displaySources[currentSource];

  // -----------------------------------------------------------
  // Titre
  // -----------------------------------------------------------

  drawSourceHeader(src.title);

  // -----------------------------------------------------------
  // Page
  // -----------------------------------------------------------

  int start = -1;
  int count = 0;

  if (!getPageRange(
        src,
        currentPage,
        start,
        count
      )) {

    u8g2.sendBuffer();
    return;
  }

  if (start >= 0 &&
      count == 1 &&
      src.values[start].scrollText) {

    drawWrappedText(
      src.values[start].text,
      2,
      24,
      252
    );

    u8g2.sendBuffer();
    return;
  }

  // -----------------------------------------------------------
  // Affichage normal : jusqu'à 3 lignes par colonne.
  // -----------------------------------------------------------

  const int leftX = 2;
  const int rightX = 130;
  const int lineY = 24;

  for (
    int i = 0;
    i < count;
    i++
  ) {

    int row = i % 3;
    int x = (i < 3) ? leftX : rightX;

    u8g2.drawStr(
      x,
      lineY + row * 12,
      src.values[start + i].text.c_str()
    );
  }

  u8g2.sendBuffer();
}

// =============================================================
//  ANIMATIONS AUTONOMES
// =============================================================

uint16_t animDelay() {
  return (uint16_t)constrain(config.animationSpeed, 10, 300);
}

void clearAnimation() {
  u8g2.clearBuffer();
}

void resetSnake() {
  snakeLen = 8;
  snakeDX = 1;
  snakeDY = 0;
  for (uint8_t i = 0; i < snakeLen; i++) {
    snakeX[i] = 10 - i;
    snakeY[i] = 3;
  }
  foodX = random(SNAKE_W);
  foodY = random(SNAKE_H);
  snakeNextTurn = millis() + 700;
}

bool snakeOccupied(uint8_t x, uint8_t y) {
  for (uint8_t i = 0; i < snakeLen; i++)
    if (snakeX[i] == x && snakeY[i] == y) return true;
  return false;
}

void placeSnakeFood() {
  for (uint8_t n = 0; n < 30; n++) {
    uint8_t x = random(SNAKE_W), y = random(SNAKE_H);
    if (!snakeOccupied(x, y)) { foodX = x; foodY = y; return; }
  }
}

void chooseSnakeDirection() {
  // L'IA cherche la nourriture sans demi-tour et évite son corps.
  int8_t dx = (int8_t)foodX - snakeX[0];
  int8_t dy = (int8_t)foodY - snakeY[0];
  int8_t candX = snakeDX, candY = snakeDY;

  if (abs(dx) >= abs(dy)) {
    if (dx != 0) { candX = dx > 0 ? 1 : -1; candY = 0; }
    else if (dy != 0) { candX = 0; candY = dy > 0 ? 1 : -1; }
  } else {
    if (dy != 0) { candX = 0; candY = dy > 0 ? 1 : -1; }
    else if (dx != 0) { candX = dx > 0 ? 1 : -1; candY = 0; }
  }

  if (!(candX == -snakeDX && candY == -snakeDY)) {
    uint8_t nx = (snakeX[0] + candX + SNAKE_W) % SNAKE_W;
    uint8_t ny = (snakeY[0] + candY + SNAKE_H) % SNAKE_H;
    if (!snakeOccupied(nx, ny) || (nx == snakeX[snakeLen-1] && ny == snakeY[snakeLen-1])) {
      snakeDX = candX; snakeDY = candY;
    }
  }
}

void updateSnake() {
  unsigned long now = millis();
  if (now - lastAnimationFrame < animDelay()) return;
  lastAnimationFrame = now;

  chooseSnakeDirection();
  int8_t nx = (int8_t)snakeX[0] + snakeDX;
  int8_t ny = (int8_t)snakeY[0] + snakeDY;
  if (nx < 0) nx = SNAKE_W - 1;
  if (nx >= SNAKE_W) nx = 0;
  if (ny < 0) ny = SNAKE_H - 1;
  if (ny >= SNAKE_H) ny = 0;

  if (snakeOccupied(nx, ny) && !(nx == snakeX[snakeLen-1] && ny == snakeY[snakeLen-1])) {
    resetSnake();
    return;
  }

  bool eat = (nx == foodX && ny == foodY);
  uint8_t newLen = snakeLen + (eat && snakeLen < SNAKE_MAX ? 1 : 0);
  for (int i = newLen - 1; i > 0; i--) {
    snakeX[i] = snakeX[i-1]; snakeY[i] = snakeY[i-1];
  }
  snakeX[0] = nx; snakeY[0] = ny;
  snakeLen = newLen;
  if (eat) { placeSnakeFood(); }
}

void drawSnake() {
  clearAnimation();
  u8g2.setFont(u8g2_font_spleen6x12_mr);
  for (uint8_t i = 0; i < snakeLen; i++)
    u8g2.drawBox(snakeX[i] * 8, 1 + snakeY[i] * 7, 7, 6);
  u8g2.drawFrame(foodX * 8 + 2, 3 + foodY * 7, 4, 4);
  u8g2.sendBuffer();
}

void lifeSet(uint8_t a[12][8],uint8_t x,uint8_t y,bool v){
  if(x>=64||y>=12)return;
  uint8_t m=1<<(x&7);
  if(v)a[y][x>>3]|=m; else a[y][x>>3]&=~m;
}

bool lifeGet(uint8_t a[12][8],int x,int y){
  if(x<0||x>=64||y<0||y>=12)return false;
  return (a[y][x>>3]>>(x&7))&1;
}

void resetLife(){
  memset(lifeA,0,sizeof(lifeA));
  memset(lifeB,0,sizeof(lifeB));
  uint16_t wanted=constrain(config.lifeInitialCells,10,180);
  uint16_t placed=0;
  while(placed<wanted){
    uint8_t x=random(64), y=random(12);
    if(!lifeGet(lifeA,x,y)){ lifeSet(lifeA,x,y,true); placed++; }
  }
  lifeGeneration=0;
}

void updateLife(){
  unsigned long now=millis();
  if(now-lastAnimationFrame<animDelay()*2)return;
  lastAnimationFrame=now;

  for(uint8_t y=0;y<12;y++) for(uint8_t x=0;x<64;x++){
    uint8_t n=0;
    for(int8_t dy=-1;dy<=1;dy++) for(int8_t dx=-1;dx<=1;dx++)
      if((dx||dy)&&lifeGet(lifeA,x+dx,y+dy)) n++;
    bool on=lifeGet(lifeA,x,y);
    bool next=(on&&(n==2||n==3))||(!on&&n==3);
    lifeSet(lifeB,x,y,next);
  }
  memcpy(lifeA,lifeB,sizeof(lifeA));
  lifeGeneration++;

  if(lifeGeneration >= (uint16_t)config.lifeMaxGenerations)
    resetLife();
}

void drawLife(){
  clearAnimation();
  for(uint8_t y=0;y<12;y++) for(uint8_t x=0;x<64;x++)
    if(lifeGet(lifeA,x,y)) u8g2.drawBox(x*4,y*4,3,3);
  u8g2.sendBuffer();
}

void resetOscilloscope(){
  for(uint8_t x=0;x<128;x++) scopeY[x]=24;
  scopePhase=0.0f; scopeValue=0;
}

void updateOscilloscope(){
  unsigned long now = millis();

  if(now - lastAnimationFrame < animDelay())
    return;

  lastAnimationFrame = now;

  // Décale l'historique vers la gauche
  for(uint8_t x = 0; x < 127; x++)
    scopeY[x] = scopeY[x + 1];

  // -----------------------------------------------------------
  // Fréquence variable
  // -----------------------------------------------------------

  float frequency =
    config.scopeFrequency
    + sin(millis() * config.scopeFrequencySpeed)
    * config.scopeFrequencyVariation;

  // Sécurité : la fréquence ne peut pas devenir négative
  if(frequency < 0.01f)
    frequency = 0.01f;

  scopeCurrentFrequency = frequency;
  // -----------------------------------------------------------
  // Avance de phase
  // -----------------------------------------------------------

  scopePhase += frequency;

  if(scopePhase >= 2.0f * PI)
    scopePhase -= 2.0f * PI;

  // -----------------------------------------------------------
  // Sinusoïde
  // -----------------------------------------------------------

  int16_t sine =
    (int16_t)(sin(scopePhase) * 11.0f);

  scopeY[127] =
    (uint8_t)constrain(
      24 + sine,
      1,
      46
    );
}
void drawOscilloscope(){
  clearAnimation();

  // -----------------------------------------------------------
  // Cadre
  // -----------------------------------------------------------
  u8g2.drawFrame(0, 0, 256, 50);

  // -----------------------------------------------------------
  // Fréquence instantanée
  // -----------------------------------------------------------
  u8g2.setFont(u8g2_font_5x7_tf);

  char freqText[20];

  snprintf(
    freqText,
    sizeof(freqText),
    "F: %.2f Hz",
    scopeCurrentFrequency
  );

  u8g2.drawStr(4, 8, freqText);

  // -----------------------------------------------------------
  // Petites graduations
  // -----------------------------------------------------------
  for(uint16_t x = 16; x < 256; x += 16){
    u8g2.drawPixel(x, 47);
    u8g2.drawPixel(x, 46);
  }

  // -----------------------------------------------------------
  // Grandes graduations
  // -----------------------------------------------------------
  for(uint16_t x = 32; x < 256; x += 32){
    u8g2.drawPixel(x, 44);
    u8g2.drawPixel(x, 45);
    u8g2.drawPixel(x, 46);
    u8g2.drawPixel(x, 47);
  }

  // -----------------------------------------------------------
  // Courbe
  // -----------------------------------------------------------
  for(uint8_t x = 1; x < 128; x++){
    u8g2.drawLine(
      (x - 1) * 2,
      scopeY[x - 1],
      x * 2,
      scopeY[x]
    );
  }

  u8g2.sendBuffer();
}
void resetAnimation(){animationInitialized=false;lastAnimationFrame=0;}

void updateAnimationDisplay(){
  if(!animationInitialized){
    animationInitialized=true;
    lastAnimationFrame=0;
    if(config.animation=="snake") resetSnake();
    else if(config.animation=="life") resetLife();
    else if(config.animation=="neural") nnResetNetwork();
    else resetOscilloscope();
  }
  if(config.animation=="snake"){updateSnake();drawSnake();}
  else if(config.animation=="life"){updateLife();drawLife();}
  else if(config.animation=="neural"){updateNeuralNet();drawNeuralNet();}
  else {updateOscilloscope();drawOscilloscope();}
}

void resetMatrixColumn(int col, bool randomStart) {

  MatrixColumn &m = matrixColumns[col];

  // -----------------------------------------------------------
  // Longueur aléatoire du ruban
  // -----------------------------------------------------------

  m.trailLength = random(3, MATRIX_MAX_TRAIL + 1);

  // Espacement vertical entre les caractères
  m.spacing = random(7, 10);

  // -----------------------------------------------------------
  // Vitesse propre à cette colonne
  //
  // La vitesse réelle sera calculée dans updateMatrixDisplay()
  // à partir de config.matrixSpeed.
  //
  // 0.60 = colonne plus lente
  // 1.00 = vitesse normale
  // 1.40 = colonne plus rapide
  // -----------------------------------------------------------

  m.speed =
    random(60, 141) / 100.0f;

  // -----------------------------------------------------------
  // Caractères
  // -----------------------------------------------------------

  for (int i = 0; i < MATRIX_MAX_TRAIL; i++) {
    m.chars[i] =
      random(matrixCharCount);
  }

  // -----------------------------------------------------------
  // Changement des caractères
  // -----------------------------------------------------------

  m.charInterval =
    random(100, 300);

  m.charTimer = millis();

  // -----------------------------------------------------------
  // Position initiale
  // -----------------------------------------------------------

  if (randomStart) {

    m.y =
      random(
        -60,
        50
      );

  } else {

    // Nouvelle colonne au-dessus de l'écran
    m.y =
      -random(
        10,
        80
      );
  }

  m.active = true;
}


void resetMatrix() {

  matrixInitialized = true;

  for (int col = 0; col < MATRIX_COLS; col++) {

    resetMatrixColumn(
      col,
      true
    );
  }

  lastMatrixFrame = millis();
}


void updateMatrixDisplay() {

  if (!matrixInitialized) {
    resetMatrix();
  }

  unsigned long now = millis();

  // -----------------------------------------------------------
  // Fréquence de rafraîchissement de l'animation
  // -----------------------------------------------------------
  //
  // On garde une animation fluide.
  // La vitesse réelle des colonnes dépend ensuite de
  // config.matrixSpeed.
  //
  // -----------------------------------------------------------

  if (now - lastMatrixFrame < 16) {
    return;
  }

  lastMatrixFrame = now;

  // ===========================================================
  // VITESSE GLOBALE CONFIGURABLE
  // ===========================================================
  //
  // config.matrixSpeed vient de l'interface web.
  //
  // Exemple :
  //
  // 5  -> très rapide
  // 20 -> rapide
  // 40 -> moyen
  // 80 -> lent
  //
  // Plus la valeur est petite, plus ça va vite.
  //
  // ===========================================================

  float globalSpeed =
    40.0f /
    (float)max(1, config.matrixSpeed);

  // Limites de sécurité
  globalSpeed =
    constrain(
      globalSpeed,
      0.10f,
      8.0f
    );

  // ===========================================================
  // Mise à jour des rubans
  // ===========================================================

  for (int col = 0; col < MATRIX_COLS; col++) {

    MatrixColumn &m =
      matrixColumns[col];

    // ---------------------------------------------------------
    // Vitesse individuelle
    // ---------------------------------------------------------
    //
    // m.speed apporte la variation entre les colonnes.
    //
    // Exemple :
    //
    // config.matrixSpeed = 40
    //
    // colonne lente  : 40 * 0.60
    // colonne normale: 40 * 1.00
    // colonne rapide : 40 * 1.40
    //
    // ---------------------------------------------------------

    float columnSpeed =
      globalSpeed *
      m.speed;

    m.y += columnSpeed;

    // ---------------------------------------------------------
    // Changement des caractères
    // ---------------------------------------------------------

    if (
      now - m.charTimer >=
      m.charInterval
    ) {

      m.charTimer = now;

      // La tête change
      m.chars[0] =
        random(matrixCharCount);

      // Un caractère de la traînée change
      if (m.trailLength > 1) {

        int p =
          random(1, m.trailLength);

        m.chars[p] =
          random(matrixCharCount);
      }

      // Prochain changement
      m.charInterval =
        random(100, 300);
    }

    // ---------------------------------------------------------
    // Recyclage du ruban
    // ---------------------------------------------------------

    int totalHeight =
      m.trailLength *
      m.spacing;

    if (
      m.y >
      50 + totalHeight
    ) {

      resetMatrixColumn(
        col,
        false
      );
    }
  }

  // ===========================================================
  // AFFICHAGE
  // ===========================================================

  u8g2.clearBuffer();

  u8g2.setFont(
    u8g2_font_b10_t_japanese1
  );

  u8g2.setFontMode(1);

  // -----------------------------------------------------------
  // Dessine chaque ruban
  // -----------------------------------------------------------

  for (int col = 0; col < MATRIX_COLS; col++) {

    MatrixColumn &m =
      matrixColumns[col];

    int x =
      col * 8;

    for (
      int i = 0;
      i < m.trailLength;
      i++
    ) {

      // Tête en haut,
      // traînée derrière elle.
      int y =
        (int)m.y -
        (i * m.spacing);

      // Hors écran
      if (
        y < -12 ||
        y > 60
      ) {
        continue;
      }

      // -------------------------------------------------------
      // Dessin du caractère
      // -------------------------------------------------------

      u8g2.drawUTF8(
        x,
        y,
        matrixChars[
          m.chars[i]
        ]
      );
    }
  }

  u8g2.sendBuffer();
}


// =============================================================
//  SETUP
// =============================================================

void setup() {

  // -----------------------------------------------------------
  // Serial
  // -----------------------------------------------------------

  Serial.begin(115200);

  delay(100);

  // -----------------------------------------------------------
  // Ecran
  // -----------------------------------------------------------

  u8g2.begin();

  u8g2.setContrast(255);

  // -----------------------------------------------------------
  // Random
  // -----------------------------------------------------------

  randomSeed(
    micros() ^
    ESP.getChipId()
  );

  // -----------------------------------------------------------
  // Message démarrage
  // -----------------------------------------------------------

  u8g2.clearBuffer();

  u8g2.setFont(
    u8g2_font_spleen8x16_mr
  );

  u8g2.drawStr(
    2,
    29,
    "Demarrage..."
  );

  u8g2.sendBuffer();

  // -----------------------------------------------------------
  // LittleFS
  // -----------------------------------------------------------

  if (!LittleFS.begin()) {

    Serial.println(
      "Erreur LittleFS, formatage..."
    );

    LittleFS.format();

    if (!LittleFS.begin()) {

      Serial.println(
        "Impossible de monter LittleFS"
      );
    }
  }

  loadConfig();

  // -----------------------------------------------------------
  // WiFiManager
  // -----------------------------------------------------------

  WiFiManager wm;

  // Optionnel :
  //
  // wm.setConfigPortalTimeout(180);

  u8g2.clearBuffer();

  u8g2.setFont(
    u8g2_font_spleen6x12_mr
  );

  u8g2.drawStr(
    2,
    20,
    "Connexion WiFi..."
  );

  u8g2.drawStr(
    2,
    35,
    "Sinon: portail"
  );

  u8g2.drawStr(
    2,
    50,
    "VFD-Display-Setup"
  );

  u8g2.sendBuffer();

  bool connected =
    wm.autoConnect(
      "VFD-Display-Setup"
    );

  if (!connected) {

    Serial.println(
      "Echec de connexion WiFi, "
      "redemarrage..."
    );

    ESP.restart();
  }

  // -----------------------------------------------------------
  // IP
  // -----------------------------------------------------------

  u8g2.clearBuffer();

  u8g2.setFont(
    u8g2_font_spleen8x16_mr
  );

  u8g2.drawStr(
    2,
    20,
    "WiFi connecte !"
  );

  u8g2.setFont(
    u8g2_font_spleen6x12_mr
  );

  u8g2.drawStr(
    2,
    40,
    WiFi.localIP()
      .toString()
      .c_str()
  );

  u8g2.sendBuffer();

  Serial.print(
    "Adresse IP : "
  );

  Serial.println(
    WiFi.localIP()
  );

  delay(4000);

  // -----------------------------------------------------------
  // mDNS
  // -----------------------------------------------------------

  if (
    MDNS.begin(
      "vfddisplay"
    )
  ) {

    Serial.println(
      "mDNS actif : "
      "http://vfddisplay.local"
    );
  }

  // -----------------------------------------------------------
  // Routes Web
  // -----------------------------------------------------------

  server.on(
    "/",
    HTTP_GET,
    handleRoot
  );

  server.on(
    "/api/config",
    HTTP_GET,
    handleGetConfig
  );

  server.on(
    "/api/config",
    HTTP_POST,
    handleSetConfig
  );

  // -----------------------------------------------------------
  // Serveur
  // -----------------------------------------------------------

  server.begin();

  Serial.println(
    "Serveur de config demarre."
  );

  // -----------------------------------------------------------
  // Etat initial
  // -----------------------------------------------------------

  scrollX =
    u8g2.getWidth();

  currentPage = 0;

  lastPageSwitch =
    millis();

  matrixInitialized =
    false;
}

// =============================================================
//  LOOP
// =============================================================

void loop() {

  // -----------------------------------------------------------
  // Serveur
  // -----------------------------------------------------------

  server.handleClient();

  MDNS.update();

  // -----------------------------------------------------------
  // Mode
  // -----------------------------------------------------------

  if (
    config.mode == "json"
  ) {

    updateJsonDisplay();

  }

  else if (
    config.mode == "matrix"
  ) {

    updateMatrixDisplay();

  }

  else if (
    config.mode == "animation"
  ) {

    updateAnimationDisplay();

  }

  else {

    updateScrollDisplay();
  }
}
