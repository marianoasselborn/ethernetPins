// Include libraries
#include <EtherCard.h>

// ethernet interface ip address
static byte myip[] = { 192,168,5,20 };
// gateway ip address
static byte gwip[] = { 192,168,5,1 };

// ethernet mac address - must be unique on your network
static byte mymac[] = { 0x74, 0x69, 0x69, 0x2D, 0x30, 0x31 };
// tcp/ip send and receive buffer
byte Ethernet::buffer[500];
static BufferFiller bfill;

/*
 * Pin configuration
 * Array uses key for pin number and value for mode, modify to suit your app
 */
int pins[14] = {
  OUTPUT, // pin 0 RX
  OUTPUT, // pin 1 TX
  OUTPUT, // pin 2
  OUTPUT, // pin 3 PWM
  OUTPUT, // pin 4
  OUTPUT, // pin 5 PWM
  OUTPUT, // pin 6 PWM
  OUTPUT, // pin 7
  OUTPUT, // pin 8
  OUTPUT, // pin 9 PWM
  OUTPUT, // pin 10 PWM
  OUTPUT, // pin 11 PWM
  OUTPUT, // pin 12
  OUTPUT, // pin 13
};

void setup() {
  Serial.begin(57600);
  Serial.println("[ethernetPins running]");
  
  if (ether.begin(sizeof Ethernet::buffer, mymac) == 0) 
    Serial.println( "Failed to access Ethernet controller");
  
  // setup lan
  ether.staticSetup(myip, gwip);

  ether.printIp("IP:  ", ether.myip);
  ether.printIp("GW:  ", ether.gwip);  
  ether.printIp("DNS: ", ether.dnsip);
  
  for (int i = 0; i < sizeof(pins) -1; i++) {
    // initialize only non-reserved pins
    if (!isReserved(i)) {
      pinMode(i, pins[i]);
    }
  }
}

void loop() {
  word pos = ether.packetLoop(ether.packetReceive());
  
  // check if valid tcp data is received
  if (pos) {
      bfill = ether.tcpOffset();
      char* data = (char *) Ethernet::buffer + pos;
      // receive buf hasn't been clobbered by reply yet
      if (strncmp("GET / ", data, 6) == 0) {
        homePage(bfill, data);
      }
      else if (strncmp("GET /sts ", data, 6) == 0) {
        statusPage(bfill, data);
      }
      else if (strncmp("GET /cmd ", data, 6) == 0) {
        postCmd(bfill, data);
      }
      else {
        notFound(bfill, data);
      }
      // send web page data
      ether.httpServerReply(bfill.position());
  }
}

/*
 * Callback functions
 * Respond to a given path on the main app routing entry point
 */

static void homePage(BufferFiller& buf, const char* data) {
  bfill.emit_p(PSTR(
    "HTTP/1.0 301 Moved Permanently\r\n"
    "Content-Type: text/html\r\n"
    "Location: /sts\r\n"
    "\r\n"
    "<h1>Redirecting...</h1>"));
}

static void statusPage(BufferFiller& buf, const char* data) {
  /*
   * Currently accepting arguments:
   * m - mode: 1 for digital pins, 0 for analog pins (A0 to A5)
   * p - pin: pin number
   */
  if (data[8] == '?') {
    int mode  = getArgumentValue(data, 9, "m");
    int pin   = getArgumentValue(data, 9, "p");
    
    if (pin >= 0 && pin <= 13 && !isReserved(pin)) {
      // digital pin status
      if (mode == 1) {
        if (pins[pin] == INPUT || (pins[pin] == OUTPUT && !isPwm(pin))) {
          // digital pin set to INPUT or OUTPUT non-pwm
          // send json response back to requester
          provideFeedback(bfill, mode, pin, NULL);
        }
        else {
          // pwm pins do not allow digitalRead()
          badRequest(bfill);
        }
      }
    }
    else {
      // pin doesn't exist or is reserved and can't be used
      badRequest(bfill);
    }
  }
}

static void postCmd(BufferFiller& buf, const char* data) {
  /*
   * Currently accepting arguments:
   * m - mode: 1 for digital, 0 for analog
   * p - pin: pin number
   * v - value: if mode is digital, 1 for high and 0 for low; if mode is analog, an int from 0 to 255.
   */
  if (data[8] == '?') {
    int mode  = getArgumentValue(data, 9, "m");
    int pin   = getArgumentValue(data, 9, "p");
    int value = getArgumentValue(data, 9, "v");
    
    if (pin >= 0 && pin <= 13 && !isReserved(pin)) {
      // digital command
      if (mode == 1) {
        if (value == 1) {
          digitalWrite(pin, HIGH);
        }
        else if (value == 0) {
          digitalWrite(pin, LOW);
        }
        else {
          // ambiguous value for digital command
          badRequest(bfill);
        }
      }
      // analog command
      else if (mode == 0){
        // to control a pin in analog mode the value should be in range and the pin should support pwm
        if (value >= 0 && value <= 255 && isPwm(pin)) {
          analogWrite(pin, value);
        }
        else {
          // either out of range value or pin isn't pwm
          badRequest(bfill);
        }
      }
      else {
        // ambiguous mode
        badRequest(bfill);
      }
      // send json response back to requester
      provideFeedback(bfill, mode, pin, value);
    }
    else {
      // pin doesn't exist or is reserved and can't be used
      badRequest(bfill);
    }
  }
  // missplaced query param (?)
  else {
    badRequest(bfill);
  }
}

static void notFound(BufferFiller& buf, const char* data) {
  bfill.emit_p(PSTR(
    "HTTP/1.0 404 Not Found\r\n"
    "Content-Type: text/html\r\n"
    "\r\n"
    "<h1>404 Not Found</h1>")); 
}

static void badRequest(BufferFiller& buf) {
  bfill.emit_p(PSTR(
    "HTTP/1.0 400 Bad Request\r\n"
    "Content-Type: text/html\r\n"
    "\r\n"
    "<h1>400 Bad Request</h1>"));
  
  deliverResponse(bfill);
}

static void provideFeedback(BufferFiller& buf, int mode, int pin, int state) {
  // digitalRead gets mad when used on a currently pwm'ed pin
  switch (mode) {
    case 1 :
      state = digitalRead(pin);
      break;
    case 0 :
      break;
  }
  // debugging prints
  Serial.println(mode);
  Serial.println(pin);
  Serial.println(state);
  
  bfill.emit_p(PSTR("{ \"pin\" : \"$D\", \"state\" : \"$D\" }"), pin, state);
  deliverResponse(bfill);
}

static void deliverResponse(BufferFiller& buf) {
  // send web page data
  ether.httpServerReply(bfill.position());
}

/*
 * Utility functions
 * Helper functions to abstract logic
 */
static int getArgumentValue(const char* data, int offset, const char* key) {
  char temp[10];
  int value = -1;
  if (ether.findKeyVal(data + offset, temp, sizeof temp, key) > 0) {
    value = atoi(temp);
  }
  return value;
}

static bool isPwm(int pin) {
  // pwm pins
  int pwmPins[6] = {3, 5, 6, 9, 10, 11};
  for (int i = 0; i < sizeof(pwmPins) -1; i++) {
    if (pin == pwmPins[i]) {
      return true;
    }
  }
  return false;
}

static bool isReserved(int pin) {
  // reserved for ethernet shield
  int reservedPins[6] = {10, 11, 12};
  for (int i = 0; i < sizeof(reservedPins) -1; i++) {
    if (pin == reservedPins[i]) {
      return true;
    }
  }
  return false;
}
