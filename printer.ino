#include <SPI.h>
#include <Ethernet.h>
#include <SD.h>

#include <SoftwareSerial.h>
#include <Thermal.h>
#include <Bounce.h>

// ------- Settings ---------------------------------------------------

const char *printerId = "abcdef123456"; // the unique ID for this printer.
byte mac[] = { 0x90, 0xA2, 0xDA, 0x00, 0x86, 0x67 }; // physical mac address

const char *printerType = "A2-bitmap"; // controls the format of the data sent from the server

const char* host = "printer.gofreerange.com"; // the host of the backend server
const unsigned int port = 80;
const unsigned long pollingDelay = 10000; // delay between polling requests (milliseconds)

const byte printer_TX_Pin = 9; // this is the yellow wire
const byte printer_RX_Pin = 8; // this is the green wire
const byte errorLED = 7;       // the red LED
const byte downloadLED = 6;    // the amber LED
const byte readyLED = 5;       // the green LED
const byte buttonPin = 3;      // the print button

// --------------------------------------------------------------------

// #define DEBUG
#ifdef DEBUG
#define debug(a) Serial.print(millis()); Serial.print(": "); Serial.println(a);
#define debug2(a, b) Serial.print(millis()); Serial.print(": "); Serial.print(a); Serial.println(b);
#else
#define debug(a)
#define debug2(a, b)
#endif

void initDiagnosticLEDs() {
  pinMode(errorLED, OUTPUT);
  pinMode(downloadLED, OUTPUT);
  pinMode(readyLED, OUTPUT);
  digitalWrite(errorLED, HIGH);
  digitalWrite(downloadLED, HIGH);
  digitalWrite(readyLED, HIGH);
  delay(1000);
  digitalWrite(errorLED, LOW);
  digitalWrite(downloadLED, LOW);
  digitalWrite(readyLED, LOW);
}

Thermal printer(printer_RX_Pin, printer_TX_Pin);
const byte postPrintFeed = 3;

void initPrinter() {
  printer.begin(150);
}

const byte SD_Pin = 4;
void initSD() {
  pinMode(SD_Pin, OUTPUT);
  SD.begin(SD_Pin);
}

EthernetClient client;
void initNetwork() {
  // start the Ethernet connection:
  if (Ethernet.begin(mac) == 0) {
    debug("DHCP Failed");
    // no point in carrying on, so do nothing forevermore:
    while(true);
  }
  delay(1000);
  // print your local IP address:
  debug2("IP address: ", Ethernet.localIP());
}

void setup(){
  Serial.begin(9600);

  initSD();
  initNetwork();
  initPrinter();
#ifdef DEBUG
  initDiagnosticLEDs();
#endif
}

boolean downloadWaiting = false;
char* cacheFilename = "TMP";

void checkForDownload() {
  unsigned long length = 0;
  unsigned long content_length = 0;

  if (SD.exists(cacheFilename)) SD.remove(cacheFilename);
  File cache = SD.open(cacheFilename, FILE_WRITE);

  debug2("Attempting to connect to ", host);
  if (client.connect(host, port)) {
    digitalWrite(downloadLED, HIGH);
    client.print("GET "); client.print("/printer/"); client.print(printerId); client.println(" HTTP/1.0");
    client.print("Host: "); client.print(host); client.print(":"); client.println(port);
    client.print("Accept: application/vnd.freerange.printer."); client.println(printerType);
    client.println();
    boolean parsingHeader = true;
#ifdef DEBUG
    unsigned long start = millis();
#endif
    while(client.connected()) {
      debug("Still connected");
      while(client.available()) {
        if (parsingHeader) {
          client.find("Content-Length: ");
          char c;
          while (isdigit(c = client.read())) {
            content_length = content_length*10 + (c - '0');
          }
          debug2("Content length was: ", content_length);
          client.find("\n\r\n"); // the first \r may already have been read above
          parsingHeader = false;
        } else {
          cache.write(client.read());
          length++;
        }
      }
      debug("No more data to read at the moment...");
    }

    debug("Server has disconnected");
    digitalWrite(downloadLED, LOW);
    // Close the connection, and flush any unwritten bytes to the cache.
    client.stop();
    cache.seek(0);
    boolean success = (content_length == length) && (content_length == cache.size());
    cache.close();

#ifdef DEBUG
    unsigned long duration = millis() - start;
    debug2("Total bytes: ", length);
    debug2("Duration: ", duration);
    debug2("Speed: ", length/(duration/1000.0)); // NB - floating point math increases sketch size by ~2k
#endif

    if (success) {
      if (content_length > 0) {
        downloadWaiting = true;
        digitalWrite(readyLED, HIGH);
      }
    } else {
      debug("Oh no, a failure.");
      digitalWrite(errorLED, HIGH);
      digitalWrite(downloadLED, HIGH);
    }
  } else {
    debug("Couldn't connect");
    byte i = 5;
    while(i--) {
      digitalWrite(errorLED, HIGH);
      delay(100);
      digitalWrite(errorLED, LOW);
      delay(100);
    }
  }
}

void printFromDownload() {
  File cache = SD.open(cacheFilename);
  printer.printBitmap(&cache);
  printer.feed(postPrintFeed);
  cache.close();
}

Bounce bouncer = Bounce(buttonPin, 5); // 5 millisecond debounce

void loop() {
  if (downloadWaiting) {
    bouncer.update();
    if (bouncer.read() == HIGH) {
      printFromDownload();
      downloadWaiting = false;
      digitalWrite(readyLED, LOW);
    }
  } else {
    delay(pollingDelay);
    checkForDownload();
  }
}