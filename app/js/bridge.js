// One message channel for the UI. Inside the app it is WebView2's postMessage to the native
// host; in a plain browser it is the simulated speaker in mock-backend.js. Both speak the same
// JSON messages, so the mock and the app run the same UI code.
(function(root){
'use strict';
const webview=root.chrome&&root.chrome.webview;
const listeners=[];
let next=1;
const pending=new Map();
function emit(message){
  if(message.type==='result'&&pending.has(message.id)){const done=pending.get(message.id);pending.delete(message.id);done(message);}
  // One listener failing must not stop the others (the store's state would go stale).
  listeners.forEach(fn=>{try{fn(message);}catch(e){report(e);}});
}
// Script errors go to the host's diagnostic log (written only while Diagnostic log is on).
function report(e){try{console.error(e);send({type:'log',text:String(e&&e.stack||e).slice(0,1500)});}catch{}}
root.addEventListener('error',e=>report(e.error||e.message));
root.addEventListener('unhandledrejection',e=>report(e.reason));
// Fire-and-forget for settings; request() for writes whose outcome the UI shows.
function send(message){if(webview)webview.postMessage(message);else root.MockBackend.receive(message);}
// A request always ends: a host that never answers (a dropped message, an error on its side) counts as
// a failure after TIMEOUT, so nothing in the UI can wait forever.
const TIMEOUT=15000;
function request(message){
  const id=next++;
  return new Promise(resolve=>{
    const timer=setTimeout(()=>{if(pending.delete(id))resolve({type:'result',id,ok:false,error:'The speaker did not answer.'});},TIMEOUT);
    pending.set(id,r=>{clearTimeout(timer);resolve(r);});
    try{send({...message,id});}catch(e){clearTimeout(timer);pending.delete(id);resolve({type:'result',id,ok:false,error:'The request could not be sent.'});}
  });
}
if(webview)webview.addEventListener('message',e=>emit(e.data));
else root.MockBackend.connect(emit);
root.Bridge={send,request,report,on:fn=>listeners.push(fn),native:!!webview};
})(this);
