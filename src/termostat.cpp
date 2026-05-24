#include <Arduino.h>
#include <SPI.h>
#include <RF24.h>
#include <DHT.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <Encoder.h>

// Definitii pini display, senzor si butoane
#define TFT_CS    10
#define TFT_DC    9
#define TFT_RST   8
#define DHTPIN    A2
#define DHTTYPE   DHT11
#define ENC_SW_PIN 4  // PD4
#define BTN_BACK   5  // PD5

// Initializare obiecte periferice
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
DHT dht(DHTPIN, DHTTYPE);
RF24 radio(A0, A1);
Encoder roata(2, 3);

const byte adresa[6] = "TERMO";

// Starile posibile ale meniului
enum StareMeniu {
  MENIU_PRINCIPAL,
  MENIU_SIMPLU,
  MENIU_AVANSAT,
  MENIU_AVANSAT_EROARE
};

StareMeniu stareaCurenta = MENIU_PRINCIPAL;
bool cereRedesenareTotala = true;

// Date senzor si setari termostat
long pozitieVecheEncoder = -999;

float tempReala = 0.0;
float tempSetata = 22.0;

// Histerezis pentru evitarea oscilatiei la pragul de temperatura
float histerezis = 0.5;
bool incalzirePornita = false;

// Transmisie hibrida: trimite doar la schimbare sau la fiecare 10s
byte ultimaPutereTrimisa = 0;
unsigned long ultimulTimpTransmisie = 0;

// Navigare meniu principal
int casutaSelectata = 0;
int modActiv = 0; // 0 = Simplu, 1 = Avansat

// Debounce butoane fizice
unsigned long ultimulClickENTER = 0;
unsigned long ultimulClickBACK = 0;

bool modConfirmat = false;

// Parametri mod avansat: interval, durata incalzire, putere
int intervalOre = 0;
int intervalMin = 0;
int intervalSec = 0;

int duratOre = 0;
int duratMin = 0;
int duratSec = 0;

int putereProc = 0;

int campSelectat = 0;
bool inEditare = false;
int subCampCurent = 0;

unsigned long timpStartAvansat = 0;
bool avansat_incalzireActiva = false;
unsigned long durataTotalaInterval_ms = 0;
unsigned long durataTotalaIncalzire_ms = 0;

// Deseneaza fundalul fix al meniului principal
void deseneazaMeniuPrincipalStatic() {
  tft.fillScreen(ST77XX_BLACK);

  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.print("MOD ACTIV:");

  tft.drawRect(20, 80, 130, 80, ST77XX_WHITE);
  tft.setCursor(35, 110);
  tft.setTextColor(ST77XX_CYAN);
  tft.print("SIMPLU");

  tft.drawRect(170, 80, 130, 80, ST77XX_WHITE);
  tft.setCursor(185, 110);
  tft.setTextColor(ST77XX_MAGENTA);
  tft.print("AVANSAT");
}

// Actualizeaza selectia si temperatura afisata in meniul principal
void actualizeazaMeniuPrincipal() {
  if (casutaSelectata == 0) {
    tft.drawRect(168, 78, 134, 84, ST77XX_BLACK);
    tft.drawRect(18, 78, 134, 84, ST77XX_YELLOW);
  } else {
    tft.drawRect(18, 78, 134, 84, ST77XX_BLACK);
    tft.drawRect(168, 78, 134, 84, ST77XX_YELLOW);
  }

  tft.setCursor(150, 10);
  tft.setTextSize(2);
  if (modActiv == 0) {
    tft.setTextColor(ST77XX_CYAN, ST77XX_BLACK);
    tft.print("SIMPLU   ");
  } else {
    tft.setTextColor(ST77XX_MAGENTA, ST77XX_BLACK);
    tft.print("AVANSAT  ");
  }

  tft.setCursor(50, 190);
  tft.setTextColor(ST77XX_GREEN, ST77XX_BLACK);
  tft.setTextSize(3);
  tft.print(tempReala, 1);
  tft.print(" C");
}

// Interfata grafica mod simplu

// Deseneaza elementele statice ale modului simplu
void deseneazaModSimpluStatic() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 20);
  tft.print("Setare Temperatura:");
  tft.drawRect(20, 150, 280, 40, ST77XX_WHITE);
}

