/*
 * ESP32-S3 NMEA2000 Rudder Simulator
 *
 * Reads rudder angle from a potentiometer (GPIO4) and transmits
 * PGN 127245 (Rudder) on the NMEA2000 network.
 *
 * Hardware pinout:
 *   POT (potentiometer) -> GPIO4
 *   TWAI TX (CAN H)     -> GPIO17
 *   TWAI RX (CAN L)     -> GPIO16
 */

#define ESP32_CAN_TX_PIN GPIO_NUM_17 // CAN (TWAI) transmit pin
#define ESP32_CAN_RX_PIN GPIO_NUM_16 // CAN (TWAI) receive pin
#define USE_N2K_CAN 7                // Force ESP32 CAN library

#include "NMEA2000_CAN.h" // Auto-selects CAN library and creates NMEA2000 object
#include "N2kMessages.h"  // NMEA2000 message definitions

// Enable diagnostic serial output. Comment out to disable.
#define ENABLE_SERIAL_OUTPUT

// Potentiometer pin for rudder angle simulation
#define POT_POTENTIOMETER 4

// PGN messages this device transmits
const unsigned long TransmitMessages[] PROGMEM = {126996L, 126998L, 127245L, 0};

// Rudder message scheduler (period 200 ms)
tN2kSyncScheduler RudderScheduler(false, 200, 0);

// *****************************************************************************
void OnN2kOpen()
{
  RudderScheduler.UpdateNextTime();
}

// *****************************************************************************
// Read rudder angle from the potentiometer
// Mapping: potentiometer 0-4095 -> angle -40..+40 degrees (port/starboard)
double ReadRudderAngle()
{
  int pot = analogRead(POT_POTENTIOMETER);

  // Convert: 0 -> -40°, 2047 -> 0°, 4095 -> +40°
  double angleDeg = map(pot, 0, 4095, -40, 40);
  double angleRad = DegToRad(angleDeg); // Convert to radians for NMEA2000

  return angleRad;
}

// *****************************************************************************
void setup()
{
  Serial.begin(115200);

  // ADC configuration for ESP32-S3
  analogReadResolution(12);                             // 12-bit (0-4095)
  analogSetPinAttenuation(POT_POTENTIOMETER, ADC_11db); // 0-3.3V range for this pin

  // Set product information
  NMEA2000.SetProductInformation("00000001",         // Serial number
                                 100,                // Product code
                                 "Rudder Simulator", // Model name
                                 "1.0.0.0",          // Software version
                                 "1.0.0.0"           // Model version
  );

  // Set device information
  NMEA2000.SetDeviceInformation(112233, // Unique number
                                160,    // Device function = Steering Control
                                75,     // Device class = Interface
                                2046    // Manufacturer code 2046 (VodoplaV)
  );

  // Set manufacturer information for PGN 126998
  NMEA2000.SetConfigurationInformation("VodoplaV",
                                       "Rudder Simulator v1.0");

  NMEA2000.SetForwardStream(&Serial);
  NMEA2000.SetMode(tNMEA2000::N2km_NodeOnly, 22);
  NMEA2000.EnableForward(false);
  NMEA2000.ExtendTransmitMessages(TransmitMessages);
  NMEA2000.SetOnOpen(OnN2kOpen);
  NMEA2000.Open();

  // Small delay to stabilize the CAN bus after opening
  delay(100);
}

// *****************************************************************************
// Calculate 29-bit CAN ID for NMEA2000
uint32_t CalcCANID(uint8_t priority, uint8_t dp, uint8_t pf, uint8_t ps, uint8_t source)
{
  return ((uint32_t)priority << 26) | ((uint32_t)dp << 24) |
         ((uint32_t)pf << 16) | ((uint32_t)ps << 8) | source;
}

// Diagnostic variables — only allocated when serial output is enabled
#ifdef ENABLE_SERIAL_OUTPUT
static uint32_t lastN2kCANID = 0;
static uint8_t lastN2kData[8];
static uint8_t lastN2kDataLen = 0;
#endif

void loop()
{
  // Process incoming NMEA2000 messages (ISO Request, Address Claim, etc.)
  NMEA2000.ParseMessages();

  if (RudderScheduler.IsTime())
  {
    RudderScheduler.UpdateNextTime();

    tN2kMsg N2kMsg;
    double rudderAngle = ReadRudderAngle(); // Angle in radians

    // Send PGN 127245 (Rudder)
    // Parameters: angle in radians, instance=0, no direction command
    SetN2kRudder(N2kMsg, rudderAngle, 0, N2kRDO_NoDirectionOrder, N2kDoubleNA);
    NMEA2000.SendMsg(N2kMsg);

    // Save the sent message for diagnostic display
#ifdef ENABLE_SERIAL_OUTPUT
    lastN2kCANID = CalcCANID(N2kMsg.Priority, 1,
                             (uint8_t)(N2kMsg.PGN >> 8),
                             (uint8_t)N2kMsg.PGN,
                             N2kMsg.Source);
    lastN2kDataLen = N2kMsg.DataLen;
    uint8_t copyLen = N2kMsg.DataLen < 8 ? N2kMsg.DataLen : 8;
    memcpy(lastN2kData, N2kMsg.Data, copyLen);
#endif
  }

  // Diagnostic output block
#ifdef ENABLE_SERIAL_OUTPUT
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 500)
  {
    lastPrint = millis();
    int raw = analogRead(POT_POTENTIOMETER);
    double deg = map(raw, 0, 4095, -40, 40);
    // Format: CAN_ID(8 hex) flags(5 hex) data(16 hex)  [ADC: angle°]
    Serial.printf("%08lX 0FF00 ", lastN2kCANID);
    for (uint8_t i = 0; i < lastN2kDataLen; i++)
    {
      Serial.printf("%02X", lastN2kData[i]);
    }
    for (uint8_t i = lastN2kDataLen; i < 8; i++)
    {
      Serial.print("FF");
    }
    Serial.printf("  [%d: %.1f°]\n", raw, deg);
  }
#endif
}
