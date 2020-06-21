#include <Arduino.h>
#include "DHT.h"
#include <SoftwareSerial.h>
#include "TinyGPS++.h"

#define GPS_SAMPLE_DURATION 1000

#define ONEWIRE_PIN 2
#define DHT_TYPE DHT22

/*****************************************************************
 *  GPS PINS
 ****************************************************************/
#define GPS_RX_PIN 6
#define GPS_TX_PIN 7

/****************************************************************
 * NODEMCU PINS
 * **************************************************************/
#define NodeMCU_RX 11
#define NodeMCU_TX  10

/*****************************************************************
 * DHT declaration                                               *
 *****************************************************************/
DHT dht(ONEWIRE_PIN, DHT_TYPE);

/******************************************************************
 *  NodeMCU SoftwareSerial DECLARATION
 * ****************************************************************/
SoftwareSerial NodeMCU(NodeMCU_TX,NodeMCU_RX);

/*****************************************************************
 *  GPS DECLARATION
 * ****************************************************************/
SoftwareSerial serialGPS(GPS_TX_PIN,GPS_RX_PIN);
TinyGPSPlus gps;


/******************************************************************
 * PMS DECLARATION
 * **************************************************************/
SoftwareSerial serialSDS(5,4);

/******************************************************************
 *  DHT VARIABLES
 * ***************************************************************/
float dht_humidity = 0.0;
float dht_temperature = 0.0;
bool read_dht = false;

/******************************************************************
 *  GPS VARIABLES
 * ****************************************************************/
double last_value_GPS_lat = -200.0;
double last_value_GPS_lon = -200.0;
double last_value_GPS_alt = -1000.0;
String last_value_GPS_date;
String last_value_GPS_time;

bool read_gps = false;
uint64_t gps_time = 0;

/*****************************************************************
 * Payload variables
 * **************************************************************/
String received_command;
uint32_t payload_time = 0;

/****************************************************************
 * PMS VARIABLES
 * **************************************************************/
const unsigned long WARMUPTIME_SDS_MS = 15000;// time needed to "warm up" the sensor before we can take the first measurement
const unsigned long READINGTIME_SDS_MS = 5000;	
bool is_PMS_running = false;
bool send_now = false;
bool read_pms = false;

namespace cfg {
  unsigned long sending_intervall_ms = 145000;
}

enum class PmSensorCmd {
	Start,
	Stop,
	ContinuousMode
};

unsigned long starttime;
unsigned long starttime_SDS;

int pms_pm1_sum = 0;
int pms_pm10_sum = 0;
int pms_pm25_sum = 0;
int pms_val_count = 0;
int pms_pm1_max = 0;
int pms_pm1_min = 20000;
int pms_pm10_max = 0;
int pms_pm10_min = 20000;
int pms_pm25_max = 0;
int pms_pm25_min = 20000;


float last_value_PMS_P0 = -1.0;
float last_value_PMS_P1 = -1.0;
float last_value_PMS_P2 = -1.0;

unsigned long act_milli;

template<typename T, size_t N> constexpr size_t array_num_elements(const T(&)[N]) {
	return N;
}

#define msSince(timestamp_before) (act_milli - (timestamp_before))

/****************************************************************
 * SEND SERIAL DATA
 * **************************************************************/
void send_serial_data(String data){
	NodeMCU.println(data);
}

/*****************************************************************
 * send Plantower PMS sensor command start, stop, cont. mode     *
 *****************************************************************/
bool PMS_cmd(PmSensorCmd cmd) {
	static constexpr uint8_t start_cmd[] PROGMEM = {
		0x42, 0x4D, 0xE4, 0x00, 0x01, 0x01, 0x74
	};
	static constexpr uint8_t stop_cmd[] PROGMEM = {
		0x42, 0x4D, 0xE4, 0x00, 0x00, 0x01, 0x73
	};
	static constexpr uint8_t continuous_mode_cmd[] PROGMEM = {
		0x42, 0x4D, 0xE1, 0x00, 0x01, 0x01, 0x71
	};
	constexpr uint8_t cmd_len = array_num_elements(start_cmd);

	uint8_t buf[cmd_len];
	switch (cmd) {
	case PmSensorCmd::Start:
		memcpy_P(buf, start_cmd, cmd_len);
		break;
	case PmSensorCmd::Stop:
		memcpy_P(buf, stop_cmd, cmd_len);
		break;
	case PmSensorCmd::ContinuousMode:
		memcpy_P(buf, continuous_mode_cmd, cmd_len);
		break;
	}
	serialSDS.write(buf, cmd_len);
	return cmd != PmSensorCmd::Stop;
}

String package_PMS_payload(){
	
	String pms_data = "PMS#"+String(last_value_PMS_P0)+","+String(last_value_PMS_P1)+","+String(last_value_PMS_P2)+"*";
	return pms_data;
}

