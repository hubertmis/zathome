## Unreleased

### 0.1.0
* Port to NCS v3.4.1
* Experiment with multiple ADC filter algorithms, selected sliding window
* Disable GPIO event detection if ADC detection is enabled
* Modify threads priority so that ADC filtering doesn't starve network activity

#### Known bugs
* CoAP per-device resources fail without d=[0|1] attribute because of cbor map search helper failing to find the same key in a map twice

### 0.0.5
* Send non-confirmable requests if battery-operated

### 0.0.4
* Update common libraries to newer version
* Fix continuous service discovery routine

### 0.0.1
* Hardware switch handling
* OpenThread connection
* FOTA service
* Provisioning stored in settings
* Discovery of actuators
* Request preset settings from actuators
