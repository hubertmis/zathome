EESchema Schematic File Version 4
EELAYER 30 0
EELAYER END
$Descr A4 11693 8268
encoding utf-8
Sheet 1 1
Title ""
Date ""
Rev ""
Comp ""
Comment1 ""
Comment2 ""
Comment3 ""
Comment4 ""
$EndDescr
$Comp
L switch:SR086SG-G IC?
U 1 1 61912902
P 3500 3100
F 0 "IC?" H 4100 3365 50  0000 C CNN
F 1 "SR086SG-G" H 4100 3274 50  0000 C CNN
F 2 "SOIC127P600X170-9N" H 4550 3200 50  0001 L CNN
F 3 "http://ww1.microchip.com/downloads/en/DeviceDoc/20005544A.pdf" H 4550 3100 50  0001 L CNN
F 4 "Adj Switching Reg, 9V-50V, 100mA, SOIC W" H 4550 3000 50  0001 L CNN "Description"
F 5 "1.7" H 4550 2900 50  0001 L CNN "Height"
F 6 "Microchip" H 4550 2800 50  0001 L CNN "Manufacturer_Name"
F 7 "SR086SG-G" H 4550 2700 50  0001 L CNN "Manufacturer_Part_Number"
F 8 "689-SR086SG-G" H 4550 2600 50  0001 L CNN "Mouser Part Number"
F 9 "https://www.mouser.co.uk/ProductDetail/Microchip-Technology/SR086SG-G?qs=ph4zPCVRuvrOCUnBv7TdAA%3D%3D" H 4550 2500 50  0001 L CNN "Mouser Price/Stock"
F 10 "SR086SG-G" H 4550 2400 50  0001 L CNN "Arrow Part Number"
F 11 "https://www.arrow.com/en/products/sr086sg-g/microchip-technology" H 4550 2300 50  0001 L CNN "Arrow Price/Stock"
	1    3500 3100
	1    0    0    -1  
$EndComp
$Comp
L Diode_Bridge:MB4S D?
U 1 1 61914CF5
P 2150 2600
F 0 "D?" H 2494 2646 50  0000 L CNN
F 1 "MB4S" H 2494 2555 50  0000 L CNN
F 2 "Package_TO_SOT_SMD:TO-269AA" H 2300 2725 50  0001 L CNN
F 3 "http://www.vishay.com/docs/88661/mb2s.pdf" H 2150 2600 50  0001 C CNN
	1    2150 2600
	1    0    0    -1  
$EndComp
$Comp
L Regulator_Switching:LNK304D U?
U 1 1 61915E9A
P 5550 3750
F 0 "U?" H 5550 3475 50  0000 C CNN
F 1 "LNK304D" H 5550 3384 50  0000 C CNN
F 2 "Package_SO:PowerIntegrations_SO-8B" H 5550 3750 50  0001 C CIN
F 3 "http://www.powerint.com/sites/default/files/product-docs/lnk302_304-306.pdf" H 5550 3750 50  0001 C CNN
	1    5550 3750
	1    0    0    -1  
$EndComp
$Comp
L Device:L L?
U 1 1 619179C8
P 7300 3750
F 0 "L?" V 7490 3750 50  0000 C CNN
F 1 "L" V 7399 3750 50  0000 C CNN
F 2 "" H 7300 3750 50  0001 C CNN
F 3 "~" H 7300 3750 50  0001 C CNN
	1    7300 3750
	0    -1   -1   0   
$EndComp
$Comp
L Device:C C?
U 1 1 61918BE5
P 6100 3600
F 0 "C?" H 6215 3646 50  0000 L CNN
F 1 "C" H 6215 3555 50  0000 L CNN
F 2 "" H 6138 3450 50  0001 C CNN
F 3 "~" H 6100 3600 50  0001 C CNN
	1    6100 3600
	1    0    0    -1  
$EndComp
$Comp
L Device:R R?
U 1 1 6191980C
P 6400 3600
F 0 "R?" H 6470 3646 50  0000 L CNN
F 1 "R" H 6470 3555 50  0000 L CNN
F 2 "" V 6330 3600 50  0001 C CNN
F 3 "~" H 6400 3600 50  0001 C CNN
	1    6400 3600
	1    0    0    -1  
$EndComp
$Comp
L Device:CP C?
U 1 1 6191B6C3
P 6850 3600
F 0 "C?" H 6968 3646 50  0000 L CNN
F 1 "CP" H 6968 3555 50  0000 L CNN
F 2 "" H 6888 3450 50  0001 C CNN
F 3 "~" H 6850 3600 50  0001 C CNN
	1    6850 3600
	1    0    0    -1  
$EndComp
$Comp
L Device:R R?
U 1 1 6191CA76
P 6600 3200
F 0 "R?" V 6393 3200 50  0000 C CNN
F 1 "R" V 6484 3200 50  0000 C CNN
F 2 "" V 6530 3200 50  0001 C CNN
F 3 "~" H 6600 3200 50  0001 C CNN
	1    6600 3200
	0    1    1    0   
$EndComp
$Comp
L Device:D D?
U 1 1 6191D2D3
P 7300 3200
F 0 "D?" H 7300 3417 50  0000 C CNN
F 1 "D" H 7300 3326 50  0000 C CNN
F 2 "" H 7300 3200 50  0001 C CNN
F 3 "~" H 7300 3200 50  0001 C CNN
	1    7300 3200
	1    0    0    -1  
$EndComp
$EndSCHEMATC