// Actualizeaza temperatura setata si bara de progres colorata
void actualizeazaModSimplu() {
  tft.setCursor(100, 80);
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.setTextSize(4);
  tft.print(tempSetata, 1);
  tft.print(" C ");

  int latimeBara = map(tempSetata * 10, 100, 350, 0, 276);
  if (latimeBara < 0) latimeBara = 0;
  if (latimeBara > 276) latimeBara = 276;

  int rosu = map(tempSetata * 10, 100, 350, 0, 255);
  int albastru = abs(255 - rosu);
  uint16_t culoarBara = tft.color565(rosu, 0, albastru);

  tft.fillRect(22, 152, latimeBara, 36, culoarBara);
  tft.fillRect(22 + latimeBara, 152, 276 - latimeBara, 36, ST77XX_BLACK);
}

// Interfata grafica mod avansat

// Returneaza culoarea chenarului in functie de campul selectat
uint16_t culoareCamp(int index) {
  if (index == campSelectat) return ST77XX_YELLOW;
  return ST77XX_WHITE;
}

// Deseneaza structura fixa a modului avansat
void deseneazaModAvansatStatic() {
  tft.fillScreen(ST77XX_BLACK);

  tft.setTextColor(ST77XX_MAGENTA);
  tft.setTextSize(2);
  tft.setCursor(10, 5);
  tft.print("MOD AVANSAT");

  // Camp 1: Interval Orar
  tft.drawRect(5, 35, 310, 55, culoareCamp(0));
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.setCursor(12, 42);
  tft.print("INTERVAL ORAR");

  // Camp 2: Durata Incalzire
  tft.drawRect(5, 105, 310, 55, culoareCamp(1));
  tft.setCursor(12, 112);
  tft.print("DURATA INCALZIRE");

  // Camp 3: Putere
  tft.drawRect(5, 175, 310, 55, culoareCamp(2));
  tft.setCursor(12, 182);
  tft.print("PUTERE INCALZIRE");

  // Hint comenzi
  tft.setTextColor(ST77XX_CYAN);
  tft.setTextSize(1);
  tft.setCursor(5, 240);
  tft.print("ENC:Sel/Edit  BACK:SubCamp  ENTER:Salveaza");
}

// Afiseaza valorile HH:MM:SS cu subcampul activ evidentiat
void deseneazaValoriTimp(int y, int ore, int min, int sec,
                          bool esteActiv, int subCamp) {
  uint16_t culOre = (esteActiv && subCamp == 0) ? ST77XX_YELLOW : ST77XX_GREEN;
  uint16_t culMin = (esteActiv && subCamp == 1) ? ST77XX_YELLOW : ST77XX_GREEN;
  uint16_t culSec = (esteActiv && subCamp == 2) ? ST77XX_YELLOW : ST77XX_GREEN;

  tft.setTextSize(2);

  tft.setTextColor(culOre, ST77XX_BLACK);
  tft.setCursor(20, y);
  if (ore < 10) tft.print("0");
  tft.print(ore);

  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.setCursor(52, y);
  tft.print(":");

  tft.setTextColor(culMin, ST77XX_BLACK);
  tft.setCursor(65, y);
  if (min < 10) tft.print("0");
  tft.print(min);

  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.setCursor(97, y);
  tft.print(":");

  tft.setTextColor(culSec, ST77XX_BLACK);
  tft.setCursor(110, y);
  if (sec < 10) tft.print("0");
  tft.print(sec);
}

// Actualizeaza valorile dinamice in modul avansat
void actualizeazaModAvansat() {
  bool activ0 = (inEditare && campSelectat == 0);
  bool activ1 = (inEditare && campSelectat == 1);

  deseneazaValoriTimp(60, intervalOre, intervalMin, intervalSec, activ0, subCampCurent);
  deseneazaValoriTimp(130, duratOre, duratMin, duratSec, activ1, subCampCurent);

  int pwmVal = map(putereProc, 0, 100, 30, 126);
  uint16_t culPutere = (inEditare && campSelectat == 2) ? ST77XX_YELLOW : ST77XX_GREEN;
  tft.setTextColor(culPutere, ST77XX_BLACK);
  tft.setTextSize(2);
  tft.setCursor(20, 200);
  tft.print(putereProc);
  tft.print("% (PWM:");
  tft.print(pwmVal);
  tft.print(")   ");

  // Redeseneaza chenarele
  tft.drawRect(5, 35, 310, 55, culoareCamp(0));
  tft.drawRect(5, 105, 310, 55, culoareCamp(1));
  tft.drawRect(5, 175, 310, 55, culoareCamp(2));
}

