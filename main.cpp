/**
 * GreenPak(SLG46826V) writer
 *
 * mbed(LPC1768)にNVM,EEPROM用のHEX fileを転送し、GreenPakに書き込む。
 * NVM,EEPROM,RESISTERへの読み書き、NVM,EEPROMのクリア、有効になっているSlave addressの確認が可能
 *
 * <使用方法>
 * ●mbedとPCの接続
 * mbedのUSBとPCを接続してUSB-Serialとして接続する
 * PCからはteratarmなどのターミナルソフトで操作する
 *
 * USB-Serialの通信設定値
 *   baudrate : 115200[bps]
 *   bits     : 8bit
 *   parity   : none
 *   stopbit  : 1bit
 *
 * ●mbedとGreenPakとの接続
 * mbed      GreenPak
 * p9(sda)  - 9Pin(sda)
 * p10(scl) - 8Pin(scl)
 * VOUT(3.3V) - 1Pin,14Pin
 * GND        - 11Pin
 * mbedにはGreenPakを1つだけ接続できる（GreenPakの複数接続には未対応)
 *
 * ●書き込み用HEX fileの準備
 * GreenPakのHEX fileの名称を以下のようにする(これ以外の名称は無視される)
 * NVM,RESISTER : NVM.hex
 * EEPROM       : EEPROM.hex
 * このhex fileをmbedのルートディレクトリに転送しておく
 *
 * ●command
 * データ読み出し
 *   rn: NVM領域のデータ読み出し
 *   re: EEPROM領域のデータ読み出し
 *   rr: RESISTER領域のデータ読み出し
 *
 * データ書き込み
 *   wnx: NVM領域へのNVM.hexの書き込み. xにはslave address=0～f
 * を設定(設定しない場合は、現状のslave addressを継承) we:
 * EEPROM領域へのEEPROM.hexの書き込み wr: RESISTER領域へのNVM.hexの書き込み
 *
 * クリア
 *   en: NVM領域のクリア
 *   ee: EEPROM領域のクリア
 *
 *  slave addressの確認
 *   p: 今現在有効になっているslave addressを表示
 *
 * <mbedの開発環境>
 * 使用ボード: LPC1768
 * 開発環境: Keil Studio Cloud
 * mbedOS: mbedOS2
 *
 * <参考>
 * このプログラムは下記のHPを参考にしました。
 * "M5StickCやM5Stackを使ってGreenPAKに設計データを焼いてみた"
 * https://elchika.com/article/e0de0e0d-b41a-4d93-9d8a-2013f501c906/
 *
 * @file
 * @attention なし
 */

//=====================================
/* GreenPakのhex fileについての説明

<Green Pak Hex file structure>
HEX fileのコードは以下を参照
https://ja.wikipedia.org/wiki/Intel_HEX

start code
| byte count
| |  address
| |  |    recode type
| |  |    |  data                             checksum
↓ ↓  ↓    ↓  ↓　　　　　　　　　　　　　　　　　 ↓　
: 10 0000 00 9F07003D0F0000000000000000000000 FE   ↓ data
: 10 0010 00 00000000000000000000000000000000 E0
:100020000000000000000000000000E0FB000000F5
:10003000000000000000000000000000CFEFFC0006
:1000400000000000000000000000000000000000B0
:1000500000000000000000000000000000000000A0
:1000600000303000303030300000303030000000E0
:1000700030303030000000000000000000000000C0
:10008000FA00FA663B1422300C0000000000000069
:10009000000000000000000000000300000000005D
:1000A0000000002000010000000201000002000129
:1000B0000000020100000200010000020100000235
:1000C000000100000200010000000101000000002A
:1000D0000000000000000000000000000000000020
:1000E0000000000000000000000000000000000010
:1000F000000000000000000000000000000000A55B　　　　　↑ data
:00000001FF                                         ← end code

0x0000 ～ 0x00ff の256byteになる
*/

#include "ctype.h"
#include "mbed.h"
#include <cstdint>
#include <stdio.h>
//#include <string.h>
#include "BufferedSerial.h"

