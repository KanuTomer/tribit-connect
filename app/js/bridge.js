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
  listeners.forEach(fn=>fn(message));
}
// Fire-and-forget for settings; request() for writes whose outcome the UI shows.
function send(message){if(webview)webview.postMessage(message);else root.MockBackend.receive(message);}
function request(message){
  const id=next++;
  return new Promise(resolve=>{pending.set(id,resolve);send({...message,id});});
}
if(webview)webview.addEventListener('message',e=>emit(e.data));
else root.MockBackend.connect(emit);
root.Bridge={send,request,on:fn=>listeners.push(fn),native:!!webview};
})(this);
