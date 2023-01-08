GreenPak(SLG46826V-DIP)サンプルデータ

<用途>
mbed(LPC1768)を使ったGreenPakへの書き込みソフトの動作確認用です。

<動作内容>
SLG46826V-DIP の15Pin,16Pin にランダムにH/Lを出力するデータです。
15Pin,16PinにLEDを接続して点滅させることができます。

  15Pin --- R(1k) --- LED(K-A) --- Vcc(3.3V)
  16Pin --- R(1k) --- LED(K-A) --- Vcc(3.3V)

EEPROMデータは書き込みを確認するためのものでデータには意味はありません。

EEPROM address data
0x00 		0x00
0x01		0x01
0x02		0x02
0x03		0x03
それ以外	0x00

<HEX file>
NVM用: NVM.hex
EEPROM用: EEPROM.hex

<補足>
NVM.hexをRESISTER領域に直接書き込んでも動作します。
この場合は、GreenPakの電源を切ると書き込んだ内容は消えます。
電源を切っても回路データを保持するにはNVM領域に書き込みます。