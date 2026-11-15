// Read-only PNG-view HRO reconstruction. No browser packages required.
const fs=require('node:fs'), vm=require('node:vm'), path=require('node:path'), assert=require('node:assert/strict');
const elements=new Map(), calls=[];
function el(id) {
 if (!elements.has(id)) elements.set(id,{hidden:false,complete:true,naturalWidth:1280,dataset:{},
 addEventListener(){},getAttribute(){return null;},getContext(){return {
 drawImage(){},fillRect(){},createImageData(w,h){return {data:new Uint8ClampedArray(w*h*4)};},
 putImageData(data){el(id).pixels=data.data.slice();}};}});
 return elements.get(id);
}
const start=Date.parse('2026-11-12T21:20:00+09:00');
const fft=Object.freeze(Array(601).fill(-10));
let fail=false, empty=false, live=true, deferNext=false, release;
function frame() {
 const buffer=new ArrayBuffer(16+1200+1200*601*4), bytes=new Uint8Array(buffer), view=new DataView(buffer);
 bytes.set(Buffer.from('HROPNG1'));view.setUint32(8,empty?0:3,true);
 if(!empty) for (const x of [1,2,4]) {
  bytes[16+x]=1;
  for(let b=0;b<601;++b)view.setFloat32(1216+(x*601+b)*4,-10,true);
 }
 return buffer;
}
const source=fs.readFileSync(path.join(__dirname,'../ui/web/archive_hro.js'),'utf8').split('// PNG view:')[1];
const context=vm.createContext({console,document:{getElementById:el},fetch:async url=>{
 calls.push(url); if(fail)throw Error('network');
 if(deferNext) {deferNext=false;await new Promise(resolve=>{release=resolve;});}
 assert.ok(url.startsWith('/api/archive/png/frame?file='));
 return {ok:true,arrayBuffer:async()=>frame()};
}});
vm.runInContext(source.slice(source.indexOf('\n')),context);
const tick=()=>new Promise(resolve=>setImmediate(resolve));
(async()=>{
 context.pngArchiveSelected('BJ202611122120.png');
 el('pngLevelPlus').onclick(); await tick(); await tick();
 assert.equal(el('archivePlot').hidden,false);
 assert.match(el('pngRenderStatus').textContent,/Available 3\/1200/);
 assert.equal(calls.length,1); // One compact request; no list scan or JSON pages.
 const pixels=el('archivePlot').pixels;
 assert.deepEqual([...pixels.slice(0,4)],[0,18,25,255]); // No HRO for first second.
 assert.notDeepEqual([...pixels.slice(4,8)],[0,18,25,255]); // Nearest second is one.
 assert.deepEqual([...pixels.slice(12,16)],[0,18,25,255]); // Sequence gap stays blank.
 const n=calls.length;
 el('pngLevelPlus').onclick(); assert.equal(calls.length,n);
 assert.notDeepEqual(el('archivePlot').pixels,pixels); assert.equal(fft[0],-10);
 el('pngLevelReset').onclick(); assert.equal(el('pngDisplayLevel').textContent,'0 dB');
 live=false; await el('pngHroRefresh').onclick();
 assert.equal(calls.length,n+1); // Refresh reuses the same exact PNG time query.
 // The chosen level must apply automatically when moving to another time.
 for(let i=0;i<4;++i)el('pngLevelPlus').onclick();
 const plusFour=el('archivePlot').pixels.slice();
 const beforeSwitch=calls.length;
 const switched=context.pngArchiveSelected('BJ202611122200.png');
 assert.equal(el('archivePlot').hidden,true); assert.equal(el('archiveImage').hidden,true);
 assert.equal(el('pngDisplayLevel').textContent,'+4 dB (loading)');
 await switched;
 assert.equal(calls.length,beforeSwitch+1);
 assert.equal(el('pngDisplayLevel').textContent,'+4 dB');
 assert.deepEqual(el('archivePlot').pixels,plusFour);
 // A late response for a previous selection must never replace the current frame.
 deferNext=true;
 const older=context.pngArchiveSelected('BJ202611122220.png');
 await context.pngArchiveSelected('BJ202611122240.png');
 const latestPixels=el('archivePlot').pixels.slice();
 release(); await older;
 assert.deepEqual(el('archivePlot').pixels,latestPixels);
 el('pngOriginal').onclick(); assert.equal(el('archivePlot').hidden,true);assert.equal(el('archiveImage').hidden,false);
 assert.equal(el('pngDisplayLevel').textContent,'Original PNG');
 await context.pngArchiveSelected('BJ202611122300.png');
 assert.equal(el('pngDisplayLevel').textContent,'+4 dB');
 assert.deepEqual(el('archivePlot').pixels,plusFour);
 empty=true;await context.pngArchiveSelected('BJ202611122140.png');
 assert.match(el('pngRenderStatus').textContent,/Available 0\/1200/);
 fail=true;await el('pngHroRefresh').onclick();assert.equal(el('archivePlot').hidden,true);
 assert.match(el('pngRenderStatus').textContent,/could not be read/);
 assert.equal(el('pngDisplayLevel').textContent,'Original PNG');
 fail=false;empty=false;await el('pngHroRefresh').onclick();
 assert.equal(el('pngDisplayLevel').textContent,'+4 dB');
 console.log('PNG HRO redraw: one compact request, gaps, missing data, recoloring without requests, reset, raw-data protection and retry passed');
})().catch(error=>{console.error(error);process.exitCode=1;});
