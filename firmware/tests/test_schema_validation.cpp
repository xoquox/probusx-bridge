#include "schema_validation.h"
#include <cassert>
#include <iostream>
#include <vector>
using namespace schema_validation;
int main() {
  uint32_t revision = 0;
  std::string clean;
  auto check = [&](const std::string &json) { return normalize_layout(json.c_str(), json.size(), revision, clean); };
  assert(check("{\"version\":1,\"revision\":7,\"items\":[{\"id\":\"S1\",\"x\":25.5,\"y\":80,\"visible\":false,\"label\":\"Speicher <oben>\"}]}"));
  assert(revision == 7 && clean.find("Speicher <oben>") != std::string::npos);
  assert(check("{\"version\":1,\"revision\":0,\"items\":[]}"));
  const std::string item = "{\"id\":\"F3HE\",\"x\":0,\"y\":100,\"visible\":true,\"label\":\"\"}";
  assert(check("{\"version\":1,\"revision\":0,\"items\":["+item+"]}"));
  assert(!check("{\"version\":1,\"revision\":0,\"items\":["+item+","+item+"]}"));
  for (auto bad : {"null", "{}", "{\"version\":1,\"revision\":-1,\"items\":[]}",
       "{\"version\":2,\"revision\":0,\"items\":[]}", "{\"version\":1,\"revision\":0.5,\"items\":[]}",
       "{\"version\":1,\"revision\":0,\"items\":{}}"}) assert(!check(bad));
  const std::string prefix="{\"version\":1,\"revision\":0,\"items\":[";
  for (auto bad : {"{\"id\":\"S11\",\"x\":0,\"y\":0,\"visible\":true,\"label\":\"\"}",
       "{\"id\":\"S1\",\"x\":101,\"y\":0,\"visible\":true,\"label\":\"\"}",
       "{\"id\":\"S1\",\"x\":0,\"y\":-1,\"visible\":true,\"label\":\"\"}",
       "{\"id\":\"S1\",\"x\":0,\"y\":0,\"visible\":1,\"label\":\"\"}",
       "{\"id\":\"S1\",\"x\":0,\"y\":0,\"visible\":true,\"label\":\"a\\nb\"}"}) assert(!check(prefix+bad+"]}"));
  assert(!check(prefix+"{\"id\":\"S1\",\"x\":0,\"y\":0,\"visible\":true,\"label\":\""+std::string(81,'a')+"\"}]}"));
  uint32_t format=0;uint16_t w=0,h=0;
  std::vector<uint8_t> png(45,0);
  memcpy(png.data(),"\x89PNG\r\n\x1a\n",8);png[11]=13;memcpy(png.data()+12,"IHDR",4);
  png[18]=4;png[19]=0;png[22]=2;png[23]=0;memcpy(png.data()+37,"IEND",4);
  assert(image_info(png.data(),png.size(),format,w,h) && format==1 && w==1024 && h==512);
  png[16]=1; assert(!image_info(png.data(),png.size(),format,w,h));png[16]=0;
  png[0]=0;assert(!image_info(png.data(),png.size(),format,w,h));
  // JPEG mit SOF-Kopf und Abschlusssignatur. Die tatsächliche Dekodierung erfolgt im Browser.
  std::vector<uint8_t> jpg={0xff,0xd8,0xff,0xc0,0,11,8,1,0,2,0,1,1,0x11,0,0xff,0xd9};
  assert(image_info(jpg.data(),jpg.size(),format,w,h) && format==2 && w==512 && h==256);
  jpg[5]=255;assert(!image_info(jpg.data(),jpg.size(),format,w,h));
  assert(!image_info(reinterpret_cast<const uint8_t *>("<svg></svg>"),11,format,w,h));
  std::cout << "Bestanden: Schema-JSON normalisiert, Versionen und Revisionen geprüft, ungültige Kanäle/Positionen/Bezeichnungen verworfen, Bildköpfe und Größen geprüft.\n";
}
