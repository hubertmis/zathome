## Unreleased

### 0.3.1
* Fix spelling in lights menu
* Invert sliders in lights control screen

### 0.3.0+0
* Add control of RGBW leds in the apartment (hardcoded now)

### 0.2.2+0
* Fixed touchscreen experience on clock screen

### 0.2.1+0
* Fixed handling long CoAP options like host URI

### 0.2.0+0
* Updated NCS to 1.8.0

### 0.1.0+0
* Updated NCS to 1.7.0

### 0.0.3+0
* Updated NCS to 1.6.0

### 0.0.2+0
* Added ventilation management

### 0.0.1+34
* Build based on NCS 1.5.99 downloaded 13.03.2021

### 0.0.1+33
* Get time from SNTP

### 0.0.1+32
* Do not display local setting if output is not available

### 0.0.1+31
* Preparation to forbid unsecure access outside local network
  To be finished when Zephyr implements ULA address verification
  and `SO_PROTOCOL` option for `getsockopt()`.

### 0.0.1+30
* Start coaps (PSK) in parallel with coap

### 0.0.1+29
* Implement Service Discovery server and client

### 0.0.1+27
* Implement second temperature resource

### 0.0.1+26
* Use resource name from provisioning data

### 0.0.1+25
* Fixed overflow in PWM output calculations

### 0.0.1+24
* CoAP service to set provisioning data

### 0.0.1+23
* CoAP service to get provisioning data

### 0.0.1+22
* Removed OpenThread CoAP
* Moved Zephyr CoAP from port 5685 to default CoAP port 5683
* CoAP service to manage temperature settings

### 0.0.1+18
* No changes. Just version bump to verify FOTA

### 0.0.1+17:
* Buidling agains NCS 2021-01-24