//=====================================
// mbed内部のfilesystem
//=====================================
LocalFileSystem local("local"); // local file systemの設定
#define Z_bufferNumber                                                         \
  (100) // HEX fileは1行44byteなのでこれ以上のbyte数があればよい(file
        // systemは1行づつ読み込まれる)

char buffer[Z_bufferNumber]; // 読みだしたデータの保管先

//=====================================
// PCからのコマンド入力用USB-Uart
//=====================================
BufferedSerial pc(USBTX, USBRX);
#define PC_BOUD (115200)
#define Z_pcBuffer (100) // PCからのコマンド保管用
char B_pcRx[Z_pcBuffer] __attribute__((
    section("AHBSRAM0"))); // RAMが足りないのでEthernet用エリアを使用
                           // (0x2007c000)　(コピー元をそのまま転記した)

//=====================================
// mbedボード上の動作モニタLED (未使用)
//=====================================
// DigitalOut ledopen(LED1);
// DigitalOut ledout(LED2);
// DigitalOut lederror(LED4);

//=====================================
// GreenPak のI2C処理
//=====================================
/**
 * I2C定義(GreenPakとの通信用)
 */
I2C Wire(p9, p10); //!< sda:p9, sci:p10

/**
 * I2Cのslave address とGreenPakの"Control Byte"との関係
 *
 * I2Cのslave addressがGreenPakのControl byteに該当する
 * Control byteの構成
 * 上位4bit: Control Code :ICを特定するためのコード 0x0 ～ 0xf を選択できる
 *  このプログラムで"slave address"と言っているのは実際にはこのControl
 * Codeのことになる 下位4bit: Block Address :
 * IC内部のNVM,EEPROM,RESISTERを指示する(最下位の1bitはI2C通信のR/Wbitになる)
 * Block Address (Control Byte A10-8)
 *      A10
 *      |A9
 *      ||A8
 * xxxx 098xb
 * xxxx 001xb :resster
 * xxxx 010xb :NVM
 * xxxx 110xb :EEPROM
 * |  | |-| ←Block Address
 * |  |
 * |←→| ←Control Code
 */

/**
 * Block Address
 */
#define RESISTER_CONFIG (0x02)
#define NVM_CONFIG (0x04)
#define EEPROM_CONFIG (0x06)

#define MASK_CONTROLCODE                                                       \
  (0xf0) //<! I2C slave address部の ControlCode(上位4bit)を残すためのマスク

uint8_t hexData[16][16] = {}; //<! hex fileから読みだしたデータの保管用

typedef enum {
  NVM,
  EEPROM,
  RESISTER
} greenPakMemory_t; //<! 操作対象メモリの指示用

char i2cBuffer
    [17]; //<! I2C送受信用バッファ(GreenPakのaddress(1byte)+data(16byte)=17byte)

//=====================================
// usb-serial
//=====================================
/**
 * pcからmbedへのコマンド指示受信
 *
 * pcからのコマンドを受け取り受信バッファに格納する。
 * pcからenterキーを押されると１つのコマンドとして解釈する
 * main()から呼び出して、この関数の戻り値が"1"の時にコマンド解析を行う
 * @@return 0:受信中 1:受信完了
 */
int pcRecive(void) {
  static char *p = B_pcRx;
  char data;
  int ans = 0;
#define Z_00 (0x00)
#define Z_CR (0x0d)

  // buffer オーバーフロー対策
  if ((p - B_pcRx) >= Z_pcBuffer) {
    p = B_pcRx;
    *p = Z_00;
  }

  // 1文字受信処理
  while ((pc.readable() == 1) && ans == 0) {
    // 受信データあり
    data = pc.getc();

    switch (data) {
    case Z_CR:
      *p = Z_00;
      p = B_pcRx;
      ans = 1;
      break;
    case ' ':
    case ',':

      // nothing
      break;
    default:
      // 小文字のアルファベットを大文字に差し替える
      if (('a' <= data) && (data <= 'z')) {
        data -= ('a' - 'A');
      }
      *p++ = data;
      *p = Z_00;
      break;
    }
  }
  return (ans);
}

