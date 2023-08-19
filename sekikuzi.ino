#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <Arduino.h>
#include <CatPrinter.h>

//依存について
//TheNitek氏のCatGFXライブラリを利用します．
//https://github.com/TheNitek/CatGFX
//gitにもあるし，Arduino IDEのライブラリマネージャから引いてくることも可能ですが，
//この子はこの子で依存があるのでライブラリマネージャから引くことを勧めます．
//動かなかった場合は適宜ライブラリに手を入れてください．
//だいたいScan中に注目するペリフェラルの名前リストを追加するなりで接続はうまくいくと思います．
//それでダメなら検索・利用するUUIDを確認してください．
//他に，ESP32に書き込むために，ブリッジのドライバが必要です．
//https://jp.silabs.com/developers/usb-to-uart-bridge-vcp-drivers?tab=overview
//なお，Arduino IDEを利用したコンパイルは永遠にも等しい時間を要します．(ある程度工夫は可能)
//頑張っても遅いので，ほぼ完成した状態から試すことを勧めます．
//装置全体の回路図は別途添付します．

//座席の何列目から何列目までを使うか設定します．どちらも含みます．
//START_LINE <= END_LINE, 1 <= START_LINE, END_LINE <= MAX_SEATR
#define START_LINE 1
#define END_LINE 27

//奇数番目の席だけ引きます．0 or 1
#define USEONLY_ODDSEAT 0

//ボタン入力を検知するデジタルピン，Randomのシードのノイズ取得に使うアナログピン
#define INPUT_DPIN 5
#define REF_APIN 0

//クラスターに存在する座席の列数
#define MAX_SEATR 27

//校舎に存在するすべての座席数(工事か引っ越しか天変地異でもない限り，ほとんどの場合，変更が必要ないはず)
#define r1 0
#define r2 0
#define r3 0
#define r4 0
#define r5 0
#define r6 0

#define r7 9
#define r8 9
#define r9 9
#define r10 9
#define r11 9
#define r12 9
#define r13 9
#define r14 9
#define r15 9
#define r16 9

#define r17 0

#define r18 11
#define r19 11
#define r20 11
#define r21 11
#define r22 11
#define r23 11
#define r24 11
#define r25 11
#define r26 11
#define r27 11

//何列目に何席あるか確認するための配列
int cluster_map[MAX_SEATR] = {r1, r2, r3, r4, r5, r6, r7, r8, r9, r10, r11, r12, r13, r14, r15, r16, r17, r18, r19, r20, r21, r22, r23, r24, r25, r26, r27};

//シャッフル後の席番の情報を持つ．
//Arduinoはメモリ管理機能がないらしくヒープは基本利用しないほうがいいらしい？
//校舎の総席数に合わせて適宜変更してください．
//どうせsum cluster_mapから求まる総座席数で抑えるのでピタリ賞である必要はとくにない
int resultTable[400];

//校舎の総席数の情報を持つ．
int sum_seats;

//Randomで使うシードを格納する．
int seed;

//例によって，座席の名前を格納するための文字列のメモリを確保しておく．
char seatname_buf[30];

//プリンターライブラリで必要な初期化．
CatPrinter cat(400);
byte buffer[48 * 400] = {0};
char commonbuffer[30];

/*
  処理の流れ

    プリンターと接続する
    校舎に存在する総席数を求める
    {0, 1, 2...}というint配列を作り，有名なFisher-Yates shuffleでシャッフル
      Random関数のシードには，マイコンの使ってないアナログピンのノイズを使用
  ここまで用意したらボタン入力を待つ　押されたら以下の処理を繰り返す
    int配列を各列の席数で「割り算」していって，そのintが指す座席の列と番号を求める
    その列と番号が上で指定した条件(列，番号の範囲)を満たしているか確認する
    満たしていた場合のみ印刷する 満たしていなかったら次のインデックスを確かめる
    配列を全部見終わったらwhile(1)
*/

void sgSetsum_seats()
{
	int seats = 0;

	for (int i = 0; i < MAX_SEATR; i ++)
		seats = seats + cluster_map[i];		
	sum_seats = seats;
}

void sgSetresultTable()
{
  for (int i = 0; i < sum_seats; i ++)
    resultTable[i] = i;
}

void sgShuffleTable()
{
    int i = sum_seats;
    int tmp1;
    int tmp2;
  
    while (i > 1) {
        tmp1 = random() % i;
        i --;
        tmp2 = resultTable[i];
        resultTable[i] = resultTable[tmp1];
        resultTable[tmp1] = tmp2;
    }
}

