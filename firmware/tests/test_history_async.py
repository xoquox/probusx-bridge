"""Prüft echte Verlaufs-HTTP-Handler mit SDK-Testumgebung statt eines echten Boards."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
SDK = r'''
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <string>
#include <vector>
#include <deque>
#include <map>
#include <stdexcept>
using esp_err_t=int; using httpd_handle_t=void*;using nvs_handle_t=int;
constexpr int HTTPD_SOCK_ERR_FAIL=-3,HTTPD_SOCK_ERR_TIMEOUT=-4;
using httpd_send_func_t=int(*)(void*,int,const char*,size_t,int);
extern httpd_send_func_t send_override;
extern std::vector<httpd_send_func_t> overrides;
constexpr int ESP_OK=0,ESP_FAIL=-1,ESP_ERR_NO_MEM=-2,HTTP_GET=0,HTTP_POST=1,pdTRUE=1,pdPASS=1;
constexpr unsigned portMAX_DELAY=~0u,MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2;
#define pdMS_TO_TICKS(n) (n)
#define configASSERT(n) assert(n)
#define ESP_ERROR_CHECK(n) assert((n)==ESP_OK)
struct cJSON{};
inline cJSON *cJSON_CreateObject(){return new cJSON;}
inline cJSON *cJSON_AddObjectToObject(cJSON*,const char*){static cJSON v;return &v;}
inline void cJSON_AddNumberToObject(cJSON*,const char*,double){}
inline void cJSON_AddBoolToObject(cJSON*,const char*,bool){}
inline void cJSON_AddStringToObject(cJSON*,const char*,const char*){}
inline void cJSON_Delete(cJSON*p){delete p;}
inline char *cJSON_PrintUnformatted(cJSON*){return strdup("{}");}
inline void cJSON_free(void*p){free(p);}
struct httpd_config_t {};
struct httpd_req_t {int content_len=0;const char*uri="";void*sess_ctx=nullptr;};
struct httpd_uri_t{const char*uri;int method;esp_err_t(*handler)(httpd_req_t*);};
struct Queue{size_t size;std::deque<std::vector<uint8_t>> data;};
using QueueHandle_t=Queue*;using SemaphoreHandle_t=int*;
extern int held,allocations,sends,completed,closed,send_fail,allocation_fails,live_allocations;
extern size_t biggest_alloc;
extern uint64_t clock_us;
extern std::map<std::string,esp_err_t(*)(httpd_req_t*)> handlers;
extern std::map<std::string,void(*)(void*)> tasks;
extern std::string response_status;
struct Stop{};
inline QueueHandle_t xQueueCreate(unsigned,size_t n){return new Queue{n,{}};}
inline int xQueueSend(Queue*q,const void*p,unsigned){q->data.emplace_back(static_cast<const uint8_t*>(p),static_cast<const uint8_t*>(p)+q->size);return pdTRUE;}
inline int xQueueReceive(Queue*q,void*p,unsigned wait){if(q->data.empty()){if(wait==portMAX_DELAY)throw Stop{};return 0;}memcpy(p,q->data.front().data(),q->size);q->data.pop_front();return pdTRUE;}
inline SemaphoreHandle_t xSemaphoreCreateMutex(){return new int(0);}
inline int xSemaphoreTake(SemaphoreHandle_t s,unsigned){assert(!*s);*s=1;++held;return pdTRUE;}
inline void xSemaphoreGive(SemaphoreHandle_t s){assert(*s);*s=0;--held;}
inline int xTaskCreatePinnedToCore(void(*f)(void*),const char*n,unsigned,void*,unsigned priority,void*,unsigned){tasks[n]=f;if(std::string(n)=="history_query")assert(priority<4);return pdPASS;}
inline void vTaskDelay(unsigned){}
inline int64_t esp_timer_get_time(){return clock_us;}
inline void*heap_caps_malloc(size_t n,unsigned){++allocations;biggest_alloc=std::max(biggest_alloc,n);if(allocation_fails)return nullptr;++live_allocations;return malloc(n);}
inline void heap_caps_free(void*p){if(p)--live_allocations;free(p);}
inline size_t heap_caps_get_free_size(unsigned){return 512*1024;}
inline size_t heap_caps_get_largest_free_block(unsigned){return 256*1024;}
inline int nvs_open(const char*,int,nvs_handle_t*){return -1;}
constexpr int NVS_READWRITE=1,NVS_READONLY=0;
inline int nvs_get_blob(nvs_handle_t,const char*,void*,size_t*){return -1;}
inline int nvs_set_blob(nvs_handle_t,const char*,const void*,size_t){return 0;}
inline int nvs_commit(nvs_handle_t){return 0;}inline void nvs_close(nvs_handle_t){}
inline int httpd_resp_set_status(httpd_req_t*,const char*s){response_status=s;return 0;}
inline int httpd_resp_set_type(httpd_req_t*,const char*){return 0;}
inline int httpd_resp_set_hdr(httpd_req_t*,const char*,const char*){return 0;}
inline int httpd_resp_sendstr(httpd_req_t*,const char*){return 0;}
inline int httpd_resp_send_chunk(httpd_req_t*,const char*,size_t){assert(held==0);++sends;if(send_override&&send_override(nullptr,7,"x",1,0)<0)return -1;return send_fail?-1:0;}
inline int httpd_req_get_hdr_value_str(httpd_req_t*,const char*,char*,size_t){return -1;}
inline int httpd_req_recv(httpd_req_t*,char*,size_t){return -1;}
inline int httpd_req_get_url_query_str(httpd_req_t*r,char*out,size_t n){strncpy(out,r->uri,n);return 0;}
inline int httpd_query_key_value(const char*q,const char*k,char*out,size_t n){std::string s=q,key=std::string(k)+"=";auto a=s.find(key);if(a==std::string::npos)return -1;auto end=s.find('&',a);auto v=s.substr(a+key.size(),end==std::string::npos?end:end-a-key.size());strncpy(out,v.c_str(),n);return 0;}
inline int httpd_register_uri_handler(httpd_handle_t,const httpd_uri_t*u){handlers[u->uri]=u->handler;return 0;}
inline int httpd_req_async_handler_begin(httpd_req_t*r,httpd_req_t**out){*out=new httpd_req_t(*r);return 0;}
inline int httpd_req_async_handler_complete(httpd_req_t*r){++completed;delete r;return 0;}
inline int httpd_req_to_sockfd(httpd_req_t*){return 7;}
inline int httpd_queue_work(httpd_handle_t,void(*)(void*),void*){return 0;}
constexpr int SOL_SOCKET=1,SO_SNDTIMEO=2,SHUT_RDWR=2;
inline int shutdown(int,int){++closed;return 0;}
inline int send(int,const void*,size_t n,int){return send_fail?0:int(n);}
inline int httpd_sess_set_send_override(httpd_handle_t,int,httpd_send_func_t fn){send_override=fn;overrides.push_back(fn);return 0;}
inline int setsockopt(int,int,int,const void*,size_t){return 0;}
'''
TEST = r'''
#include "sdk.h"
#include "live.h"
#include "history.h"
#include <iostream>
int held=0,allocations=0,sends=0,completed=0,closed=0,send_fail=0,allocation_fails=0,live_allocations=0;
httpd_send_func_t send_override=nullptr;
std::vector<httpd_send_func_t> overrides;
size_t biggest_alloc=0;uint64_t clock_us=100000000;
std::map<std::string,esp_err_t(*)(httpd_req_t*)>handlers;
std::map<std::string,void(*)(void*)>tasks;std::string response_status;
namespace http_connections{void touch(httpd_req_t*){} void protect_async(httpd_req_t*,bool){}}
bool operation_take(){return true;}void operation_give(){}bool ota_restart_allowed(){return true;}
void live_snapshot(LiveSnapshot&){}
int main(){
 history_init();history_register(nullptr,"token");
 auto data=handlers.at("/api/history/data");
 httpd_req_t r;r.uri="from=0&to=1000&limit=1024";
 assert(data(&r)==ESP_OK);assert(allocations==0&&sends==0&&completed==0);
 // Ein weiterer Abruf wartet nicht auf den ersten. Der zentrale HTTP-Task bleibt frei.
 assert(data(&r)==ESP_OK);assert(response_status=="429 Too Many Requests"&&allocations==0);
 try{tasks.at("history_query")(nullptr);}catch(Stop&){}
 assert(completed==1&&sends==2&&closed==0&&biggest_alloc==65536&&held==0);
 assert(data(&r)==ESP_OK);send_fail=1;
 try{tasks.at("history_query")(nullptr);}catch(Stop&){}
 assert(completed==2&&closed==1&&held==0);
 // Auch nach einem Sendeabbruch ist der Platz wieder frei.
 assert(data(&r)==ESP_OK);send_fail=0;
 try{tasks.at("history_query")(nullptr);}catch(Stop&){}
 assert(completed==3&&held==0&&live_allocations==0);
 allocation_fails=1;assert(data(&r)==ESP_OK);
 try{tasks.at("history_query")(nullptr);}catch(Stop&){}
 assert(completed==4&&response_status=="503 Service Unavailable"&&live_allocations==0);
 allocation_fails=0;assert(data(&r)==ESP_OK);
 try{tasks.at("history_query")(nullptr);}catch(Stop&){}
 assert(completed==5&&live_allocations==0);
 clock_us=110000000;assert(overrides.front()(nullptr,7,"x",1,0)==HTTPD_SOCK_ERR_TIMEOUT);
 clock_us=100000000;send_fail=1;assert(overrides.front()(nullptr,7,"x",1,0)==HTTPD_SOCK_ERR_FAIL);
 send_fail=0;assert(overrides.front()(nullptr,7,"x",1,0)==1);
 std::cout<<"Bestanden: echte HTTP-Handler geben sofort zurück, nur ein Hintergrundabruf, 64-KiB-Grenze, keine Sperre beim Senden und vollständige Freigabe nach Abbruch.\n";
}
'''
class HistoryAsyncTest(unittest.TestCase):
 def test_actual_async_handlers(self):
  with tempfile.TemporaryDirectory() as directory:
   d=Path(directory);(d/'sdk.h').write_text(SDK);(d/'test.cpp').write_text(TEST)
   for name in ['cJSON.h','esp_http_server.h','esp_heap_caps.h','esp_timer.h','nvs.h','freertos/FreeRTOS.h','freertos/semphr.h','freertos/task.h','freertos/queue.h','lwip/sockets.h']:
    f=d/name;f.parent.mkdir(exist_ok=True);f.write_text('#include "sdk.h"\n')
   exe=d/'test'
   subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-I',str(d),'-I',str(ROOT/'src'),str(d/'test.cpp'),str(ROOT/'src/history.cpp'),'-o',str(exe)],check=True)
   subprocess.run([str(exe)],check=True)
if __name__=='__main__':unittest.main()