/**
 * asciiコード1文字をhexに変換
 *
 * @param[in] char* p : 文字の入った変数のポインタ
 * @return 変換結果 0x00 ～ 0x0f, 0xff:変換不能
 */
uint8_t atoh1(char *p) {
  char a = *p;
  uint8_t ans = 0xff;

  if (('0' <= a) && (a <= '9')) {
    ans = a - '0';
  } else if (('a' <= a) && (a <= 'f')) {
    ans = a - 'a' + 0x0a;
  } else if (('A' <= a) && (a <= 'F')) {
    ans = a - 'A' + 0x0a;
  } else {
    ans = 0xff;
  }
  return (ans);
}

/**
 *  asciiコード2文字をhexに変換
 *
 * @param[in] char* p: 文字の入った変数ポインタ
 * @return 変換結果 0x00 ～ 0xff (変換不能の場合は0xffにしている)
 */
uint8_t atoh2(char *p) {
  uint8_t ans;
  uint8_t up = atoh1(p);
  uint8_t dn = atoh1(p + 1);
  if ((up != 0xff) && (dn != 0xff)) {
    ans = (up << 4) | dn;
  } else {
    // 変換不能
    ans = 0xff;
  }
  return (ans);
}

/**
 * HEX fileを読み出す
 *
 * 対象ファイルは"NVM.hex"か"EEPROM.hex"の決め打ち
 * hex fileの内容が意図しない形式になっていた場合のことは考慮していない
 * 異常データでも読み出しするので用意するHEX fileには注意すること
 * @param[in] 格納対象データ greenPakMemory_t NVM,RESISTER: NVM.hex, EEPROM:
 * EEPROM.hexを読み込む
 * @return 0:データなし n:読み込み桁数(正常なら16になる)
 */
uint8_t hexFileRead(greenPakMemory_t memoryType) {
  uint8_t ans = 0;

  uint8_t byteCount;
  uint8_t address; // 上位8bitは必ず0x00になるので省略
  uint8_t recodeType;

  FILE *fp;
  char *p;

  // 読みだしたデータの格納バッファを0x00に初期化する
  // 0x00はNVM,EEPROM共に初期値なので安全側になる
  for (uint8_t i; i < 16; i++) {
    for (uint8_t j; j < 16; j++) {
      hexData[i][j] = 0x00;
    }
  }

  pc.printf("HEX file read\n");

  switch (memoryType) {
  case NVM:
  case RESISTER:
    fp = fopen("/local/NVM.hex", "r");
    break;
  case EEPROM:
    fp = fopen("/local/EEPROM.hex", "r");
    break;
  default:
    return 0;
    break;
  }
  if (fp == NULL) {
    return 0;
  }

  while (fgets(buffer, Z_bufferNumber, fp) != NULL) {
    p = buffer;

    while (*p != 0x00) {
      switch (*p++) {
      case ':':
        byteCount = atoh2(p);
        p += 2;
        p += 2;
        address = atoh2(p) >> 4; // 2byte addressの下位1byteを取得
        if (address > 0x0f) {
          address = 0x00;
        } // 範囲外なら0x00にしておく
        p += 2;
        recodeType = atoh2(p);
        p += 2;

        pc.printf("byte=%02x address=%02x type=%02x : ", byteCount, address,
                  recodeType);
        wait(.1);
        if (byteCount != 0x10) {
          pc.printf("end of data\n");
          break;
        }

        ans++;
        for (uint8_t i = 0; i < 16; i++) {
          hexData[address][i] = atoh2(p);
          p += 2;
          pc.printf("%02x", hexData[address][i]);
        }
        pc.printf("\n");
      }
    }
  }
  fclose(fp);

  return (ans);
}

//=====================================
// GreenPak 操作
//=====================================
//*************************************
/**
 * GreenPakのslave address(Control Code)を確認
 *
 * PCへのモニタ表示用
 */
