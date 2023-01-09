# mbed(LPC1768)を使ったGreenPAK(SLG46826V-DIP)のProgramer
## 概要
mbed(LPC1768)を使ってGreenPak(SLG46826V-DIP)に設計データを書き込むツールです。

mbed(LPC1768)はPC に接続するとUSB メモリとして認識されるので、GreenPak の設計データであるHex ファイルをそのままmbed に転送して、GreenPak に書き込むことができます。

GreenPak には回路データと内蔵EEPROM データの２種類のHex file がありますがどちらも書き込みができます。

詳細は　DocumentフォルダのPDFファイルを参照してください。

mbedの実行ファイル(binファイル)はmbedBinFileフォルダにあります。
GreenPAK(SLG46826V-DIP)のHEXファイルはgreenPakSampleフォルダにあります。
これらのファイルがあれば、mbedプログラムのコンパイルや、GreenPAKのデータ作成をすることなく動作確認することができます。

## 開発環境
keil studio cloud を使っています。

