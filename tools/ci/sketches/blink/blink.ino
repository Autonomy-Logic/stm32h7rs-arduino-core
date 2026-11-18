// SPDX-License-Identifier: Apache-2.0
// Minimal sketch the CI compiles for every board of this core.

void setup() {
	Serial.begin(115200);
	pinMode(65, OUTPUT);
}

void loop() {
	digitalWrite(65, !digitalRead(65));
	Serial.println("tick");
	delay(500);
}