//*************************************
void ping(void) {
  int ans;
  int control_code;

  for (int i = 0; i < 16; i++) {
    control_code = (i << 4) | RESISTER_CONFIG;
    ans = Wire.read(control_code, i2cBuffer,
                    0); // ICに影響を与えないようにreadコマンドで確認する
    wait(0.01);
    pc.printf("slave address =  0x%02x ", i);
    if (ans == 0) {
      pc.printf(" is present\n");
    } else {
      pc.printf(" is not present\n");
    }
  }
  pc.printf("\n");
  wait(0.1);
}

//*************************************
/**
 * 接続されているGreenPakのslave addressを取得
 *
 * GreenPakの操作用
 * @return 取得したslave address: 0x00～0x0f, 見つからなければ0xffを返す
 */
//*************************************
int checkSlaveAddres(void) {
  int control_code;
  int address = 0xff;
  int ans;

  for (int i = 0; i < 16; i++) {
    control_code = (i << 4) | RESISTER_CONFIG;

    ans = Wire.read(control_code, i2cBuffer,
                    0); // ICに影響を与えないようにreadコマンドで確認する
    if (ans == 0) {
      address = i;
      //      pc.printf("slave address =  0x%02x\n", i);
      break;
    }
  }
  return (address);
}

//*************************************
/**
 * greenPak 再起動指示
 *
 * NVMの書き換えをしたときにNVMの内容をRESISTERに反映させるために再起動させる
 */
//*************************************
void powercycle(void) {
  int slaveAddress = checkSlaveAddres();
  if (slaveAddress == 0xff) {
    return;
  }

  int control_code =
      (slaveAddress << 4) | RESISTER_CONFIG; // ControlCode(A14-11)=slaveAddress(4bit)
                                             // + BlockAddress(A10-8)=000b

  pc.printf("Power Cycling!\n\n");
  // Software reset
  // レジスタアドレス=0xc8 bit1を1にすると I2C
  // resetをしてNVMのデータをレジスタに転送することができる
  i2cBuffer[0] = 0xC8;
  i2cBuffer[1] = 0x02;
  Wire.write(control_code, i2cBuffer,
             2); // MASK_CONTROLCODEは Control Code:slave
                 // addressを残しresisterアクセスにするためのマスク
  // pc.printf("Done Power Cycling!\n");
}

//*************************************
/**
 * GreenPakのAck確認
 *
 * GreenPakへの操作指示後の動作完了をI2CのACKの受信で確認する
 * @param[in] 確認対象のGreenPakのControl Byte
 * @return 0:ACK, -1:確認失敗
 */
//*************************************
int ackPolling(int addressForAckPolling) {
  int ans;
  int nack_count = 0;
  while (1) {

    ans = Wire.read(addressForAckPolling, i2cBuffer, 0);
    if (ans == 0) {
      return 0;
    }
    if (nack_count >= 1000) {
      pc.printf("Geez! Something went wrong while programming!\n");
      return -1;
    }
    nack_count++;
    wait(1);
  }
}

//*************************************
/**
 * 操作対象のmemoryの表示
 *
 * PCに操作対象になっているmemoryの種別を表示する
 * @param[in] greenPakMemory_t 操作対象memory
 */
//*************************************
void printMemoryType(greenPakMemory_t memoryType) {
  switch (memoryType) {
  case NVM:
    pc.printf("memory = NVM\n");
    break;
  case EEPROM:
    pc.printf("memory = EEPROM\n");
    break;
  case RESISTER:
    pc.printf("memory = RESISTER\n");
    break;
  default:
    pc.printf("memory = unknown\n");
    break;
  }
}

//*************************************
/**
 * GreenPak NVMプロテクト解除
 *
 * NVM書き込み時に誤ってNVMプロテクトをかけてしまった場合に、それを解除する
 */