// Verifica ca niciun camp din modul avansat nu e zero
bool configuratieValida() {
  bool intervalZero = (intervalOre == 0 && intervalMin == 0 && intervalSec == 0);
  bool duratZero = (duratOre == 0 && duratMin == 0 && duratSec == 0);
  bool putereZero = (putereProc == 0);
  return !(intervalZero || duratZero || putereZero);
}

// Punct de intrare principal
int main(void) {
  init();

  Serial.begin(9600);

  // Configureaza pinii butoanelor ca intrari cu pull-up intern
  DDRD &= ~((1 << DDD4) | (1 << DDD5));
  PORTD |= (1 << PORTD4) | (1 << PORTD5);

  sei();

  tft.init(240, 320);
  tft.setRotation(3);

  tft.invertDisplay(false);
  dht.begin();

  pinMode(TFT_CS, OUTPUT);
  digitalWrite(TFT_CS, HIGH);
  delay(10);

  if (!radio.begin()) {
    Serial.println("EROARE: Modulul NRF24 nu raspunde!");
    while (1);
  }

  radio.openWritingPipe(adresa);
  radio.setPALevel(RF24_PA_MIN);
  radio.stopListening();

  unsigned long ultimulTimpSenzor = 0;

  while (1) {
    unsigned long acum = millis();

    // 1. Citire senzor si logica termostat
    if (acum - ultimulTimpSenzor > 2000) {
      float citire = dht.readTemperature();
      if (!isnan(citire))
        tempReala = citire;

      byte putereDeTrimis = 0;
      if (modActiv == 0) {
        // Mod simplu: control proportional cu histerezis
        if (incalzirePornita) {
          if (tempReala >= tempSetata) {
            incalzirePornita = false;
            putereDeTrimis = 0;
          } else {
            float eroare = tempSetata - tempReala;
            float putereCalculata = eroare * 10;

            if (putereCalculata > 255) 
              putereCalculata = 255;
            if (putereCalculata < 30) 
              putereCalculata = 30;

            putereDeTrimis = (byte)putereCalculata;
          }
        }
        else {
          if (tempReala <= (tempSetata - histerezis)) {
            incalzirePornita = true;
            putereDeTrimis = 255;
          } else {
            putereDeTrimis = 0;
          }
        }
      } else {
        // Mod avansat: timer ciclic interval/incalzire
        unsigned long acumLocal = millis();

        // Initializeaza duratele in ms o singura data la prima rulare
        if (timpStartAvansat == 0) {
          timpStartAvansat = acumLocal;
          durataTotalaInterval_ms = ((unsigned long)intervalOre * 3600
                                  + (unsigned long)intervalMin * 60
                                  + intervalSec) * 1000UL;
          durataTotalaIncalzire_ms = ((unsigned long)duratOre * 3600
                                    + (unsigned long)duratMin * 60
                                    + duratSec) * 1000UL;
          avansat_incalzireActiva = false;
          Serial.println("AVANSAT: Timer pornit");
        }

        unsigned long timpScurs = acumLocal - timpStartAvansat;

        if (!avansat_incalzireActiva) {
          // Faza de asteptare: asteapta intervalul sa expire
          if (timpScurs >= durataTotalaInterval_ms) {
            avansat_incalzireActiva = true;
            timpStartAvansat = acumLocal;
            Serial.println("AVANSAT: Pornesc incalzirea!");
          }
          putereDeTrimis = 0;
        } else {
          // Faza de incalzire: aplica puterea setata
          if (timpScurs >= durataTotalaIncalzire_ms) {
            avansat_incalzireActiva = false;
            timpStartAvansat = acumLocal;
            Serial.println("AVANSAT: Opresc incalzirea, reincep intervalul");
          }
          putereDeTrimis = (avansat_incalzireActiva)
                          ? (byte)map(putereProc, 0, 100, 30, 126)
                          : 0;
        }
      }

      // Transmite puterea doar la schimbare sau la 10s
      if (modConfirmat && stareaCurenta == MENIU_PRINCIPAL) {
        
        int diferenta = abs((int)putereDeTrimis - (int)ultimaPutereTrimisa);
        
        if (diferenta > 10 || (acum - ultimulTimpTransmisie > 10000)) {
          
          Serial.println("===============================");
          Serial.println("[DEBUG] 1. Intru in blocul de transmisie");
          
          digitalWrite(TFT_CS, HIGH);
          
          radio.flush_tx();
          
          Serial.println("[DEBUG] 2. Arunc pachetul in aer...");
      
          radio.startWrite(&putereDeTrimis, sizeof(putereDeTrimis), true);
          
          Serial.println("[DEBUG] 3. Am scapat din transmisie! Merg la ecran.");

          ultimaPutereTrimisa = putereDeTrimis;
          ultimulTimpTransmisie = acum;
          Serial.println("===============================");
        }
      }

      ultimulTimpSenzor = acum;
    }

    // 2. Citire encoder rotativ
    int miscariRotita = 0;
    long pozitieNouaEncoder = roata.read() / 4;

    if (pozitieNouaEncoder > pozitieVecheEncoder) {
      miscariRotita = -1;
      pozitieVecheEncoder = pozitieNouaEncoder;
    } else if (pozitieNouaEncoder < pozitieVecheEncoder) {
      miscariRotita = 1;
      pozitieVecheEncoder = pozitieNouaEncoder;
    }

    // 3. Citire butoane cu debounce
    bool btnEnterApasat = false;
    bool btnBackApasat = false;

    if (!(PIND & (1 << PIND4))) {
      if (acum - ultimulClickENTER > 200) {
        btnEnterApasat = true;
        ultimulClickENTER = acum;
      }
    }

    if (!(PIND & (1 << PIND5))) {
      if (acum - ultimulClickBACK > 200) {
        btnBackApasat = true;
        ultimulClickBACK = acum;
      }
    }

    // 4. Masina de stari meniu

    // Meniu principal
    if (stareaCurenta == MENIU_PRINCIPAL) {
      if (cereRedesenareTotala) {
        deseneazaMeniuPrincipalStatic();
        cereRedesenareTotala = false;
      }

      if (miscariRotita > 0) 
        casutaSelectata = 1;
      if (miscariRotita < 0) 
        casutaSelectata = 0;

      if (btnBackApasat) {
        modActiv = (modActiv == 0) ? 1 : 0;
      }

      if (btnEnterApasat) {
        if (casutaSelectata == 0)
          stareaCurenta = MENIU_SIMPLU;
        else
          stareaCurenta = MENIU_AVANSAT;
        cereRedesenareTotala = true;
        roata.write(0);
        pozitieVecheEncoder = roata.read() / 4;
      }

      actualizeazaMeniuPrincipal();
    }

    // Mod simplu
    else if (stareaCurenta == MENIU_SIMPLU) {
      if (cereRedesenareTotala) {
        deseneazaModSimpluStatic();
        cereRedesenareTotala = false;
      }

      if (miscariRotita != 0) {
        tempSetata += (miscariRotita * 0.5);
        if (tempSetata < 10.0) 
          tempSetata = 10.0;
        if (tempSetata > 35.0) 
          tempSetata = 35.0;
      }

      if (btnBackApasat) {
        stareaCurenta = MENIU_PRINCIPAL;
        cereRedesenareTotala = true;
        roata.write(0);
        pozitieVecheEncoder = roata.read() / 4;
      }

      if (btnEnterApasat) {
        modActiv = 0;
        modConfirmat = true;
        stareaCurenta = MENIU_PRINCIPAL;
        cereRedesenareTotala = true;
        roata.write(0);
        pozitieVecheEncoder = roata.read() / 4;
      }

      actualizeazaModSimplu();
    }

    // Mod avansat
    else if (stareaCurenta == MENIU_AVANSAT) {
      if (cereRedesenareTotala) {
        deseneazaModAvansatStatic();
        actualizeazaModAvansat();
        cereRedesenareTotala = false;
      }

      if (!inEditare) {
        if (miscariRotita > 0) {
          campSelectat++;
          if (campSelectat > 2) 
            campSelectat = 2;
          cereRedesenareTotala = true;
        }
        if (miscariRotita < 0) {
          campSelectat--;
          if (campSelectat < 0) 
            campSelectat = 0;
          cereRedesenareTotala = true;
        }

        if (btnEnterApasat) {
          inEditare = true;
          subCampCurent = 0;
        }

        if (btnBackApasat) {
          inEditare = false;
          campSelectat = 0;
          stareaCurenta = MENIU_PRINCIPAL;
          cereRedesenareTotala = true;
          roata.write(0);
          pozitieVecheEncoder = roata.read() / 4;
        }
      }
      else {
        // Editare camp selectat
        bool schimbare = false;

        if (campSelectat == 0 || campSelectat == 1) {
          int* ore = (campSelectat == 0) ? &intervalOre : &duratOre;
          int* min = (campSelectat == 0) ? &intervalMin : &duratMin;
          int* sec = (campSelectat == 0) ? &intervalSec : &duratSec;

          if (subCampCurent == 0 && miscariRotita != 0) {
            *ore += miscariRotita;
            if (*ore < 0) 
              *ore = 23;
            if (*ore > 23) 
              *ore = 0;
            schimbare = true;
          } else if (subCampCurent == 1 && miscariRotita != 0) {
            *min += miscariRotita;
            if (*min < 0) 
              *min = 59;
            if (*min > 59) 
              *min = 0;
            schimbare = true;
          } else if (subCampCurent == 2 && miscariRotita != 0) {
            *sec += miscariRotita;
            if (*sec < 0) 
              *sec = 59;
            if (*sec > 59) 
              *sec = 0;
            schimbare = true;
          }

          if (btnBackApasat) {
            subCampCurent++;
            if (subCampCurent > 2) {
              subCampCurent = 0;
              inEditare = false;
            }
            schimbare = true;
          }
        }
        else if (campSelectat == 2) {
          if (miscariRotita != 0) {
            putereProc += miscariRotita;
            if (putereProc < 0) 
              putereProc = 0;
            if (putereProc > 100) 
              putereProc = 100;
            schimbare = true;
          }

          if (btnBackApasat) {
            inEditare = false;
            schimbare = true;
          }

          if (btnEnterApasat) {
            inEditare = false;
            if (configuratieValida()) {
              modActiv = 1;
              modConfirmat = true;
              timpStartAvansat = 0;
              campSelectat = 0;
              stareaCurenta = MENIU_PRINCIPAL;
              cereRedesenareTotala = true;
              roata.write(0);
              pozitieVecheEncoder = roata.read() / 4;
            } else {
              stareaCurenta = MENIU_AVANSAT_EROARE;
              cereRedesenareTotala = true;
            }
          }
        }

        // Redeseneaza doar daca s-a schimbat ceva
        if (schimbare) {
          actualizeazaModAvansat();
        }
      }
    }

    // Ecran de eroare configuratie invalida
    else if (stareaCurenta == MENIU_AVANSAT_EROARE) {
      if (cereRedesenareTotala) {
        tft.fillScreen(ST77XX_BLACK);

        tft.setTextColor(ST77XX_RED);
        tft.setTextSize(2);
        tft.setCursor(60, 70);
        tft.print("! EROARE !");

        tft.setCursor(10, 105);
        tft.print("Configuratie");
        tft.setCursor(10, 130);
        tft.print("indisponibila!");

        tft.setTextColor(ST77XX_WHITE);
        tft.setTextSize(1);
        tft.setCursor(20, 175);
        tft.print("Toate campurile trebuie");
        tft.setCursor(20, 190);
        tft.print("sa fie completate (> 0).");

        tft.setTextColor(ST77XX_CYAN);
        tft.setCursor(50, 220);
        tft.print("Apasa orice buton...");
        cereRedesenareTotala = false;
      }

      if (btnEnterApasat || btnBackApasat) {
        stareaCurenta = MENIU_AVANSAT;
        cereRedesenareTotala = true;
      }
    }

  }

  return 0;
}
