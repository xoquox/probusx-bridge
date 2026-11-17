// Gegenprobe mit einem privaten Rohmitschnitt. Kurzstatus und Hauptdaten unabhängig vergleichen.
#include "telemetry.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <vector>
#include <array>
#include <iostream>
int main(int argc,char **argv) {
  assert(argc==2);
  std::ifstream in(argv[1],std::ios::binary);
  std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(in),{}};
  for(size_t block: {size_t(1),size_t(7),size_t(1024)}) {
    pbx::Parser parser;int16_t temperatures[14]{},outputs[15]{};
    std::array<int,4> last{};bool have_short=false;unsigned main=0,ext=0,shorts=0,compared=0,r1=0,r3=0;
    auto decode=[&](const uint8_t*f,size_t,bool) {
      if(f[1]!=0x13)return;
      if(f[2]==0x36 && f[4]==0x70) {
        for(int i=0;i<4;++i)last[i]=f[5+i];
        have_short=true;++shorts;
      }else if(f[2]==0x36 && f[4]==0x61) {
        pbx::main_values(f,temperatures,outputs);++main;
        if(have_short){for(int i=0;i<4;++i)assert(outputs[11+i]/2.0==last[i]);++compared;}
        if(outputs[11])++r1;
        if(outputs[13])++r3;
      }else if(f[2]==0x40 && f[4]==0xf0) {
        std::array<int16_t,4> old{outputs[11],outputs[12],outputs[13],outputs[14]};
        pbx::flex1_temperatures(f,temperatures);++ext;
        for(int i=0;i<4;++i)assert(old[i]==outputs[11+i]);
      }
    };
    for(size_t i=0;i<bytes.size();i+=block)parser.feed(bytes.data()+i,std::min(block,bytes.size()-i),decode,1000000);
    assert(main && ext && shorts && compared && r3 && !parser.quality.crc_bad && !parser.rejected);
    std::cout<<block<<"-Byte-Blöcke: Haupt="<<main<<", F0="<<ext<<", Kurzstatus="<<shorts<<", Vergleiche="<<compared<<", R1 aktiv="<<r1<<", R3 aktiv="<<r3<<".\n";
  }
}
