// Prüft die F1-Istwerte samt CRC-Pfad und schützt sie gegen F0-Nullwerte.
#include "telemetry.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <vector>
#include <iostream>
int main(int argc, char **argv) {
  assert(argc == 2);
  std::ifstream in(argv[1], std::ios::binary);
  std::vector<uint8_t> data{std::istreambuf_iterator<char>(in), {}};
  const uint8_t head[] = {0xa5,0x13,0x36,1,0x61};
  auto it = std::search(data.begin(),data.end(),std::begin(head),std::end(head));
  assert(it != data.end() && size_t(data.end()-it) >= 241);
  std::vector<uint8_t> frame(it,it+241);
  auto set = [&](unsigned at, int16_t value) { frame[at]=uint16_t(value)>>8;frame[at+1]=uint16_t(value); };
  auto check = [&] { auto crc=pbx::crc(frame.data(),238);frame[238]=0xaa;frame[239]=crc>>8;frame[240]=crc; };
  set(114,0);set(116,0);set(118,200);set(120,0);check();
  int16_t temps[14]{},outputs[15]{};unsigned accepted=0;
  pbx::Parser parser;
  auto decode = [&](const uint8_t *f,size_t,bool) {
    ++accepted;
    if(f[4]==0x61)pbx::main_values(f,temps,outputs);
    else if(f[4]==0xf0)pbx::flex1_temperatures(f,temps);
  };
  // Einen Hauptframe über unterschiedliche Empfangsblöcke verteilen.
  parser.feed(frame.data(),117,decode,1000);parser.feed(frame.data()+117,124,decode,2000);
  assert(accepted==1 && outputs[11]==0 && outputs[12]==0 && outputs[13]==200 && outputs[14]==0);
  uint8_t ext[26]={0xa5,0x13,0x40,1,0xf0,1,0x55,9,0xc4,9,0xc4,9,0xc4};
  auto crc=pbx::crc(ext,24);ext[24]=crc>>8;ext[25]=crc;
  parser.feed(ext,sizeof(ext),decode,3000);
  assert(accepted==2 && temps[10]==341 && outputs[13]==200);
  // Ein-/Aus-Wechsel auf R1 und R3: zugehörige Wörter getrennt auswerten.
  set(114,200);check();parser.feed(frame.data(),frame.size(),decode,4000);
  assert(outputs[11]==200 && outputs[13]==200);
  set(114,0);set(118,0);check();parser.feed(frame.data(),frame.size(),decode,5000);
  assert(outputs[11]==0 && outputs[13]==0);
  // Eine beschädigte aktive Meldung darf keinen neuen Ausgangswert setzen.
  set(118,200);check();frame[118]^=1;unsigned before=accepted;
  parser.feed(frame.data(),frame.size(),decode,6000);
  assert(accepted==before && outputs[13]==0 && parser.quality.crc_bad==1);
  assert(pbx::flex1_at(3000,2000)==2000);
  assert(pbx::flex1_at(2000,3000)==2000);
  assert(pbx::flex1_at(0,3000)==0 && pbx::flex1_at(3000,0)==0);
  std::cout<<"Bestanden: F1-Istwerte aus Hauptdaten, F0 überschreibt keine Ausgänge, R1/R3-Wechsel, CRC-Verwerfung, Blockgrenzen und gemeinsame Frische.\n";
}