//*************************************
void resister_unprotect(void) {
  int slaveAddress = checkSlaveAddres();
  if (slaveAddress == 0xff) {
    return;
  }

  int control_code =
      (slaveAddress << 4) |
      RESISTER_CONFIG; // ControlCode(A14-11)=slaveAddress(4bit) +
                       // BlockAddress(A10-8)=000b

  // resisiterのプロテクトをクリアする
  // レジスタアドレス: 0xE1 にNVMのプロテクト領域がある (HM p.171)
  // 下位2bit 00: read/write/erase 可能
  //          01: read禁止
  //          10: write/erase 禁止
  //          11: read/write/erase 禁止
  i2cBuffer[0] = 0xE1;
  i2cBuffer[1] = 0x00;
  Wire.write(control_code, i2cBuffer,
             2); // MASK_CONTROLCODEは Control Code:slave
                 // addressを残しresisterアクセスにするためのマスク

  i2cBuffer[0] = 0xE1;
  Wire.write(control_code, i2cBuffer, 1);

  Wire.read(control_code, i2cBuffer, 1);
  uint8_t val = i2cBuffer[0];
  //  pc.printf("reg address:0xE1 = %02x\n", val); //
  //  0x00ならプロテクト解除されている
}

//*************************************
/**
 * 指示memory領域のクリア指示
 *
 * @param[in] greenPakMemory_t NVM,EEPROM,RESISTER クリア対象領域の指示 　
 * @return 0:正常終了 -1:異常終了
 */
//*************************************
int eraseChip(greenPakMemory_t memoryType) {
  int slaveAddress = checkSlaveAddres();
  if (slaveAddress == 0xff) {
    pc.printf("not found IC\n");

    return -1;
  }

  int control_code =
      (slaveAddress << 4) |
      RESISTER_CONFIG; // ControlCode(A14-11)=slaveAddress(4bit) +
                       // BlockAddress(A10-8)=000b
  int addressForAckPolling = control_code;

  pc.printf("slave address =  0x%02x\n", slaveAddress);

  printMemoryType(memoryType);

  if (memoryType == RESISTER) {
    pc.printf("RESISTER don't erase area\n");
    return (0);
  }

  resister_unprotect();

  for (uint8_t i = 0; i < 16; i++) {
    pc.printf("Erasing page: 0x%02x ", i);

    i2cBuffer[0] = 0xE3; // I2C Word Address
    // Page Erase Register
    // bit7: ERSE  1
    // bit4: ERSEB4  0: NVM, 1:EEPROM
    // bit3-0: ERSEB3-0: page address
    if (memoryType == NVM) {
      pc.printf("NVM ");
      i2cBuffer[1] = (0x80 | i);
    } else if (memoryType == EEPROM) {
      pc.printf("EEPROM ");
      i2cBuffer[1] = (0x90 | i);
    }
    Wire.write(control_code, i2cBuffer,
               2); // Control BYte = ControlCode + Block Address

    wait(0.1);

    /* To accommodate for the non-I2C compliant ACK behavior of the Page Erase
     * Byte, we've removed the software check for an I2C ACK and added the
     * "Wire.endTransmission();" line to generate a stop condition.
     *  - Please reference "Issue 2: Non-I2C Compliant ACK Behavior for the NVM
     * and EEPROM Page Erase Byte" in the SLG46824/6 (XC revision) errata
     * document for more information.
     *
     * 要約: たまにNACKを返すことがあるので、無条件に終了させればよい。
     * https://medium.com/dialog-semiconductor/slg46824-6-arduino-programming-example-1459917da8b
     */

    // tER(20ms)の処理終了待ち
    if (ackPolling(addressForAckPolling) == -1) {
      pc.printf("NG\n");
      return -1;
    } else {
      pc.printf("ready \n");
      wait(0.1);
    }
  }
  pc.printf("\n");

  powercycle();
  return 0;
}