void send_PMS_payload(){
	String pms_payload = package_PMS_payload();
	send_serial_data(pms_payload);
}

/*****************************************************************
 * read Plantronic PM sensor sensor values                       *
 *****************************************************************/
void fetchSensorPMS() {
	char buffer;
	int value;
	int len = 0;
	int pm1_serial = 0;
	int pm10_serial = 0;
	int pm25_serial = 0;
	int checksum_is = 0;
	int checksum_should = 0;
	bool checksum_ok = false;
	int frame_len = 24;				// min. frame length

	if (msSince(starttime) < (cfg::sending_intervall_ms - (WARMUPTIME_SDS_MS + READINGTIME_SDS_MS))) {
		if (is_PMS_running) {
			is_PMS_running = PMS_cmd(PmSensorCmd::Stop);
		}
	} else {
		if (! is_PMS_running) {
			is_PMS_running = PMS_cmd(PmSensorCmd::Start);
		}

		serialSDS.listen();
       delay(50);
		while (serialSDS.available() > 0) {
			buffer = serialSDS.read();
			
			value = int(buffer);
			switch (len) {
			case 0:
				if (value != 66) {
					len = -1;
				};
				break;
			case 1:
				if (value != 77) {
					len = -1;
				};
				break;
			case 2:
				checksum_is = value;
				break;
			case 3:
				frame_len = value + 4;
				break;
			case 10:
				pm1_serial += ( value << 8);
				break;
			case 11:
				pm1_serial += value;
				break;
			case 12:
				pm25_serial = ( value << 8);
				break;
			case 13:
				pm25_serial += value;
				break;
			case 14:
				pm10_serial = ( value << 8);
				break;
			case 15:
				pm10_serial += value;
				break;
			case 22:
				if (frame_len == 24) {
					checksum_should = ( value << 8 );
				};
				break;
			case 23:
				if (frame_len == 24) {
					checksum_should += value;
				};
				break;
			case 30:
				checksum_should = ( value << 8 );
				break;
			case 31:
				checksum_should += value;
				break;
			}
			if ((len > 2) && (len < (frame_len - 2))) { checksum_is += value; }
			len++;
			if (len == frame_len) {
				
				if (checksum_should == (checksum_is + 143)) {
					checksum_ok = true;
				} else {
					len = 0;
				};
				if (checksum_ok) {
					if ((! isnan(pm1_serial)) && (! isnan(pm10_serial)) && (! isnan(pm25_serial))) {
						pms_pm1_sum += pm1_serial;
						pms_pm10_sum += pm10_serial;
						pms_pm25_sum += pm25_serial;
						if (pms_pm1_min > pm1_serial) {
							pms_pm1_min = pm1_serial;
						}
						if (pms_pm1_max < pm1_serial) {
							pms_pm1_max = pm1_serial;
						}
						if (pms_pm25_min > pm25_serial) {
							pms_pm25_min = pm25_serial;
						}
						if (pms_pm25_max < pm25_serial) {
							pms_pm25_max = pm25_serial;
						}
						if (pms_pm10_min > pm10_serial) {
							pms_pm10_min = pm10_serial;
						}
						if (pms_pm10_max < pm10_serial) {
							pms_pm10_max = pm10_serial;
						}
					
						pms_val_count++;
					}
					len = 0;
					checksum_ok = false;
					pm1_serial = 0;
					pm10_serial = 0;
					pm25_serial = 0;
					checksum_is = 0;
				}
			}
			
		}

	}

if(send_now){

		last_value_PMS_P0 = -1;
		last_value_PMS_P1 = -1;
		last_value_PMS_P2 = -1;
		if (pms_val_count > 2) {
			pms_pm1_sum = pms_pm1_sum - pms_pm1_min - pms_pm1_max;
			pms_pm10_sum = pms_pm10_sum - pms_pm10_min - pms_pm10_max;
			pms_pm25_sum = pms_pm25_sum - pms_pm25_min - pms_pm25_max;
			pms_val_count = pms_val_count - 2;
		}
		if (pms_val_count > 0) {
			last_value_PMS_P0 = float(pms_pm1_sum) / float(pms_val_count);
			last_value_PMS_P1 = float(pms_pm10_sum) / float(pms_val_count);
			last_value_PMS_P2 = float(pms_pm25_sum) / float(pms_val_count);
			Serial.println(last_value_PMS_P0);
			Serial.println(last_value_PMS_P1);
			Serial.println(last_value_PMS_P2);
			
		}
		pms_pm1_sum = 0;
		pms_pm10_sum = 0;
		pms_pm25_sum = 0;
		pms_val_count = 0;
		pms_pm1_max = 0;
		pms_pm1_min = 20000;
		pms_pm10_max = 0;
		pms_pm10_min = 20000;
		pms_pm25_max = 0;
		pms_pm25_min = 20000;

		if (cfg::sending_intervall_ms > (WARMUPTIME_SDS_MS + READINGTIME_SDS_MS)) {
			is_PMS_running = PMS_cmd(PmSensorCmd::Stop);
		}

	}

}


