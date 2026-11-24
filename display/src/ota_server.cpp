#include <Arduino.h>
#include <Update.h>
#include <WebServer.h>
#include <Preferences.h>
#include "controller.h"
#include "render_ui.h"
#include "image_update.h"
extern Preferences prefs;
namespace {
WebServer server(80);ImageUpdate localUpdate;bool busy=false,allowed=false,failed=false,isAudio=false;size_t expected=0,received=0;uint32_t restartAt=0,uploadAt=0;
const char PAGE[] PROGMEM=R"HTML(<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>ESP Lyric Speaker Update</title><style>body{font:16px system-ui;background:#101820;color:#eee;max-width:650px;margin:40px auto;padding:20px}button,input{font:inherit;margin:10px 0;padding:12px}pre{white-space:pre-wrap}button{background:#e2be76;border:0;border-radius:8px}</style><h1>ESP Lyric Speaker</h1><p>Update both boards from a .vup file. Playback pauses during the update.</p><input id="file" type="file" accept=".vup"><br><button id="go">Update both boards</button><pre id="log">Choose a speaker firmware file.</pre><script>
const log=document.querySelector('#log');document.querySelector('#go').onclick=async()=>{let go=document.querySelector('#go');go.disabled=true;try{const f=document.querySelector('#file').files[0];if(!f)throw Error('Choose a firmware file first.');let h=new Uint8Array(await f.slice(0,256).arrayBuffer());if(new TextDecoder().decode(h.slice(0,8))!=='VINYLUP1')throw Error('Invalid firmware file.');let v=new DataView(h.buffer),a=v.getUint32(8,true),b=v.getUint32(12,true);if(a<24||b<24||a>1572864||b>1572864||f.size!==256+a+b)throw Error('Invalid file size.');let md5a=new TextDecoder().decode(h.slice(16,48)),md5b=new TextDecoder().decode(h.slice(48,80));if(!/^[0-9a-f]{32}$/.test(md5a)||!/^[0-9a-f]{32}$/.test(md5b))throw Error('Invalid manifest.');let av=new Uint8Array(await f.slice(256,280).arrayBuffer()),bv=new Uint8Array(await f.slice(256+a,280+a).arrayBuffer());if(av[0]!==233||av[12]!==0||av[13]!==0||bv[0]!==233||bv[12]!==5||bv[13]!==0)throw Error('Incorrect ESP32 / C3 target.');
async function send(role,data,size,md5){log.textContent='Updating '+role+'...';let form=new FormData();form.append('image',data,role+'.bin');let r=await fetch('/ota/'+role+'?size='+size+'&md5='+md5,{method:'POST',body:form});let result=await r.json();if(!r.ok||!result.ok)throw Error(role+': update failed');}
await send('audio',f.slice(256,256+a),a,md5a);await new Promise(r=>setTimeout(r,4500));await send('display',f.slice(256+a),b,md5b);log.textContent='Both images received. The display board is restarting.';
}catch(e){log.textContent=e.message;}finally{go.disabled=false;}};</script></html>)HTML";
bool auth(){if(server.authenticate("vinyl",prefs.getString("otakey","vinylupdate").c_str()))return true;server.requestAuthentication();return false;}
void abortTransfer(){if(isAudio)relayRequest(linkwire::OtaAbort,nullptr,0);else localUpdate.abort();busy=false;networkResume();uiRestore();}
void upload(bool audio){auto&u=server.upload();uploadAt=millis();
 if(u.status==UPLOAD_FILE_START){allowed=auth();failed=false;received=0;isAudio=audio;if(!allowed)return;
  expected=size_t(server.arg("size").toInt());String md5=server.arg("md5");if(!ImageUpdate::valid(expected,md5.c_str())){failed=true;return;}
  busy=true;networkCancel();uint32_t since=millis();while(runtimeBudget.background.load()&&millis()-since<9000){handleResults();transportTick();delay(1);}if(runtimeBudget.background){failed=true;busy=false;networkResume();return;}
  uiUpdate(audio?"UPDATE AUDIO":"UPDATE C3",0);
  if(audio){uint8_t p[37];p[0]=0;linkwire::put32(p+1,expected);memcpy(p+5,md5.c_str(),32);if(!relayRequest(linkwire::OtaBegin,p,sizeof p))failed=true;}
  else if(!localUpdate.begin(expected,md5.c_str(),5))failed=true;
  if(failed)abortTransfer();
 }else if(u.status==UPLOAD_FILE_WRITE){if(!allowed||failed)return;size_t at=0;while(at<u.currentSize){size_t n=std::min(size_t(256),u.currentSize-at);bool ok=false;
  if(isAudio){uint8_t p[260];linkwire::put32(p,received);memcpy(p+4,u.buf+at,n);uint32_t offset=0;ok=relayRequest(linkwire::OtaData,p,n+4,&offset)&&offset==received+n;}
  else ok=localUpdate.write(u.buf+at,n);
  if(!ok){failed=true;abortTransfer();return;}received+=n;at+=n;}
  static unsigned last=~0u;unsigned percent=received*100/expected;if(percent!=last){last=percent;uiUpdate(isAudio?"UPDATE AUDIO":"UPDATE C3",percent);}
 }else if(u.status==UPLOAD_FILE_END){if(!allowed||failed)return;if(received!=expected)failed=true;else if(isAudio)failed=!relayRequest(linkwire::OtaEnd,nullptr,0);else failed=!localUpdate.finish();if(failed)abortTransfer();else{busy=false;if(!isAudio)restartAt=millis()+1500;else{networkResume();uiRestore();}}}
 else if(u.status==UPLOAD_FILE_ABORTED){if(allowed)abortTransfer();failed=true;}
}
}
void otaBeginServer(){server.on("/",HTTP_GET,[](){if(auth())server.send_P(200,"text/html; charset=utf-8",PAGE);});server.on("/update",HTTP_GET,[](){if(auth())server.send_P(200,"text/html; charset=utf-8",PAGE);});
 auto complete=[](){if(!allowed){server.send(401,"application/json","{\"ok\":false}");return;}server.send(failed?500:200,"application/json",failed?"{\"ok\":false}":"{\"ok\":true}");};
 server.on("/ota/audio",HTTP_POST,complete,[](){upload(true);});server.on("/ota/display",HTTP_POST,complete,[](){upload(false);});server.begin();}
void otaTick(){server.handleClient();if(busy&&millis()-uploadAt>15000){failed=true;abortTransfer();}if(restartAt&&int32_t(millis()-restartAt)>=0)ESP.restart();}
bool otaBusy(){return busy||restartAt;}