//*************************************
/**
 * 指示memory領域への書き込み指示
 *
 * @param[in] greenPakMemory_t NVM,EEPROM,RESISTER 対象領域の指示 　
 * @param[in] int NVM書き込み時にslave addressを変更する場合に指示(NVMのみ必要)
 * つけなければ現状と同じaddressを設定する
 * @return 0:正常終了 -1:異常終了
 */
//*************************************
int writeChip(greenPakMemory_t memoryType, int nextSlaveAddress = 0xff) {
  int control_code = 0x00;
  int addressForAckPolling = 0x00;

  int ans;

  uint8_t nowSlaveAddress = checkSlaveAddres();
  if (nowSlaveAddress == 0xff) {
    pc.printf("not found IC\n");

    return -1;
  }

  pc.printf("slave address =  0x%02x\n", nowSlaveAddress);

  if (memoryType == NVM) {
    if ((nextSlaveAddress < 0x00) || (0x0f < nextSlaveAddress)) {
      nextSlaveAddress = nowSlaveAddress;
    }
    pc.printf("next slave address = 0x%02x\n", nextSlaveAddress);
  }

  printMemoryType(memoryType);

  if (memoryType == NVM) {

    resister_unprotect();

    // Serial.println(F("Writing NVM"));
    // Set the slave address to 0x00 since the chip has just been erased
    // Set the control code to 0x00 since the chip has just been erased
    control_code = nowSlaveAddress << 4;
    control_code |= NVM_CONFIG;
    addressForAckPolling = nowSlaveAddress << 4;
    if (hexFileRead(NVM) != 16) {
      return -1;
    };
  } else if (memoryType == EEPROM) {
    // pc.printf("Writing EEPROM\n");
    control_code = nowSlaveAddress << 4;
    control_code |= EEPROM_CONFIG;
    addressForAckPolling = nowSlaveAddress << 4;
    if (hexFileRead(EEPROM) != 16) {
      return -1;
    };
  } else if (memoryType == RESISTER)
  {
    // Serial.println(F("Writing RESISTER"));
    control_code = nowSlaveAddress << 4;
    control_code |= RESISTER_CONFIG;
    addressForAckPolling = nowSlaveAddress << 4;
    if (hexFileRead(NVM) != 16) {
      return -1;
    };
  }
  pc.printf("\n");

  if (memoryType == NVM) {
    // nextSlaveAddressに設定されている値に差し替える
    // レジスタアドレス=0xcaのbit3-0 にI2C slave address を設定する
    // bit7-4:
    // 0にすると下位4bitのアドレスが有効になる(1にするとIO2,3,4,5の端子状態がアドレスになる)
    //
    // レジスタを書き換える場合にはslave address の書き換えは行わない
    // この場合に書き換えると、この直後からアドレスが切り替わりその後の書き込みができなくなる
    hexData[0xC][0xA] = (hexData[0xC][0xA] & 0xF0) | nextSlaveAddress;
  } else if (memoryType == RESISTER) {
    hexData[0xC][0xA] = (hexData[0xC][0xA] & 0xF0) | nowSlaveAddress;
  }

  // erase
  if (memoryType != RESISTER) {
    pc.printf("erase start\n");
    if (eraseChip(memoryType) == 0) {
      wait(0.3); // erase後の安定待ち(これが無いとこの後の書き込みでエラーになる)
      pc.printf("erase OK\n");
    } else {
      pc.printf("erase NG\n");
      return -1;
    }
  } else {
    pc.printf("RESISTER don't erase area\n");
  }

  // Write each byte of hexData[][] array to the chip
  for (int i = 0; i < 16; i++) {
    i2cBuffer[0] = i << 4;
    pc.printf("%02x: ", i);

    for (int j = 0; j < 16; j++) {
      i2cBuffer[j + 1] = hexData[i][j];
      pc.printf("%02x ", hexData[i][j]);
    }
    ans = Wire.write(control_code, i2cBuffer, 17);
    wait(0.01);

    if (ans != 0) {
      pc.printf(" nack\n");
      pc.printf("Oh No! Something went wrong while programming!\n");
      Wire.stop();
      return -1;
    }

    pc.printf(" ack ");

    if (ackPolling(addressForAckPolling) == -1) {
      return -1;
    } else {
      pc.printf("ready\n");
      wait(0.1);
    }
  }

  Wire.stop();

  // NVMを書き換えたら再起動させて動作に反映させる
  if (memoryType == NVM) {
    powercycle();
  }
  return 0;
}