/***************************************************************
 * Package DHT Sensor Values for transmission
 * *************************************************************/
String package_DHT_payload(){
  String dht_data;
  dht_data = "DHT#"+String(dht_temperature)+","+String(dht_humidity)+"*";
  return dht_data;
}


/*****************************************************
 * READ DHT SENSOR VALUES
 ********************************************************/
void fetchSensorDHT(float &t, float &h){

  	t = dht.readTemperature();
	  h = dht.readHumidity();

		if (isnan(t) || isnan(h)) {
			delay(100);
			t = dht.readTemperature(false);
			h = dht.readHumidity();
		}
		if (isnan(t) || isnan(h)) {
		Serial.println(F("DHT11/DHT22 read failed"));
		}

	String dht_values = package_DHT_payload();
	send_serial_data(dht_values);
	read_dht = false;

}

/***************************************************************
 * Package  GPS Sensor Values for transmission
 * *************************************************************/
String package_GPS_payload(){
  String gps_data;
  gps_data ="GPS#"+String(last_value_GPS_lat,6)+","+String(last_value_GPS_lon,6)+","+String(last_value_GPS_alt)+","+last_value_GPS_date+","+last_value_GPS_time+"*&";
  return gps_data;
}

/*****************************************************************
 * read GPS sensor values                                        *
 *****************************************************************/

void fetchSensorGPS()
{
     
 	if (gps.location.isUpdated()) {
		if (gps.location.isValid()) {
			last_value_GPS_lat = gps.location.lat();
			last_value_GPS_lon = gps.location.lng();
		} else {
			last_value_GPS_lat = -200;
			last_value_GPS_lon = -200;
			Serial.println(F("Lat/Lng INVALID"));
		}
		if (gps.altitude.isValid()) {
			last_value_GPS_alt = gps.altitude.meters();
			String gps_alt(last_value_GPS_lat);
		} else {
			last_value_GPS_alt = -1000;
			Serial.println(F("Altitude INVALID"));
		}
  	}
		if (gps.date.isValid()) {
			char gps_date[16];
			snprintf_P(gps_date, sizeof(gps_date), PSTR("%02d/%02d/%04d"),
					gps.date.month(), gps.date.day(), gps.date.year());
			last_value_GPS_date = gps_date;
		} else {
			Serial.println(F("Date INVALID"));
		}
		if (gps.time.isValid()) {
			char gps_time[20];
			snprintf_P(gps_time, sizeof(gps_time), PSTR("%02d:%02d:%02d.%02d"),
				gps.time.hour(), gps.time.minute(), gps.time.second(), gps.time.centisecond());
			last_value_GPS_time = gps_time;
		} else {
			Serial.println(F("Time: INVALID"));
		}


		String gps_values = package_GPS_payload();
		send_serial_data(gps_values);
		read_gps = false;
	                                            
}



void setup() {
	
	pinMode(NodeMCU_RX, OUTPUT);
	pinMode(GPS_RX_PIN, OUTPUT);
	
	pinMode(NodeMCU_TX, INPUT);
	pinMode(GPS_TX_PIN, INPUT);
	
	
	Serial.begin(9600);
	NodeMCU.begin(9600);
	serialGPS.begin(9600); //set GPS baudrate
  	serialSDS.begin(9600);
	dht.begin();
	
	Serial.print(F("Testing TinyGPS++ library v. "));
  	Serial.println(TinyGPSPlus::libraryVersion());

	starttime = millis();

}

void loop() {


	act_milli = millis();
	send_now = msSince(starttime) > cfg::sending_intervall_ms;


	NodeMCU.listen();
	delay(50);

	//Check if there is any data available from the NodeMCU
	while(NodeMCU.available() > 0){
		received_command = NodeMCU.readString();
		Serial.println(received_command);
			if(received_command.indexOf("fetchSensorGPS") >= 0){
					read_gps = true;
			}
			if(received_command.indexOf("fetchSensorDHT") >= 0){
					read_dht = true;
			}
			if(received_command.indexOf("fetchSensorPMS") >= 0){
				send_PMS_payload();
			}
		}



	if(read_gps){
		gps_time = millis();
		serialGPS.listen();
		delay(50);
		
		while((millis() - gps_time) < GPS_SAMPLE_DURATION){
			//Check if there is available data on GPS serial port
			while (serialGPS.available() > 0){
				gps.encode(serialGPS.read());
			}
		}
		fetchSensorGPS();
	}

	if(read_dht){
		fetchSensorDHT(dht_temperature,dht_humidity);
	}

	fetchSensorPMS();

	if(send_now){
		Serial.println("Time to sample");
		starttime = millis();
	}

  

}