int sgGetSeatR(int number)
{
  int tmp = number + 1;
  int index = 0;

  while (index < MAX_SEATR)
  {
    if (tmp - cluster_map[index] > 0)
      tmp = tmp - cluster_map[index];
    else
      return (index + 1);
    index ++;
  }
  return (-1);
}

int sgGetSeatS(int number)
{
  int tmp = number + 1;
  int index = 0;

  while (index < MAX_SEATR)
  {
    if (tmp - cluster_map[index] > 0)
      tmp = tmp - cluster_map[index];
    else
      return (tmp);
    index ++;
  }
  return (-1);
}

char *sgGetSeatName(int number)
{
  static int printout_times;

  bzero((void *)seatname_buf, 30);
  strcpy(seatname_buf, "[");
  itoa(printout_times, &seatname_buf[1], 10);
  strcpy(&seatname_buf[strlen(seatname_buf)], "]: ");
  strcpy(&seatname_buf[strlen(seatname_buf)], "c1r");
  itoa(sgGetSeatR(number), &seatname_buf[strlen(seatname_buf)], 10);
  strcpy(&seatname_buf[strlen(seatname_buf)], "s");
  itoa(sgGetSeatS(number), &seatname_buf[strlen(seatname_buf)], 10);
  printout_times ++;
  return (seatname_buf);
}

//feedの紙の長さは適宜いじってください
void  sgPrintOutString(char *str)
{
  cat.fillBuffer(0);
  cat.fillScreen(1);
  cat.setTextWrap(true);
  cat.setTextSize(3);
  cat.setTextColor(0);
  cat.setCursor(0, 24);
  cat.println(str);
  cat.printBuffer();
  cat.feed(60);
}

int sgIsValidSeat(int number)
{
  int r = sgGetSeatR(number);
  int s = sgGetSeatS(number);

  if (r <= 0 || s <= 0)
  {
    Serial.println((char *)"R or S is negative.");
    return (-1);
  }
  if (!(START_LINE <= r && r <= END_LINE))
    return (0);
  if (USEONLY_ODDSEAT == 1)
  {
    if (s % 2 == 0)
      return (0);
  }
  return (1);
}



//デバッグ用にいろいろだす
void  sgDebug_PrintArray()
{
  for (int i = 0; i < sum_seats; i ++)
    Serial.println(resultTable[i]);
}

void  sgDebug_PrintOutSettings()
{
  cat.fillBuffer(0);
  cat.fillScreen(1);
  cat.setTextWrap(true);
  cat.setTextSize(2);
  cat.setTextColor(0);
  cat.setCursor(0, 24);
  cat.println((char *)"Hello. Setup Complete.");
  cat.print((char *)"Seed: ");
  itoa(seed, commonbuffer, 10);
  cat.println(commonbuffer);
  cat.print((char *)"start, endline: ");
  itoa(START_LINE, commonbuffer, 10);
  cat.print(commonbuffer);
  cat.print(", ");
  itoa(END_LINE, commonbuffer, 10);
  cat.println(commonbuffer);
  cat.print((char *)"useonly_oddseats: ");
  itoa(USEONLY_ODDSEAT, commonbuffer, 10);
  cat.println(commonbuffer);
  cat.printBuffer();
  cat.feed(100);
}





void setup()
{
  int status;
  Serial.begin(115200);
  Serial.println("\nSearching catty Printer!");
  pinMode(INPUT_DPIN, INPUT);
  seed = analogRead(REF_APIN);
  randomSeed(seed);
  cat.begin(buffer, sizeof(buffer));

  //猫プリンターをスキャンしてコネクト
  while(status == 0)
  {
    status = cat.connect();
    if (status == 0)
      Serial.println((char *)"not found. Search again");
  }
  Serial.println((char *)"Printer found!");

  //テーブルを初期化してシャッフルする
  sgSetsum_seats();
  sgSetresultTable();
  sgShuffleTable();

  //なんかだす
  sgDebug_PrintArray();
  sgDebug_PrintOutSettings();
  Serial.println((char *)"Setup Completed.");
}

void loop()
{
  static int i;
  int status = 0;
  int is_pushed = 0;

  is_pushed = digitalRead(INPUT_DPIN);
  if (is_pushed == 1 && i < sum_seats)
  {
    //条件を満たす座席が出るまでループ
    while (i < sum_seats)
    {
      status = sgIsValidSeat(resultTable[i]);
      if (status == 1)
        break ;
      i ++;
    }
    //条件満たしてたら印刷
    if (i < sum_seats && status == 1)
    {
      sgPrintOutString(sgGetSeatName(resultTable[i]));
      i ++;
      delay(4000);
    }
  }
  else if (is_pushed == 1 && i >= sum_seats)
  {
    Serial.println((char *)"output completed");
    cat.disconnect();
    while(1){}
  }
}