//*************************************
/**
 * 指示memory領域からの読み込み指示
 *
 * @param[in] greenPakMemory_t NVM,EEPROM,RESISTER 対象領域の指示 　
 * @return 0:正常終了 -1:異常終了
 */
//*************************************
int readChip(greenPakMemory_t memoryType) {
  int slaveAddress = checkSlaveAddres();
  if (slaveAddress == 0xff) {
    pc.printf("not found IC\n");
    return -1;
  }

  uint8_t control_code = slaveAddress << 4;

  pc.printf("slave address =  0x%02x\n", slaveAddress);

  printMemoryType(memoryType);

  // I2C Block Addressの設定
  // A9=1, A8=0: NVM (0x02)
  // A9=1, A8=1: EEPROM (0x03)
  if (memoryType == NVM) {
    control_code |= NVM_CONFIG;
  } else if (memoryType == EEPROM) {
    control_code |= EEPROM_CONFIG;
  } else if (memoryType == RESISTER)
  {
    control_code |= RESISTER_CONFIG;

  }

  for (int i = 0; i < 16; i++) {
    pc.printf("%02x :", i);

    i2cBuffer[0] = i << 4;
    Wire.write(control_code, i2cBuffer, 1, true);
    wait(0.01);

    Wire.read(control_code, i2cBuffer, 16, true);

    for (int j = 0; j < 16; j++) {
      pc.printf("%02x ", i2cBuffer[j]);
    }
    pc.printf("\n");
  }
  Wire.stop();
  return 0;
}

//*************************************
/**
 * mainルーチン
 *
 */
//*************************************
int main() {
  int ans;
  //  pc.format(8,Serial::Even,1);
  pc.baud(PC_BOUD);
  Wire.frequency(10000);

  pc.printf("\n>");
  while (1) {

    if (pcRecive() == 1) {
      char *p = B_pcRx;
      switch (*p++) {
      case 'E':
        pc.printf("erase start\n");
        switch (*p++) {
        case 'N':
          ans = eraseChip(NVM);
          break;
        case 'E':
          ans = eraseChip(EEPROM);
          break;
        case 'R':
        default:
          ans = -2;
          break;
        }

        switch (ans) {
        case 0:
          pc.printf("erase OK\n");
          break;
        case -1:
          pc.printf("erase NG\n");
        case -2:
        default:
          pc.printf("command error\n");
          break;
        }
        break;
      case 'P':
        ping();
        break;
      case 'R':
        pc.printf("Reading chip!\n");
        switch (*p++) {
        case 'N':
          ans = readChip(NVM);
          break;
        case 'E':
          ans = readChip(EEPROM);
          break;
        case 'R':
          ans = readChip(RESISTER);
          break;
        default:
          ans = -2;
        }

        switch (ans) {
        case 0:
          pc.printf("read OK\n");
          break;
        case -1:
          break;
          pc.printf("read NG\n");
        case -2:
        default:
          pc.printf("command error\n");
          break;
        }
        break;
      case 'W':
        switch (*p++) {
        case 'N':
          ans = writeChip(NVM, atoh1(p));
          break;
        case 'E':
          ans = writeChip(EEPROM);
          break;
        case 'R':
          ans = writeChip(RESISTER);
          break;
        default:
          ans = -2;
          break;
        }

        switch (ans) {
        case 0:
          pc.printf("write OK\n");
          break;
        case -1:
          pc.printf("write NG\n");
          break;
        case -2:
          pc.printf("command error\n");
          break;
        }
        pc.printf("\n");
        break;
      case 'D':
        pc.printf("D input\n");
        break;
      }
      pc.printf("\n>");
    }
  }
}
